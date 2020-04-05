#include <set>
#include "algo.h"
#include "delay.h"

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

int64_t busyPeriod(const std::map<int, DelayData>& inDelays, VlinkConfig* config, bool byTick) {
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
        printf("bpPrev = %ld, bp = %ld\n", bpPrev, bp); // DEBUG
        if(++it >= maxIt) {
            return -1;
        }
    }
//    if(it > 1) {
//        printf("\t\t\t\tBP CALCULATED IN %d ITERATIONS\n", it); // DEBUG
//    }
    return bp;
}

Error calcFirstA(PortDelays* scheme, int vlId) {
    auto vl = scheme->config->getVlink(vlId);
    int64_t dmin = vl->smin;
    int64_t jit = vl->jit0b + sizeRound(vl->smax, scheme->config->cellSize) - vl->smin;
    scheme->delays[vlId] = DelayData(dmin, jit);
    return Error::Success;
}

Error calcFirstB(PortDelays* scheme, int vlId) {
    auto vl = scheme->config->getVlink(vlId);
    int64_t dmin = std::min(scheme->config->cellSize, vl->smin);
    int64_t jit = vl->jit0b + scheme->config->cellSize + ramp(scheme->config->cellSize - vl->smin);
    scheme->delays[vlId] = DelayData(dmin, jit);
    return Error::Success;
}

DelayData e2eA(const PortDelays* scheme, int vlId) {
    auto delay = scheme->getDelay(vlId);
    assert(delay.ready());
    return DelayData(delay.dmin(), delay.jit() - scheme->config->cellSize);
}

DelayData e2eB(const PortDelays* scheme, int vlId) {
    auto delay = scheme->getDelay(vlId);
    assert(delay.ready());
    auto vl = scheme->config->getVlink(vlId);
    int64_t dmin = delay.dmin() + ramp(vl->smin - scheme->config->cellSize);
    int64_t dmax = delay.dmax() + vl->smax - scheme->config->cellSize * 2;
    return DelayData(dmin, dmax - dmin);
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
    return calcFirstA(this, vlId);
}

DelayData VoqA::e2e(int vlId) const {
    return e2eA(this, vlId);
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
    return calcFirstB(this, vlId);

}

DelayData VoqB::e2e(int vlId) const {
    return e2eB(this, vlId);
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

    assert(bp >= curSizeRound);
    // calc delayFunc in chosen points, part 1
    for(int64_t t = 0; t <= bp - curSizeRound; t += curVl->bagB) {
        printf("p1: calc delayFunc for t=%ld\n", t); // DEBUG
        delayFuncValue = delayFunc(t, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // calc delayFunc in chosen points, part 2
    DelayData curDelay;
    int64_t maxSizeRound = -1; // max sizeRound_l by l != curVlId
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
        for(int64_t t1 = vl->bagB - delay.jit(); t1 < bp; t1 += vl->bagB) {
            // t2 <= sizeRound(min(...), cellSize) - curSizeRound && t1 + t2 <= bp - curSizeRound
            int64_t tmax = std::min<int64_t>(
                    t1 + sizeRound(std::min<int64_t>(vl->smax, vl->bagB - delay.jit()), config->cellSize),
                    bp
                ) - curSizeRound;
            for(int64_t t = t1 - curSizeRound + 1;
                t <= tmax;
                t += config->cellSize)
            {
                if(t < 0) {
                    continue;
                }
                printf("p2: calc delayFunc for t=%ld\n", t); // DEBUG
                delayFuncValue = delayFunc(t, curVlId);
                if(delayFuncValue > delayFuncMax) {
                    delayFuncMax = delayFuncValue;
                }
            }
        }
    }

    // calc delayFunc in chosen points, part 3
    int64_t tmax = std::min(bp, maxSizeRound) - curSizeRound;
    for(int64_t t = config->cellSize; t <= tmax; t += config->cellSize) {
        printf("p3: calc delayFunc for t=%ld\n", t); // DEBUG
        delayFuncValue = delayFunc(t, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // DEBUG
    int64_t delayFuncMax2 = -1;
    for(int64_t t = 0; t <= bp - curVl->smax; t++) {
        delayFuncValue = delayFunc(t, curVlId);
        if(delayFuncValue > delayFuncMax2) {
            delayFuncMax2 = delayFuncValue;
        }
    }
    printf("--------------------------------DELAYFUNC MAX CALC COMPARE: %ld <= %ld\n", delayFuncMax, delayFuncMax2);
    // /DEBUG

    // calc delayFuncRem in chosen points
    int qMin = numPacketsUp(bp - sizeRound(curVl->smin, config->cellSize), curVl->bagB, 0);
    int qMax = numPackets(bp, curVl->bagB, curDelay.jit());
    for(int q = qMin; q <= qMax; q++) {
        printf("p4: calc delayFuncRem for q=%d\n", q); // DEBUG
        delayFuncValue = delayFuncRem(q, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    assert(delayFuncMax >= 0);
    printf("delayFuncMax = %ld\n\n", delayFuncMax); // DEBUG
    int64_t dmax = curDelay.dmax() + delayFuncMax;
    int64_t dmin = curDelay.dmin() + sizeRound(curVl->smin, config->cellSize);
    assert(dmax >= dmin);
    delays[curVlId] = DelayData(dmin, dmax-dmin);
    return Error::Success;
}

Error OqCellA::calcFirst(int vlId) {
    return calcFirstA(this, vlId);
}

DelayData OqCellA::e2e(int vlId) const {
    return e2eA(this, vlId);
}

template<>
Error OqA::calcFirst(int vlId) {
    return calcFirstA(this, vlId);
}

template<>
Error OqB::calcFirst(int vlId) {
    return calcFirstB(this, vlId);
}

template<>
DelayData OqA::e2e(int vlId) const {
    return e2eA(this, vlId);
}

template<>
DelayData OqB::e2e(int vlId) const {
    return e2eB(this, vlId);
}

