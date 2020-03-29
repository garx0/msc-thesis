#include <iostream>
#include <cstdio>
#include <set>
#include "algo.h"

Error Mock::calcCommon(int vl) {
    int64_t dmin = 0, dmax = 0;
    for(auto [id, delay]: inDelays) {
        dmin += delay.dmin();
        dmax += delay.dmax();
    }
    dmin += 100;
    dmax += 200;
    delays[vl] = DelayData(dmin, dmax-dmin);
    return Error::Success;
}

Error Mock::calcFirst(int vl) {
    delays[vl] = DelayData(0, config->getVlink(vl)->jit0b);
    return Error::Success;
}

DelayData Mock::e2e(int vl) const {
    return getDelay(vl);
}

// floor(x/y)
inline int64_t floordiv(int64_t x, int64_t y) {
    return x / y;
}

// ceil(x/y)
inline int64_t ceildiv(int64_t x, int64_t y) {
    return x / y + (x % y != 0);
}

// = ceil(x/y + 0) = floor(x/y) + 1
inline int64_t ceildiv_up(int64_t x, int64_t y) {
    return x / y + 1;
}

// = max(0, n)
inline int64_t ramp(int64_t n) {
    return n * (n >= 0);
}

// round size of packet to whole number of cells
inline int64_t sizeRound(int64_t size, int64_t cellSize) {
    return size + (-size % cellSize);
}

// round size of packet to whole number of cells if round == true
inline int64_t sizeRound(int64_t size, int64_t cellSize, bool round) {
    return size + round * (-size % cellSize);
}

inline int64_t numPackets(int64_t intvl, int64_t bag, int64_t jit) {
    return ceildiv(intvl + jit, bag);
}

// = numPackets(intvl+0, ...) (limit from above)
inline int64_t numPacketsUp(int64_t intvl, int64_t bag, int64_t jit) {
    return ceildiv_up(intvl + jit, bag);
}

inline int64_t numCells(int64_t intvl, int64_t bag, int64_t jit, int64_t nCells, int64_t cellSize) {
    return nCells * ((intvl + jit) / bag)
           + std::min(nCells,
                      ceildiv((intvl + jit) % bag - jit * ((intvl + jit) / bag == 0), cellSize));
}

// = numCells(intvl+0, ...) (limit from above)
inline int64_t numCellsUp(int64_t intvl, int64_t bag, int64_t jit, int64_t nCells, int64_t cellSize) {
    return nCells * ((intvl + jit) / bag)
           + std::min(nCells,
                      ceildiv((intvl + jit) % bag - jit * ((intvl + jit) / bag == 0), cellSize));
}

Error Voq::completeCheck(Device *sw) {
    // columns of Cij matrix are located at different PortDelays objects in next switches
    std::set<int> visited; // visited output ports
    defaultIntMap inPortLoad; // (sum of Cpq by q) by p in device->ports
    for(auto port: sw->getAllPorts()) {
        for(auto vnode: port->getAllVnodes()) {
            for(const auto& own: vnode->next) {
                Vnode* nxt = own.get();
                if(visited.find(nxt->outPrev) != visited.end()) {
                    // to avoid multiple dynamic casts of the same object ptr
                    continue;
                }
                visited.insert(nxt->outPrev);
                auto outPortDelays = dynamic_cast<Voq*>(nxt->in->delays.get());
                for(auto port2: sw->getAllPorts()) {
                    // the result will be same if we remove 'visited' variable,
                    // remove header of this cycle and change port2 to port in its body
                    inPortLoad.Inc(port2->id, outPortDelays->outPrevLoad.Get(port2->id));
                }
            }
        }
    }
    for(auto [portId, load]: inPortLoad) {
        if(load > sw->config->voqL) {
            // TODO verbose
            return Error::BadForVoq;
        }
    }
    return Error::Success;
}

void VoqA::calcVlinkLoad() {
    for(auto [vlId, delay]: inDelays) {
        assert(delay.ready());
        auto vl = config->getVlink(vlId);
        int load = numPackets(config->voqL * config->cellSize, vl->bagB, delay.jit())
                * ceildiv(vl->smax, config->cellSize);
        vlinkLoad.Inc(vlId, load);
    }
}

Error VoqA::calcCommon(int vlId) {
    calcOutPrevLoad();
    auto vl = config->getVlink(vlId);
    if(outPrevLoadSum > config->voqL) {
        // TODO verbose
        return Error::BadForVoq;
    }
    int64_t localMaxDelay = (config->voqL * 2 + 1 + outPrevLoadSum - vlinkLoad.Get(vlId)) * config->cellSize
                         + sizeRound(vl->smax, config->cellSize);
    int64_t localMinDelay = sizeRound(vl->smin, config->cellSize) + config->cellSize + vl->smin;
    assert(localMaxDelay >= localMinDelay);
    auto inDelay = getInDelay(vlId);
    delays[vlId] = DelayData(
            inDelay.dmin() + localMinDelay,
            inDelay.jit() + localMaxDelay - localMinDelay);
    return Error::Success;
}

Error VoqA::calcFirst(int vlId) {
    auto vl = config->getVlink(vlId);
    int64_t dmin = vl->smin;
    int64_t jit = vl->jit0b + sizeRound(vl->smax, config->cellSize) - vl->smin;
    delays[vlId] = DelayData(dmin, jit);
    return Error::Success;
}

DelayData VoqA::e2e(int vlId) const {
    auto delay = getDelay(vlId);
    assert(delay.ready());
    return DelayData(delay.dmin(), delay.jit() - config->cellSize);
}

void VoqB::calcVlinkLoad() {
    for(auto [vlId, delay]: inDelays) {
        assert(delay.ready());
        auto vl = config->getVlink(vlId);
        int load = numCells(config->voqL * config->cellSize, vl->bagB, delay.jit(),
                ceildiv(vl->smax, config->cellSize), config->cellSize);
        vlinkLoad.Inc(vlId, load);
    }
}

Error VoqB::calcCommon(int vlId) {
    calcOutPrevLoad();
    auto vl = config->getVlink(vlId);
    if(outPrevLoadSum > config->voqL) {
        // TODO verbose
        return Error::BadForVoq;
    }
    int64_t localMinDelay = sizeRound(vl->smin, config->cellSize) + config->voqL * config->cellSize
                    + std::min(config->cellSize, vl->smin);
    int64_t localMaxDelay;
    int64_t nCells = ceildiv(vl->smax, config->cellSize);
    if(nCells < config->voqL) {
        localMaxDelay = (nCells + config->voqL + outPrevLoadSum * 2 + 1) * config->cellSize - vl->smin;
    } else {
        localMaxDelay = (nCells + config->voqL * 2 + 1) * config->cellSize;
    }
    assert(localMaxDelay >= localMinDelay);
    auto inDelay = getInDelay(vlId);
    delays[vlId] = DelayData(
            inDelay.dmin() + localMinDelay,
            inDelay.jit() + localMaxDelay - localMinDelay);
    return Error::Success;
}

Error VoqB::calcFirst(int vlId) {
    auto vl = config->getVlink(vlId);
    int64_t dmin = std::min(config->cellSize, vl->smin);
    int64_t jit = vl->jit0b + config->cellSize + ramp(config->cellSize - vl->smin);
    delays[vlId] = DelayData(dmin, jit);
    return Error::Success;
}

DelayData VoqB::e2e(int vlId) const {
    auto delay = getDelay(vlId);
    assert(delay.ready());
    auto vl = config->getVlink(vlId);
    int64_t dmin = delay.dmin() + ramp(vl->smin - config->cellSize);
    int64_t dmax = delay.dmax() + vl->smax - config->cellSize * 2;
    return DelayData(dmin, dmax - dmin);
}

Error OqPacket::calcCommon(int vlId) {
    // TODO
    return Error::Success;
}

Error OqPacket::calcFirst(int vlId) {
    // TODO
    return Error::Success;
}

DelayData OqPacket::e2e(int vlId) const {
    // TODO
    return DelayData();
}