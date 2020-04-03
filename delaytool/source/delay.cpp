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

int64_t busyPeriod(const std::map<int, DelayData>& inDelays, VlinkConfig* config, bool byTick = false) {
    static int64_t maxIt = 10000;
    int it = 0;
    int64_t bp = 1;
    int64_t bpPrev = 0;
    while(bp != bpPrev) {
        bpPrev = bp;
        bp = 0;
        for(auto [vlId, delay]: inDelays) {
            auto vl = config->getVlink(vlId);
            bp += numPackets(bpPrev, vl->bagB, delay.jit()) * sizeRound(vl->smax, config->cellSize, byTick);
        }
        if(++it >= maxIt) {
            return -1;
        }
    }
    return bp;
}

// == Rk,j(t) - Jk, k == curVlId
int64_t OqPacket::delayFunc(int64_t t, int curVlId) const {
    int64_t res = -t;
    for(auto [vlId, delay]: inDelays) {
        auto vl = config->getVlink(vlId);
        res += numPacketsUp(t, vl->bagB, (vlId != curVlId) * delay.jit()) * vl->smax;
    }
    return res;
}

// == Rk,j(q)* - Jk, k == curVlId
int64_t OqPacket::delayFuncRem(int q, int curVlId) {
    if(delayFuncRemConstPart == std::numeric_limits<int64_t>::min()) {
        // delayFuncRemConstPart wasn't calculated
        delayFuncRemConstPart = -bp;
        for(auto [vlId, delay]: inDelays) {
            auto vl = config->getVlink(vlId);
            delayFuncRemConstPart += numPacketsUp(bp - vl->smax, vl->bagB, delay.jit()) * vl->smax;
        }
    }
    auto curVl = config->getVlink(curVlId);
    int64_t curJit = getInDelay(curVlId).jit();
    return delayFuncRemConstPart
            + (q + 1 - numPacketsUp(bp - curVl->smax, curVl->bagB, curJit)) * curVl->smax
            - ramp((q - 1) * curVl->bagB + curVl->smax - bp);
}

Error OqPacket::calcCommon(int curVlId) {
    if(bp < 0) {
        bp = busyPeriod(inDelays, config, byTick);
        if(bp < 0) {
            return Error::BpDiverge;
        }
    }
    auto curVl = config->getVlink(curVlId);
    int64_t delayFuncMax = -1;
    int64_t delayFuncValue;

    // calc delayFunc in chosen points, part 1
    for(int64_t t = 0; t <= bp - curVl->smax; t += curVl->bagB) {
        delayFuncValue = delayFunc(t, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // calc delayFunc in chosen points, part 2
    DelayData curDelay;
    for(auto [vlId, delay]: inDelays) {
        if(vlId == curVlId) {
            curDelay = delay;
            continue;
        }
        auto vl = config->getVlink(vlId);
        for(int64_t t = ceildiv(delay.jit(), vl->bagB) * vl->bagB - delay.jit();
            t <= bp - curVl->smax;
            t += vl->bagB)
        {
            delayFuncValue = delayFunc(t, curVlId);
            if(delayFuncValue > delayFuncMax) {
                delayFuncMax = delayFuncValue;
            }
        }
    }

    // calc delayFuncRem in chosen points
    int qMin = numPacketsUp(bp - curVl->smin, curVl->bagB, 0);
    int qMax = numPackets(bp, curVl->bagB, curDelay.jit());
    for(int q = qMin; q <= qMax; q++) {
        delayFuncValue = delayFuncRem(q, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }
    assert(delayFuncMax >= 0);
    int64_t dmax = delayFuncMax + curDelay.dmax();
    int64_t dmin = curDelay.dmin() + curVl->smin;
    assert(dmax >= dmin);
    delays[curVlId] = DelayData(dmin, dmax-dmin);
    return Error::Success;
}

Error OqPacket::calcFirst(int vlId) {
    auto vl = config->getVlink(vlId);
    int64_t dmin = vl->smin;
    int64_t jit = vl->jit0b + vl->smax - vl->smin;
    delays[vlId] = DelayData(dmin, jit);
    return Error::Success;
}

DelayData OqPacket::e2e(int vlId) const {
    return getDelay(vlId);
}

// == Rk,j(t) - Jk, k == curVlId
int64_t OqCellA::delayFunc(int64_t t, int curVlId) const {
    auto curVl = config->getVlink(curVlId);
    int64_t curSizeRound = sizeRound(curVl->smax, config->cellSize);
    int64_t res = ceildiv_up(t, curVl->bagB) * curSizeRound - t;
    for(auto [vlId, delay]: inDelays) {
        auto vl = config->getVlink(vlId);
        int nCells = ceildiv(vl->smax, config->cellSize);
        res += (vlId != curVlId)
                * numCellsUp(t + curSizeRound - config->cellSize, vl->bagB, delay.jit(), nCells, config->cellSize)
                * config->cellSize;
    }
    return res;
}

// == Rk,j(q)* - Jk, k == curVlId
int64_t OqCellA::delayFuncRem(int q, int curVlId) {
    auto curVl = config->getVlink(curVlId);
    int64_t curSizeRound = sizeRound(curVl->smax, config->cellSize);
    int curNcells = ceildiv(curVl->smax, config->cellSize);
    if(delayFuncRemConstPart == std::numeric_limits<int64_t>::min()) {
        // delayFuncRemConstPart wasn't calculated
        delayFuncRemConstPart = -bp;
        for(auto [vlId, delay]: inDelays) {
            auto vl = config->getVlink(vlId);
            int nCells = ceildiv(vl->smax, config->cellSize);
            delayFuncRemConstPart +=
                    numCellsUp(bp - config->cellSize, vl->bagB, delay.jit(), nCells, config->cellSize)
                    * config->cellSize;
        }
    }
    int64_t curJit = getInDelay(curVlId).jit();
    return delayFuncRemConstPart
           + (q + 1) * curSizeRound
           - numCellsUp(bp - config->cellSize, curVl->bagB, curJit, curNcells, config->cellSize)
           * config->cellSize
           - ramp((q - 1) * curVl->bagB + curSizeRound - bp);
}

Error OqCellA::calcCommon(int curVlId) {
    if(bp < 0) {
        bp = busyPeriod(inDelays, config, true);
        if(bp < 0) {
            return Error::BpDiverge;
        }
    }
    auto curVl = config->getVlink(curVlId);
    int64_t curSizeRound = sizeRound(curVl->smax, config->cellSize);
    int64_t delayFuncMax = -1;
    int64_t delayFuncValue;

    // calc delayFunc in chosen points, part 1
    for(int64_t t = 0; t <= bp - curSizeRound; t += curVl->bagB) {
        delayFuncValue = delayFunc(t, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // calc delayFunc in chosen points, part 2
    DelayData curDelay;
    int64_t maxSizeRound = -1; // max sizeRound_l, l != curVlId
    for(auto [vlId, delay]: inDelays) {
        if(vlId == curVlId) {
            curDelay = delay;
            continue;
        }
        auto vl = config->getVlink(vlId);
        int64_t sRound = sizeRound(vl->smax, config->cellSize);
        if(sRound > maxSizeRound) {
            maxSizeRound = sRound;
        }
        for(int64_t t1 = vl->bagB - delay.jit();
            ;
            t1 += vl->bagB)
        {
            bool end = false;
            int64_t t2max = sizeRound(std::min<int64_t>(vl->smax, vl->bagB - delay.jit()), config->cellSize)
                    - curSizeRound;
            for(int64_t t2 = - curSizeRound + 1;
                t2 <= t2max;
                t2 += config->cellSize)
            {
                int64_t t = t1 + t2;
                if(t < 0) {
                    continue;
                } else if(t > bp - curSizeRound) {
                    end = true;
                    break;
                }
                delayFuncValue = delayFunc(t, curVlId);
                if(delayFuncValue > delayFuncMax) {
                    delayFuncMax = delayFuncValue;
                }
            }
            if(end) {
                break;
            }
        }
    }

    // calc delayFunc in chosen points, part 3
    int64_t tmax = std::min(bp, maxSizeRound) - curSizeRound;
    for(int64_t t = config->cellSize; t <= tmax; t += config->cellSize) {
        delayFuncValue = delayFunc(t, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // calc delayFuncRem in chosen points
    int qMin = numPacketsUp(bp - sizeRound(curVl->smin, config->cellSize), curVl->bagB, 0);
    int qMax = numPackets(bp, curVl->bagB, curDelay.jit());
    for(int q = qMin; q <= qMax; q++) {
        delayFuncValue = delayFuncRem(q, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    assert(delayFuncMax >= 0);
    int64_t dmax = delayFuncMax + curDelay.dmax();
    int64_t dmin = curDelay.dmin() + sizeRound(curVl->smin, config->cellSize);
    assert(dmax >= dmin);
    delays[curVlId] = DelayData(dmin, dmax-dmin);
    return Error::Success;
}

// not used
Error OqCellA::calcFirst(int vlId) {
    auto vl = config->getVlink(vlId);
    int64_t dmin = vl->smin;
    int64_t jit = vl->jit0b + sizeRound(vl->smax, config->cellSize) - vl->smin;
    delays[vlId] = DelayData(dmin, jit);
    return Error::Success;
}

// not used
DelayData OqCellA::e2e(int vlId) const {
    auto delay = getDelay(vlId);
    assert(delay.ready());
    return DelayData(delay.dmin(), delay.jit() - config->cellSize);
}

