#include <set>
#include "algo.h"
#include "delay.h"

int64_t busyPeriod(const std::map<int, DelayData>& inDelays, VlinkConfig* config, bool byTick) {
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
        if(++it >= maxBpIter) {
            return -1;
        }
    }
//    if(it > 1) {
//        printf("\t\t\t\tBP CALCULATED IN %d ITERATIONS\n", it); // DEBUG
//    }
//    printf("bp<%d> = %ld\n", byTick, bp); // DEBUG
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


Error Mock::calcCommon(int vl) {
    delays[vl] = DelayData(0, 0);
    return Error::Success;
}

Error Mock::calcFirst(int vl) {
    delays[vl] = DelayData(0, config->getVlink(vl)->jit0b);
    return Error::Success;
}

DelayData Mock::e2e(int vl) const {
    return getDelay(vl);
}

Error completeCheckVoq(Device *sw) {
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
//        printf("load on in port [%d] = %d\n", portId, load); // DEBUG
        if(load > sw->config->voqL) {
            std::string verbose =
                    "overload on input port "
                    + std::to_string(portId)
                    + " of switch "
                    + std::to_string(sw->id);
            return Error(Error::BadForVoq, VoqOverloadVerbose(portId, sw->id, "input"));
        }
    }
    return Error::Success;
}

void Voq::calcOutPrevLoad() {
    if(loadReady) {
        return;
    }
    calcVlinkLoad();
    Device* prevSwitch = nullptr;
    for(auto [vlId, load]: vlinkLoad) {
        prevSwitch = port->vnodes[vlId]->prev->device;
        assert(prevSwitch->type == Device::Switch);
        break;
    }
    for(auto [vlId, load]: vlinkLoad) {
        Vnode *prevNode = port->vnodes[vlId]->prev;
        assert(prevNode->device->id == prevSwitch->id);
        outPrevLoad.Inc(prevNode->in->id, load);
        outPrevLoadSum += load;
        break;
    }
    loadReady = true;
}

void VoqA::calcVlinkLoad() {
    for(auto [vlId, delay]: inDelays) {
        assert(delay.ready());
        auto vl = config->getVlink(vlId);
        int load = numPackets(config->voqL * config->cellSize, vl->bagB, delay.jit())
                * ceildiv(vl->smax, config->cellSize);
        vlinkLoad.Inc(vlId, load);
    }
//    for(auto [id, load]: vlinkLoad) {
//        printf("[device %d] load on out port [%d] = %d\n", port->device->id, id, load); // DEBUG
//    }
}

Error VoqA::calcCommon(int vlId) {
    calcOutPrevLoad();
    auto vl = config->getVlink(vlId);
    if(outPrevLoadSum > config->voqL) {
        return Error(Error::BadForVoq,
                VoqOverloadVerbose(port->outPrev, port->prevDevice->id, "output"));
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
//    for(auto [id, load]: vlinkLoad) {
//        printf("load on out port [%d] = %d\n", id, load); // DEBUG
//    }
}

Error VoqB::calcCommon(int vlId) {
    calcOutPrevLoad();
    auto vl = config->getVlink(vlId);
    if(outPrevLoadSum > config->voqL) {
        return Error(Error::BadForVoq,
                VoqOverloadVerbose(port->outPrev, port->prevDevice->id, "output"));
    }
    int curSizeRoundMin = sizeRound(vl->smin, config->cellSize);
    int64_t localMinDelay = std::min(config->voqL * config->cellSize, curSizeRoundMin)
            + curSizeRoundMin +
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
int64_t OqCellB::delayFunc(int64_t t, int curVlId) const {
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
int64_t OqCellB::delayFuncRem(int q, int curVlId) {
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
           + ramp(curJit - ramp(curSizeRound + (q - 1) * curVl->bagB - bp))
           - curJit;
}

Error OqCellB::calcCommon(int curVlId) {
    if(bp < 0) {
        bp = busyPeriod(inDelays, config, true);
        if(bp < 0) {
            std::string verbose =
                    "overload on output port "
                    + std::to_string(port->outPrev)
                    + " of switch "
                    + std::to_string(port->prevDevice->id)
                    + " because busy period calculation took over "
                    + std::to_string(maxBpIter)
                    + " iterations";
            return Error(Error::BpDiverge, OqOverloadVerbose(port));
        }
    }
    auto curVl = config->getVlink(curVlId);
    int64_t curSizeRound = sizeRound(curVl->smax, config->cellSize);
    int64_t delayFuncMax = -1;
    int64_t delayFuncValue;

    assert(bp >= curSizeRound);
    // calc delayFunc in chosen points, part 1
    for(int64_t t = 0; t <= bp - curSizeRound; t += curVl->bagB) {
//        printf("p1: calc delayFunc for t=%ld\n", t); // DEBUG
        delayFuncValue = delayFunc(t, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // calc delayFunc in chosen points, part 2
    int64_t argShift = curSizeRound - config->cellSize; // delayFunc(t) calls numCellsUp(t + argShift, ...)
    DelayData curDelay;
    int64_t maxP3Limit = -1; // max (min(BAG_l - Jit_l, sRound_l)) by l != curVlId
    for(auto [vlId, delay]: inDelays) {
        if(vlId == curVlId) {
            curDelay = delay;
            continue;
        }
        auto vl = config->getVlink(vlId);
        int64_t sRound = sizeRound(vl->smax, config->cellSize);
        int64_t p3Limit = std::min(sRound, vl->bagB - delay.jit());
        if(p3Limit > maxP3Limit) {
            maxP3Limit = p3Limit;
        }
        for(int64_t t1 = vl->bagB - delay.jit() - argShift; t1 <= bp - curSizeRound; t1 += vl->bagB) {
            int64_t tmax = std::min(bp - curSizeRound, t1 + sRound - 1);
            for(int64_t t = t1; t <= tmax; t += config->cellSize) {
                if(t < 0) {
                    continue;
                }
//                printf("p2: calc delayFunc for t=%ld\n", t); // DEBUG
                delayFuncValue = delayFunc(t, curVlId);
                if(delayFuncValue > delayFuncMax) {
                    delayFuncMax = delayFuncValue;
                }
            }
        }
    }

    // calc delayFunc in chosen points, part 3
    int64_t tmax = std::min(bp - curSizeRound, maxP3Limit - argShift - 1);
    for(int64_t t = config->cellSize; t <= tmax; t += config->cellSize) {
//        printf("p3: calc delayFunc for t=%ld\n", t); // DEBUG
        delayFuncValue = delayFunc(t, curVlId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // DEBUG
//    int64_t delayFuncMax2 = -1;
//    int64_t delayFuncArgMax2 = -1;
//    for(int64_t t = 0; t <= bp - curVl->smax; t++) {
//        delayFuncValue = delayFunc(t, curVlId);
////        printf("f(%ld)=%ld; ", t, delayFuncValue);
////        printf("f(%ld)+t=%ld\n", t, delayFuncValue + t);
//        if(delayFuncValue > delayFuncMax2) {
//            delayFuncArgMax2 = t;
//            delayFuncMax2 = delayFuncValue;
//        }
//    }
//    if(delayFuncMax != delayFuncMax2) {
//        printf("--------------------------------NOT REACHED MAX: %ld (on %ld) <= %ld (on %ld)\n",
//                delayFuncMax, delayFuncArgMax, delayFuncMax2, delayFuncArgMax2);
//    } else {
//        printf("--------------------------------reached max: %ld (on %ld) == %ld (on %ld)\n",
//               delayFuncMax, delayFuncArgMax, delayFuncMax2, delayFuncArgMax2);
//    }
//    assert(delayFuncMax == delayFuncMax2);
//    printf("delayFunc max = %ld\n", delayFuncMax);
    // /DEBUG

    // calc delayFuncRem in chosen points
    int qMin = numPacketsUp(bp - sizeRound(curVl->smin, config->cellSize), curVl->bagB, 0);
    int qMax = numPackets(bp, curVl->bagB, curDelay.jit());
//    printf("qmin=%d, qmax=%d\n", qMin, qMax); // DEBUG
    for(int q = qMin; q <= qMax; q++) {
//        printf("p4: calc delayFuncRem for q=%d\n", q); // DEBUG
        delayFuncValue = delayFuncRem(q, curVlId);
//        printf("delayFuncRem(%d) = %ld\n", q, delayFuncValue); // DEBUG
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    assert(delayFuncMax >= 0);
//    printf("delayFuncMax = %ld\n\n", delayFuncMax); // DEBUG
    int64_t dmax = curDelay.dmax() + delayFuncMax;
    int64_t dmin = curDelay.dmin() + sizeRound(curVl->smin, config->cellSize);
    assert(dmax >= dmin);
    delays[curVlId] = DelayData(dmin, dmax-dmin);
    return Error::Success;
}

Error OqCellB::calcFirst(int vlId) {
    return calcFirstB(this, vlId);
}

DelayData OqCellB::e2e(int vlId) const {
    return e2eB(this, vlId);
}

template<>
DelayData OqA::completeDelay(DelayData delay, int vlId) const {
    return DelayData(delay.dmin(), delay.jit() + config->cellSize);
}

template<>
Error OqA::calcFirst(int vlId) {
    return calcFirstA(this, vlId);
}

template<>
DelayData OqA::e2e(int vlId) const {
    return e2eA(this, vlId);
}

template<>
DelayData OqB::completeDelay(DelayData delay, int vlId) const {
    auto vl = config->getVlink(vlId);
    int64_t dmaxInc = config->cellSize * 2 - vl->smax;
    int64_t dminInc = -ramp(vl->smin - config->cellSize);
    assert(delay.dmin() + dminInc <= delay.dmax() + dmaxInc);
    return DelayData(delay.dmin() + dminInc, delay.jit() + dmaxInc - dminInc);
}

template<>
Error OqB::calcFirst(int vlId) {
    return calcFirstB(this, vlId);
}

template<>
DelayData OqB::e2e(int vlId) const {
    return e2eB(this, vlId);
}




