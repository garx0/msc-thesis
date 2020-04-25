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
            auto vl = delay.vl();
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

Error Mock::calcCommon(Vlink* vl) {
    setDelay(DelayData(vl, 0, 0));
    return Error::Success;
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
        auto vl = delay.vl();
        int load = numPackets(config->voqL * config->cellSize, vl->bagB, delay.jit())
                * ceildiv(vl->smax, config->cellSize);
        vlinkLoad.Inc(vlId, load);
    }
//    for(auto [id, load]: vlinkLoad) {
//        printf("[device %d] load on out port [%d] = %d\n", port->device->id, id, load); // DEBUG
//    }
}

Error VoqA::calcCommon(Vlink* vl) {
    calcOutPrevLoad();
    if(outPrevLoadSum > config->voqL) {
        return Error(Error::BadForVoq,
                VoqOverloadVerbose(port->outPrev, port->prevDevice->id, "output"));
    }
    int64_t localMaxDelay = (config->voqL * 2 + 1 + outPrevLoadSum - vlinkLoad.Get(vl->id)) * config->cellSize
                         + sizeRound(vl->smax, config->cellSize);
    int64_t localMinDelay = sizeRound(vl->smin, config->cellSize) + config->cellSize + vl->smin;
    assert(localMaxDelay >= localMinDelay);
    auto inDelay = getInDelay(vl->id);
    setDelay(DelayData(
            vl, inDelay.dmin() + localMinDelay,
            inDelay.jit() + localMaxDelay - localMinDelay));
    return Error::Success;
}

void VoqB::calcVlinkLoad() {
    for(auto [vlId, delay]: inDelays) {
        assert(delay.ready());
        auto vl = delay.vl();
        int load = numCells(config->voqL * config->cellSize, vl->bagB, delay.jit(),
                ceildiv(vl->smax, config->cellSize), config->cellSize);
        vlinkLoad.Inc(vlId, load);
    }
//    for(auto [id, load]: vlinkLoad) {
//        printf("load on out port [%d] = %d\n", id, load); // DEBUG
//    }
}

Error VoqB::calcCommon(Vlink* vl) {
    calcOutPrevLoad();
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
    auto inDelay = getInDelay(vl->id);
    setDelay(DelayData(
            vl, inDelay.dmin() + localMinDelay,
            inDelay.jit() + localMaxDelay - localMinDelay));
    return Error::Success;
}

// == Rk,j(t) - Jk, k == curVlId
int64_t OqCellB::delayFunc(int64_t t, Vlink* curVl) const {
    int64_t curSizeRound = sizeRound(curVl->smax, config->cellSize);
    int64_t res = ceildiv_up(t, curVl->bagB) * curSizeRound - t;
    for(auto [vlId, delay]: inDelays) {
        auto vl = delay.vl();
        int nCells = ceildiv(vl->smax, config->cellSize);
        res += (vlId != curVl->id)
                * numCellsUp(t + curSizeRound - config->cellSize, vl->bagB, delay.jit(), nCells, config->cellSize)
                * config->cellSize;
    }
    return res;
}

// == Rk,j(q)* - Jk, k == curVlId
int64_t OqCellB::delayFuncRem(int q, Vlink* curVl) {
    int curSizeRound = sizeRound(curVl->smax, config->cellSize);
    int64_t value = 0;
    int64_t bags = (q - 1) * curVl->bagB;
    for(auto [vlId, delay]: inDelays) {
        auto vl = delay.vl();
        int nCells = ceildiv(vl->smax, config->cellSize);
        value += vl->id != curVl->id
                ? numCellsUp(std::min(bp, bags + curSizeRound) - config->cellSize,
                        vl->bagB, delay.jit(), nCells, config->cellSize) * config->cellSize
                : q * curSizeRound;
    }
    return std::min(bp, value) - bags;
}

Error OqCellB::calcCommon(Vlink* curVl) {
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
    int64_t curSizeRound = sizeRound(curVl->smax, config->cellSize);
    int64_t delayFuncMax = -1;
    int64_t delayFuncValue;

    assert(bp >= curSizeRound);
    // calc delayFunc in chosen points, part 1
    for(int64_t t = 0; t <= bp - curSizeRound; t += curVl->bagB) {
//        printf("p1: calc delayFunc for t=%ld\n", t); // DEBUG
        delayFuncValue = delayFunc(t, curVl);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
//        assert(delayFuncValue + getInDelay(curVl->id).jit() >= 0); // DEBUG
    }

    // calc delayFunc in chosen points, part 2
    int64_t argShift = curSizeRound - config->cellSize; // delayFunc(t) calls numCellsUp(t + argShift, ...)
    DelayData curDelay;
    int64_t maxP3Limit = -1; // max (min(BAG_l - Jit_l, sRound_l)) by l != curVlId
    for(auto [vlId, delay]: inDelays) {
        if(vlId == curVl->id) {
            curDelay = delay;
            continue;
        }
        auto vl = delay.vl();
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
                delayFuncValue = delayFunc(t, curVl);
                if(delayFuncValue > delayFuncMax) {
                    delayFuncMax = delayFuncValue;
                }
//                assert(delayFuncValue + curDelay.jit() >= 0); // DEBUG
            }
        }
    }

    // calc delayFunc in chosen points, part 3
    int64_t tmax = std::min(bp - curSizeRound, maxP3Limit - argShift - 1);
    for(int64_t t = config->cellSize; t <= tmax; t += config->cellSize) {
//        printf("p3: calc delayFunc for t=%ld\n", t); // DEBUG
        delayFuncValue = delayFunc(t, curVl);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
//        assert(delayFuncValue + curDelay.jit() >= 0); // DEBUG
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
    int qMin = numPacketsUp(bp - curSizeRound, curVl->bagB, 0);
    int qMax = numPackets(bp, curVl->bagB, curDelay.jit());
//    printf("numPackets(%ld, %ld, %ld) = %ld\n", bp, curVl->bagB, curDelay.jit(), qMax);
//    printf("qmin=%d, qmax=%d\n", qMin, qMax); // DEBUG
//    printf("%ld <= %ld < %ld\n", (qMax-1) * curVl->bagB, bp - curSizeRound + curDelay.jit(), bp + curDelay.jit());
    for(int q = qMin; q <= qMax; q++) {
//        printf("p4: calc delayFuncRem for q=%d\n", q); // DEBUG
        delayFuncValue = delayFuncRem(q, curVl);
//        printf("bp=%ld\n", bp);
//        printf("delayFuncRem(%d) = %ld\n", q, delayFuncValue); // DEBUG
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
//        assert(delayFuncValue + curDelay.jit() >= 0); // DEBUG
    }

    assert(delayFuncMax >= 0);
//    printf("delayFuncMax = %ld\n\n", delayFuncMax); // DEBUG
    int64_t dmax = curDelay.dmax() + delayFuncMax;
    int64_t dmin = curDelay.dmin() + sizeRound(curVl->smin, config->cellSize);
    assert(dmax >= dmin);
    setDelay(DelayData(curVl, dmin, dmax-dmin));
    return Error::Success;
}


