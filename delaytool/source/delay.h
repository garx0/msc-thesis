#pragma once
#ifndef DELAYTOOL_DELAY_H
#define DELAYTOOL_DELAY_H
#include "algo.h"

constexpr int64_t maxBpIter = 10000;

class Mock : public PortDelays
{
public:
    Mock(Port* port): PortDelays(port, "Mock") {}

    DelayData e2e(int vl) const override;

    Error calcCommon(Vlink* vl) override;

    Error calcFirst(Vlink* vl) override;
};

class defaultIntMap : public std::map<int, int> {
public:
    defaultIntMap() = default;

    int Get(int key);

    void Inc(int key, int val);
};

// base class for VoqA and VoqB
class Voq : public PortDelays
{
public:
    Voq(Port* port, const std::string& type): PortDelays(port, type), outPrevLoadSum(0), loadReady(false) {}

    // C_l values by l on prev switch
    defaultIntMap vlinkLoad;

    // C_pj values by p on prev switch, where j ~ this->port->outPrev
    defaultIntMap outPrevLoad;

    // sum of outPrevLoad values
    int outPrevLoadSum;

    // is information about input load (outPrevLoad and its sum) ready
    bool loadReady;

    void calcOutPrevLoad();

    virtual void calcVlinkLoad() = 0;
};

class VoqA : public Voq
{
public:
    explicit VoqA(Port* port): Voq(port, "VoqA") {}

    DelayData e2e(int vl) const override;

    void calcVlinkLoad() override;

    Error calcCommon(Vlink* vl) override;

    Error calcFirst(Vlink* vl) override;
};

class VoqB : public Voq
{
public:
    explicit VoqB(Port* port): Voq(port, "VoqB") { }

    DelayData e2e(int vl) const override;

    void calcVlinkLoad() override;

    Error calcCommon(Vlink* vl) override;

    Error calcFirst(Vlink* vl) override;
};

// if cells = true, skmax is rounded up to whole number of cells
template<bool cells = false>
class OqPacket : public PortDelays
{
public:
    explicit OqPacket(Port* port)
            : PortDelays(port, "OqPacket<" + std::to_string(cells) + ">"), bp(-1),
              delayFuncRemConstPart(std::numeric_limits<int64_t>::min()) {}

    DelayData e2e(int vl) const override;

    // == Rk,j(t) - Jk, k == vl->id
    int64_t delayFunc(int64_t t, Vlink* vl) const;

    // == Rk,j(q)* - Jk, k == vl->id
    int64_t delayFuncRem(int q, Vlink* vl);

    Error calcCommon(Vlink* vl) override;

    Error calcFirst(Vlink* vl) override;

private:
    int64_t bp;
    int64_t delayFuncRemConstPart;
};

// OqB without packet FIFO
class OqCellB : public PortDelays
{
public:
    explicit OqCellB(Port* port)
            : PortDelays(port, "OqCellB"), bp(-1),
              delayFuncRemConstPart(std::numeric_limits<int64_t>::min()) {}

    DelayData e2e(int vl) const override;

    // == Rk,j(t) - Jk, k == vl->id
    int64_t delayFunc(int64_t t, Vlink* vl) const;

    // == Rk,j(q)* - Jk, k == vl->id
    int64_t delayFuncRem(int q, Vlink* vl);

    Error calcCommon(Vlink* vl) override;

    Error calcFirst(Vlink* vl) override;

private:
    int64_t bp;
    int64_t delayFuncRemConstPart;
};

// two different schemes stitched together
template<class Scheme1, class Scheme2>
class TwoSchemes : public PortDelays
{
    static_assert(std::is_base_of<PortDelays, Scheme1>::value && std::is_base_of<PortDelays, Scheme2>::value,
                  "parameters of TwoSchemes must inherit from PortDelays");
public:
    explicit TwoSchemes(Port* port)
            : PortDelays(port, "TwoSchemes"), midDelaysReady(false), scheme1(port), scheme2(port)
    {
        type += "<" + scheme1.type + ", " + scheme2.type + ">";
    }

    DelayData e2e(int vl) const override;

    Error calcCommon(Vlink* vl) override;

    Error calcFirst(Vlink* vl) override;

    // make out delay from scheme2 out delay
    DelayData completeDelay(DelayData delay) const;

private:
    bool midDelaysReady;
    Scheme1 scheme1;
    Scheme2 scheme2;
};

using OqA = TwoSchemes<OqPacket<true>, OqPacket<>>;
using OqB = TwoSchemes<OqCellB, OqPacket<>>;

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

// intvl >= 0
inline int64_t numCells(int64_t intvl, int64_t bag, int64_t jit, int64_t nCells, int64_t cellSize) {
    return nCells * ((intvl + jit) / bag)
           + std::min(nCells,
                      ceildiv((intvl + jit) % bag - jit * ((intvl + jit) / bag == 0), cellSize));
}

// = numCells(intvl+0, ...) (limit from above)
// intvl >= 0
inline int64_t numCellsUp(int64_t intvl, int64_t bag, int64_t jit, int64_t nCells, int64_t cellSize) {
    return nCells * ((intvl + jit) / bag)
           + std::min(nCells,
                      ceildiv_up((intvl + jit) % bag - jit * ((intvl + jit) / bag == 0), cellSize));
}

inline std::string VoqOverloadVerbose(int portId, int switchId, const std::string& portType) {
    return std::string("overload on ")
           + portType
           + " port "
           + std::to_string(portId)
           + " of switch "
           + std::to_string(switchId);
}

inline std::string OqOverloadVerbose(Port* port) {
    return std::string("overload on output port ")
           + std::to_string(port->outPrev)
           + " of switch "
           + std::to_string(port->prevDevice->id)
           + " because busy period calculation took over "
           + std::to_string(maxBpIter)
           + " iterations";
}

int64_t busyPeriod(const std::map<int, DelayData>& inDelays, VlinkConfig* config, bool byTick = false);

Error calcFirstA(PortDelays* scheme, Vlink* vl);

Error calcFirstB(PortDelays* scheme, Vlink* vl);

DelayData e2eA(const PortDelays* scheme, int vlId);

DelayData e2eB(const PortDelays* scheme, int vlId);

// == Rk,j(t) - Jk, k == curVlId
template<bool cells>
int64_t OqPacket<cells>::delayFunc(int64_t t, Vlink* curVl) const {
    int64_t res = -t;
    for(auto [vlId, delay]: inDelays) {
        auto vl = delay.vl();
        res += numPacketsUp(t, vl->bagB, (vlId != curVl->id) * delay.jit()) * vl->smax;
    }
    return res;
}

// == Rk,j(q)* - Jk, k == curVlId
template<bool cells>
int64_t OqPacket<cells>::delayFuncRem(int q, Vlink* curVl) {
    if(delayFuncRemConstPart == std::numeric_limits<int64_t>::min()) {
        // delayFuncRemConstPart wasn't calculated
        delayFuncRemConstPart = -bp;
        for(auto [vlId, delay]: inDelays) {
            auto vl = delay.vl();
            delayFuncRemConstPart += numPacketsUp(bp - vl->smax, vl->bagB, delay.jit()) * vl->smax;
        }
    }
    int64_t curJit = getInDelay(curVl->id).jit();
    return delayFuncRemConstPart
           + (q + 1 - numPacketsUp(bp - curVl->smax, curVl->bagB, curJit)) * curVl->smax
           + ramp(curJit - ramp(curVl->smax + (q - 1) * curVl->bagB - bp))
           - curJit;
}

template<bool cells>
Error OqPacket<cells>::calcCommon(Vlink* curVl) {
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
            return Error(Error::BpDiverge, OqOverloadVerbose(port));
        }
    }
    int64_t delayFuncMax = -1;
    int64_t delayFuncValue;

    // calc delayFunc in chosen points, part 1
    for(int64_t t = 0; t <= bp - curVl->smax; t += curVl->bagB) {
        delayFuncValue = delayFunc(t, curVl);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // calc delayFunc in chosen points, part 2
    DelayData curDelay;
    for(auto [vlId, delay]: inDelays) {
        if(vlId == curVl->id) {
            curDelay = delay;
            continue;
        }
        auto vl = delay.vl();
        for(int64_t t = sizeRound(delay.jit(), vl->bagB) - delay.jit();
            t <= bp - curVl->smax;
            t += vl->bagB)
        {
            delayFuncValue = delayFunc(t, curVl);
            if(delayFuncValue > delayFuncMax) {
                delayFuncMax = delayFuncValue;
            }
        }
    }

    // DEBUG
//    int64_t delayFuncMax2 = -1;
//    for(int64_t t = 0; t <= bp - curVl->smax; t++) {
//        delayFuncValue = delayFunc(t, curVlId);
//        if(delayFuncValue > delayFuncMax2) {
//            delayFuncMax2 = delayFuncValue;
//        }
//    }
//    assert(delayFuncMax == delayFuncMax2);
//    printf("--------------------------------DELAYFUNC MAX CALC COMPARE: %ld <= %ld\n", delayFuncMax, delayFuncMax2);
//    printf("delayFunc max = %ld\n", delayFuncMax);
    // /DEBUG

    // calc delayFuncRem in chosen points
    int qMin = numPacketsUp(bp - curVl->smin, curVl->bagB, 0);
    int qMax = numPackets(bp, curVl->bagB, curDelay.jit());
//    printf("qmin=%d, qmax=%d\n", qMin, qMax); // DEBUG
    for(int q = qMin; q <= qMax; q++) {
        delayFuncValue = delayFuncRem(q, curVl);
//        printf("delayFuncRem(%d) = %ld\n", q, delayFuncValue); // DEBUG
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }
    assert(delayFuncMax >= 0);
    int64_t dmax = delayFuncMax + curDelay.dmax();
    int64_t dmin = curDelay.dmin() + curVl->smin;
    assert(dmax >= dmin);
    setDelay(DelayData(curVl, dmin, dmax-dmin));
    return Error::Success;
}

template<bool cells>
Error OqPacket<cells>::calcFirst(Vlink* vl) {
    int64_t dmin = vl->smin;
    int64_t jit = vl->jit0b + vl->smax - vl->smin;
    setDelay(DelayData(vl, dmin, jit));
    return Error::Success;
}

template<bool cells>
DelayData OqPacket<cells>::e2e(int vlId) const {
    return getDelay(vlId);
}

template<class Scheme1, class Scheme2>
Error TwoSchemes<Scheme1, Scheme2>::calcCommon(Vlink* curVl) {
    if(!midDelaysReady) {
        scheme1.setInDelays(inDelays);
        for(auto [vlId, delay]: inDelays) {
            Error err = scheme1.calc(delay.vl());
            if(err) {
                return err;
            }
        }
        scheme2.setInDelays(scheme1.getDelays());
//        printf("counted middelays\n");
        midDelaysReady = true;
    }
    Error err = scheme2.calc(curVl);
    if(err) {
        return err;
    }
    setDelay(completeDelay(scheme2.getDelay(curVl->id)));
    return Error::Success;
}

#endif //DELAYTOOL_DELAY_H
