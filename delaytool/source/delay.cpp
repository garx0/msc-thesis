#include <set>
#include "algo.h"
#include "delay.h"

int64_t QRTA::busyPeriod(const std::map<std::pair<int,int>, DelayData>& inDelays, VlinkConfig* config) {
    uint64_t it = 1;
    int64_t bp = 1;
    int64_t bpPrev = 0;
    for(; bp != bpPrev; it++) {
        bpPrev = bp;
        bp = 0;
        for(auto [vlBranch, delay]: inDelays) {
            auto vl = delay.vl();
            bp += numPackets(bpPrev, vl->bagB, delay.jit()) * vl->smax;
        }
        if(config->bpMaxIter != 0 && it >= config->bpMaxIter) {
            return -1;
        }
    }
    return bp;
}

// == Rk,j(t) - Jk, k == curVlId
int64_t QRTA::delayFunc(int64_t t, Vlink* curVl, int curBranchId) const {
    int64_t res = -t;
    for(auto [vlBranch, delay]: inDelays) {
        auto [vlId, branchId] = vlBranch;
        auto vl = delay.vl();
        assert(vl->id == vlId);
        bool cur = (vl->id == curVl->id) && (branchId == curBranchId);
        res += numPacketsUp(t, vl->bagB, (!cur) * delay.jit()) * vl->smax;
    }
    return res;
}

// == Rk,j(q)* - Jk, k == curVlId
int64_t QRTA::delayFuncRem(int q, Vlink* curVl, int curBranchId) const {
    int64_t value = 0;
    int64_t bags = (q - 1) * curVl->bagB;
    for(auto [vlBranch, delay]: inDelays) {
        auto [vlId, branchId] = vlBranch;
        auto vl = delay.vl();
        assert(vl->id == vlId);
        bool cur = (vl->id == curVl->id) && (branchId == curBranchId);
        value += vl->smax * (
                !cur
                ? numPacketsUp(std::min(bp - curVl->smax, bags), vl->bagB, delay.jit())
                : q);
    }
    return std::min(bp, value) - bags;
}

Error QRTA::clear_bp() {
    bp = -1;
    return Error::Success;
}

Error QRTA::calc_bp() {
    if(bp < 0) {
        double rate_ratio = total_rate();
        if(rate_ratio >= 1) {
            return Error::BpTooLong;
        }
        bp = busyPeriod(inDelays, config);
        if (bp < 0) {
            return Error::BpTooLong;
        }
    }
    return Error::Success;
}

Error QRTA::calc(Vlink* curVl, int curBranchId) {
    Error err = calc_bp();
    if(err) {
        return err;
    }

    int64_t delayFuncMax = -1;
    int64_t delayFuncValue;

    // calc delayFunc in chosen points, part 1
    for(int64_t t = 0; t <= bp - curVl->smax; t += curVl->bagB) {
        delayFuncValue = delayFunc(t, curVl, curBranchId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // calc delayFunc in chosen points, part 2
    DelayData curDelay;
    for(auto [vlBranch, delay]: inDelays) {
        auto[vlId, branchId] = vlBranch;
        if(vlId == curVl->id && branchId == curBranchId) {
            curDelay = delay;
            continue;
        }
        auto vl = delay.vl();
        for(int64_t t = roundToMultiple(delay.jit(), vl->bagB) - delay.jit();
            t <= bp - curVl->smax;
            t += vl->bagB)
        {
            delayFuncValue = delayFunc(t, curVl, curBranchId);
            if(delayFuncValue > delayFuncMax) {
                delayFuncMax = delayFuncValue;
            }
        }
    }

    // calc delayFuncRem in chosen points
    int qMin = numPacketsUp(bp - curVl->smin, curVl->bagB, 0);
    int qMax = numPackets(bp, curVl->bagB, curDelay.jit());
    for(int q = qMin; q <= qMax; q++) {
        delayFuncValue = delayFuncRem(q, curVl, curBranchId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }
    assert(delayFuncMax >= 0);
    int64_t dmax = delayFuncMax + curDelay.dmax();
    int64_t dmin = curDelay.dmin() + curVl->smin;
    assert(dmax >= dmin);
    calc_result = DelayData(curVl, nullptr, dmin, dmax-dmin);
    return Error::Success;
}

// sum BW of concurring virtual links / link rate
double QRTA::total_rate() {
    double s = 0;
    for(auto [vlBranch, delay]: inDelays) {
        auto vl = delay.vl();
        s += static_cast<double>(vl->smax) / vl->bagB;
    }
    return s;
}