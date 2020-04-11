#pragma once
#ifndef DELAYTOOL_ALGO_H
#define DELAYTOOL_ALGO_H

#include <iostream>
#include <cstdio>
#include <memory>
#include <vector>
#include <map>
#include <cassert>
#include <set>
#include "algo.h"

class Vlink;
class Vnode;
class Device;
class PortDelays;
class PortDelaysFactory;
class Port;
class VlinkConfig;

using VlinkOwn = std::unique_ptr<Vlink>;
using VnodeOwn = std::unique_ptr<Vnode>;
using DeviceOwn = std::unique_ptr<Device>;
using PortDelaysOwn = std::unique_ptr<PortDelays>;
using PortOwn = std::unique_ptr<Port>;
using VlinkConfigOwn = std::unique_ptr<VlinkConfig>;
using PortDelaysFactoryOwn = std::unique_ptr<PortDelaysFactory>;

// returns shuffled {0, ..., size - 1} vector if shuffle=true, not shuffled otherwise
// uses C random tools
// shuffle used only for DeletePaths tool, not for delay calculation
std::vector<int> idxRange(int size, bool shuffle);

class Error {
public:
    enum ErrorType {Success, Cycle, BadForVoq, BpDiverge};

    Error(ErrorType type = Success, const std::string& verbose = "", const std::string& verboseRaw = "")
        : type(type), verbose(verbose), verboseRaw(verboseRaw) {}

    Error(Error& other) = default;

    Error(Error&& other) = default;

    Error& operator=(Error& other) = default;

    Error& operator=(Error&& other) = default;

    const std::string& Verbose() const {
        return verbose;
    }

    const std::string& VerboseRaw() const {
        return verboseRaw;
    }

    std::string TypeString() const {
        switch(type) {
            case Success:
                return "Success";
            case Cycle:
                return "Cycle";
            case BadForVoq:
                return "BadForVoq";
            case BpDiverge:
                return "BpDiverge";
        }
        return "";
    }

    operator bool() const {
        return type != Success;
    }

    bool operator==(Error& other) const {
        return type == other.type;
    }

    bool operator!=(Error& other) const {
        return type != other.type;
    }

    bool operator==(ErrorType other) const {
        return type == other;
    }

    bool operator!=(ErrorType other) const {
        return type != other;
    }

    friend bool operator==(ErrorType lhs, const Error& rhs);
    friend bool operator!=(ErrorType lhs, const Error& rhs);

private:
    ErrorType type;
    std::string verbose;
    std::string verboseRaw;
};

bool operator==(Error::ErrorType lhs, const Error& rhs);

bool operator!=(Error::ErrorType lhs, const Error& rhs);

class VlinkConfig
{
public:
    VlinkConfig(): factory(std::make_unique<PortDelaysFactory>()) {}

    int64_t linkRate; // in byte/ms
    std::string scheme;
    int cellSize; // sigma, bytes
    int voqL; // for scheme == "VoqA", "VoqB"
    std::map<int, VlinkOwn> vlinks;
    std::map<int, DeviceOwn> devices;
    std::map<int, int> _portDevice; // get device ID by input/output port ID
    std::map<int, int> links;
    PortDelaysFactoryOwn factory;

    Vlink* getVlink(int id) const;

    Device* getDevice(int id) const;

    int connectedPort(int portId) const;

    int portDevice(int portId) const;

    std::vector<Vlink*> getAllVlinks() const;

    std::vector<Device*> getAllDevices() const;

    // random order of vlinks and destinations processing if shuffle=true
    // prints delays if print=true
    Error calcE2e(bool print = false);

    Error detectCycles(bool shuffle = false);

    double linkByte2ms(int64_t linkByte) { return static_cast<double>(linkByte) / linkRate; }
};

class Vlink
{
public:
    Vlink(VlinkConfig* config, int id, std::vector<std::vector<int>> paths,
          int bag, int smax, int smin, double jit0);

    VlinkConfig* const config;
    const int id;
    VnodeOwn src; // tree root
    std::map<int, Vnode*> dst; // tree leaves, key is device id
    int bag; // in ms
    int64_t bagB; // in link-bytes, == bag * config->linkRate)
    int smax; // in bytes
    int smin; // in bytes
    double jit0; // jitter of start of packet transfer from source end system, in ms
    int64_t jit0b; // in link-bytes, == ceil(jit0 * config->linkRate)
};

class Device
{
public:
    enum Type {Switch, End};

    Device(VlinkConfig* config, Type type, int id)
        : config(config), id(id), type(type) {}

    // called when config->_portDevices is complete
    void AddPorts(const std::vector<int>& portIds);

    VlinkConfig* const config;
    const int id;
    const Type type;


    std::map<int, PortOwn> ports; // input ports
    std::vector<Vlink*> sourceFor; // Vlinks which have this device as source

    Port* getPort(int portId) const;

    std::vector<Port*> getAllPorts() const;

    // get vnodes coming from output port by port id
    // i.e. vnodes in which prev->device == this and in->outPrev == portId
    std::vector<Vnode*> fromOutPort(int portId) const;
};

// INPUT PORT
class Port
{
public:
    // there's a link connecting output port outPrev of prevDevice with input port id (this) of device
    int id;
    int outPrev; // id of output port with which this input port is connected by link
    Device* const device;
    Device* prevDevice;
    std::map<int, Vnode*> vnodes; // get Vnode by Vlink id
    PortDelaysOwn delays; // delays until queuing in switch which contains this input port

    Port(Device* device, int number);

    Vnode* getVnode(int vlId) const;

    std::vector<Vnode*> getAllVnodes() const;
};

// time is measured in bytes through link = (time in ms) * (link rate in byte/ms) / (1 byte)
class DelayData
{
public:
    DelayData(): _dmin(-2), _jit(-1), _dmax(-1), _ready(false) {}
    DelayData(int64_t dmin, int64_t jit): _dmin(dmin), _jit(jit), _dmax(dmin + jit), _ready(true) {}

    bool ready() const { return _ready; }

    int64_t dmin() const { return _ready ? _dmin : -1; }

    int64_t jit() const { return _ready ? _jit : -1; }

    int64_t dmax() const { return _ready ? _dmax : -1; }

private:
    int64_t _dmin;
    int64_t _jit;
    int64_t _dmax;
    bool _ready;
};

class PortDelays
{
public:
    explicit PortDelays(Port* port, const std::string& type)
    : config(port->device->config), port(port), type(type), _ready(false) {}

    VlinkConfig* const config;
    Port* const port;
    std::string type;

    void setInDelays(const std::map<int, DelayData>& values);

    bool ready() { return _ready; }

    DelayData getDelay(int vl) const { return getFromMap(vl, delays); }

    DelayData getInDelay(int vl) const { return getFromMap(vl, inDelays); }

    const std::map<int, DelayData>& getDelays() const { return delays; }

    const std::map<int, DelayData>& getInDelays() const { return inDelays; }

    Error calc(int vl, bool first = false);

    // outDelay[vl] must have been calculated prior to call of this method
    virtual DelayData e2e(int vl) const = 0;

    friend Error calcFirstA(PortDelays* scheme, int vlId);
    friend Error calcFirstB(PortDelays* scheme, int vlId);
    friend DelayData e2eA(PortDelays* scheme, int vlId);
    friend DelayData e2eB(PortDelays* scheme, int vlId);

    virtual Error calcCommon(int vl) = 0;
    virtual Error calcFirst(int vl) = 0;

protected:
    std::map<int, DelayData> delays;
    std::map<int, DelayData> inDelays;
    bool _ready;

    static DelayData getFromMap(int vl, const std::map<int, DelayData>& delaysMap);
};

class Vnode
{
public:
    Vnode(Vlink* vlink, int deviceId, Vnode* prev);

    VlinkConfig* const config;
    Vlink* const vl;
    Port* in; // in port of this device
    std::vector<VnodeOwn> next;
    Vnode* const prev; // (also == vnode of same Vlink from prev device's ports, which is unambiguous)
    Device* const device; // == in->device
    int outPrev; // == in->outPrev - id of out port of prev device
    enum {NotVisited, VisitedNotPrepared, Prepared} cycleState; // for cycle detecting
    bool calcPrepared;

    // e2e-delay, used only if this->device->type == Device::Dst
    DelayData e2e;

    // random order of data dependency graph traversal if shuffle=true
    Error calcE2e();

    Error prepareTest(bool shuffle = false);

    Vnode* selectNext(int deviceId) const;

    // get ids of all devices which are leafs of this node's subtree
    std::vector<const Vnode*> getAllDests() const;

private:
    // prepare input delay data for calculation of delay of this vnode AND calculate this delay
    // random order of data dependency graph traversal if shuffle=true
    Error prepareCalc(std::string debugPrefix = ""); // DEBUG in signature

    // recursive helper function for getAllDests()
    void _getAllDests(std::vector<const Vnode*>& vec) const;
};

class Mock : public PortDelays
{
public:
    Mock(Port* port): PortDelays(port, "Mock") {}

    DelayData e2e(int vl) const override;

    Error calcCommon(int vl) override;

    Error calcFirst(int vl) override;
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

    static Error completeCheck(Device *device);

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

    Error calcCommon(int vl) override;

    Error calcFirst(int vl) override;
};

class VoqB : public Voq
{
public:
    explicit VoqB(Port* port): Voq(port, "VoqB") { }

    DelayData e2e(int vl) const override;

    void calcVlinkLoad() override;

    Error calcCommon(int vl) override;

    Error calcFirst(int vl) override;
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

    // == Rk,j(t) - Jk, k == curVlId
    int64_t delayFunc(int64_t t, int curVlId) const;

    // == Rk,j(q)* - Jk, k == curVlId
    int64_t delayFuncRem(int q, int curVlId);

    Error calcCommon(int vl) override;

    Error calcFirst(int vl) override;

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

    // == Rk,j(t) - Jk, k == curVlId
    int64_t delayFunc(int64_t t, int curVlId) const;

    // == Rk,j(q)* - Jk, k == curVlId
    int64_t delayFuncRem(int q, int curVlId);

    Error calcCommon(int vl) override;

    Error calcFirst(int vl) override;

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

    Error calcCommon(int vl) override;

    Error calcFirst(int vl) override;

    // make out delay from scheme2 out delay
    DelayData completeDelay(DelayData delay, int vl) const;

private:
    bool midDelaysReady;
    Scheme1 scheme1;
    Scheme2 scheme2;
};

using OqA = TwoSchemes<OqPacket<true>, OqPacket<>>;
using OqB = TwoSchemes<OqCellB, OqPacket<>>;

class PortDelaysFactory {
public:

    PortDelaysFactory() { RegisterAll(); }

    // register Creators for all PortDelays types
    void RegisterAll();

    // interface for TCreator
    // is to unite TCreator objects creating different PortDelays under one type
    class ICreator {
    public:
        ICreator() = default;
        virtual ~ICreator() = default;
        virtual PortDelaysOwn Create(Port*) const = 0;
    };

    // class that create TPortDelays objects with its constructor arguments
    template<typename TPortDelays>
    class TCreator : public ICreator {
    public:
        PortDelaysOwn Create(Port* port) const override {
            if constexpr(std::is_constructible_v<TPortDelays, decltype(port)>) {
                return std::make_unique<TPortDelays>(port);
            } else {
                throw std::logic_error("can't make PortDelays with these argument types");
            }
        }
    };

    // register TCreator<TPortDelays> at specified name
    template<typename TPortDelays>
    void AddCreator(const std::string& name) {
        creators[name] = std::make_shared<PortDelaysFactory::TCreator<TPortDelays>>();
    }

    // calls corresponding Create method of Creator registered at specified name
    PortDelaysOwn Create(const std::string& name, Port*);

private:
    using TCreatorPtr = std::shared_ptr<ICreator>;
    std::map<std::string, TCreatorPtr> creators;
};

#endif //DELAYTOOL_ALGO_H