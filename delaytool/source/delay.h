#pragma once
#ifndef DELAYTOOL_DELAY_H
#define DELAYTOOL_DELAY_H
#include "algo.h"

// floor(x/y)
inline int64_t floordiv(int64_t x, int64_t y) {
    return x / y;
}

// ceil(x/y), x >= 0
inline int64_t ceildiv(int64_t x, int64_t y) {
    return x / y + (x % y != 0);
}

// = ceil(x/y + 0) = floor(x/y) + 1, x >= 0
inline int64_t ceildiv_up(int64_t x, int64_t y) {
    return x / y + 1;
}

inline int64_t numPackets(int64_t interval, int64_t bag, int64_t jit) {
    return ceildiv(interval + jit, bag);
}

// = numPackets(interval+0, ...) (limit from above)
inline int64_t numPacketsUp(int64_t interval, int64_t bag, int64_t jit) {
    return ceildiv_up(interval + jit, bag);
}

// round x to a next multiple of k
inline int64_t roundToMultiple(int64_t x, int64_t k) {
    return x + k * (x % k != 0) - x % k;
}

inline std::string OqOverloadVerbose(Vlink* vl, Device* device) {
    return std::string("overload for vl ")
           + std::to_string(vl->id)
           + " of switch "
           + std::to_string(device->id)
           + " because busy period calculation took over "
           + std::to_string(device->config->bpMaxIter)
           + " iterations";
}

class QRTA
{
public:
    QRTA(VlinkConfig* config): config(config), bp(-1) {}

    // == Rk,CVL(t) - Jk, k == vl->id
    int64_t delayFunc(int64_t t, Vlink* vl, int cur_branch_id) const;

    // == Rk,CVL(q)* - Jk, k == vl->id
    int64_t delayFuncRem(int q, Vlink* vl, int cur_branch_id) const;

    Error calc_bp();

    DelayData calc_result;

    void setInDelays(const std::map<std::pair<int, int>, DelayData>& _inDelays) {
        inDelays = _inDelays;
    }

    // recalculates bp only if it is empty
    Error calc(Vlink* curVl, int cur_branch_id);

    Error clear_bp();

    double total_rate();

private:
    VlinkConfig* config;
    int64_t bp;
    std::map<std::pair<int, int>, DelayData> inDelays;

    static int64_t busyPeriod(const std::map<std::pair<int, int>, DelayData>& inDelays, VlinkConfig* config);
};

#endif //DELAYTOOL_DELAY_H
