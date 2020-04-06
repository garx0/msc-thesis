#pragma once
#ifndef DELAYTOOL_DELAY_H
#define DELAYTOOL_DELAY_H
#include "algo.h"

constexpr int64_t maxBpIter = 10000;

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
    return size + cellSize * (size % cellSize != 0) - size % cellSize;
}

// round size of packet to whole number of cells if round == true
inline int64_t sizeRound(int64_t size, int64_t cellSize, bool round) {
    return size + round * (cellSize * (size % cellSize != 0) - size % cellSize);
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
                      ceildiv_up((intvl + jit) % bag - jit * ((intvl + jit) / bag == 0), cellSize));
}

int64_t busyPeriod(const std::map<int, DelayData>& inDelays, VlinkConfig* config, bool byTick = false);

Error calcFirstA(PortDelays* scheme, int vlId);

Error calcFirstB(PortDelays* scheme, int vlId);

DelayData e2eA(const PortDelays* scheme, int vlId);

DelayData e2eB(const PortDelays* scheme, int vlId);

// == Rk,j(t) - Jk, k == curVlId
template<bool cells>
int64_t OqPacket<cells>::delayFunc(int64_t t, int curVlId) const {
    int64_t res = -t;
    for(auto [vlId, delay]: inDelays) {
        auto vl = config->getVlink(vlId);
        res += numPacketsUp(t, vl->bagB, (vlId != curVlId) * delay.jit()) * vl->smax;
    }
    return res;
}

// == Rk,j(q)* - Jk, k == curVlId
template<bool cells>
int64_t OqPacket<cells>::delayFuncRem(int q, int curVlId) {
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

template<bool cells>
Error OqPacket<cells>::calcCommon(int curVlId) {
    if(bp < 0) {
        bp = busyPeriod(inDelays, config, cells);
        if(bp < 0) {
            std::string verbose =
                    "overload on output port "
                    + std::to_string(port->outPrev)
                    + " of switch "
                    + std::to_string(port->prevDevice->id)
                    + " because busy period calculation took over "
                    + std::to_string(maxBpIter)
                    + " iterations";
            return Error(Error::BpDiverge, verbose);
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
        for(int64_t t = sizeRound(delay.jit(), vl->bagB) - delay.jit();
            t <= bp - curVl->smax;
            t += vl->bagB)
        {
            delayFuncValue = delayFunc(t, curVlId);
            if(delayFuncValue > delayFuncMax) {
                delayFuncMax = delayFuncValue;
            }
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

template<bool cells>
Error OqPacket<cells>::calcFirst(int vlId) {
    auto vl = config->getVlink(vlId);
    int64_t dmin = vl->smin;
    int64_t jit = vl->jit0b + vl->smax - vl->smin;
    delays[vlId] = DelayData(dmin, jit);
    return Error::Success;
}

template<bool cells>
DelayData OqPacket<cells>::e2e(int vlId) const {
    return getDelay(vlId);
}

template<class Scheme1, class Scheme2>
Error TwoSchemes<Scheme1, Scheme2>::calcCommon(int curVlId) {
    if(!midDelaysReady) {
        scheme1.setInDelays(inDelays);
        for(auto [vlId, _]: inDelays) {
            Error err = scheme1.calcCommon(vlId);
            if(err) {
                return err;
            }
        }
        scheme2.setInDelays(scheme1.getDelays());
        midDelaysReady = true;
    }
    Error err = scheme2.calcCommon(curVlId);
    if(err) {
        return err;
    }
    delays[curVlId] = completeDelay(scheme2.getDelay(curVlId), curVlId);
    return Error::Success;
}

#endif //DELAYTOOL_DELAY_H
