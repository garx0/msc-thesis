#pragma once
#ifndef DELAYTOOL_ALGO_H
#define DELAYTOOL_ALGO_H

#include <iostream>
#include <cstdio>
#include <memory>
#include <vector>
#include <map>
#include <cassert>

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

enum class Error {Success, Cycle, BadForVoq};

class VlinkConfig
{
public:
    VlinkConfig(): factory(std::make_unique<PortDelaysFactory>()) {}

    int chainMaxSize;
    double linkRate; // R
    std::string scheme;
    int cellSize; // sigma
    int voqL; // for scheme == "VoqA", "VoqB"
    std::map<int, VlinkOwn> vlinks;
    std::map<int, DeviceOwn> devices; // actually, switches and dests
    std::map<int, DeviceOwn> sources;
    std::map<int, std::pair<int, int>> _portCoords; // get input/output port's device ID and local idx
                                                    // by global key of corresponding duplex-port
    std::map<int, int> links;
    PortDelaysFactoryOwn factory;

    Vlink* getVlink(int id) {
        auto found = vlinks.find(id);
        assert(found != vlinks.end());
        return found->second.get();
    }

    Device* getDevice(int id, bool isSource = false) {
        std::map<int, DeviceOwn>& curMap = isSource ? sources : devices;
        auto found = curMap.find(id);
        assert(found != curMap.end());
        return found->second.get();
    }

    int connectedPort(int portNum) {
        auto found = links.find(portNum);
        assert(found != links.end());
        return found->second;
    }

    std::pair<int, int> portCoords(int portNum) {
        auto found = _portCoords.find(portNum);
        assert(found != _portCoords.end());
        return found->second;
    }

    void calcChainMaxSize();


    Error calcE2e();
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
    int bag;
    int smax;
    int smin;
    double jit0;
};

class Device
{
public:
    enum Type {Src, Switch, Dst};

    Device(VlinkConfig* config, Type type, int id)
        : config(config), id(id), type(type) {}

    // called when config->_portCoords is complete
    void AddPorts(const std::vector<int>& portNums);

    VlinkConfig* const config;
    const int id;
    const Type type;

    // input ports of switch / no input ports in ES-src / one input port in ES-dst
    std::vector<PortOwn> ports;

    // get vnodes coming from output port [idx]
    // i.e. vnodes in which prev->device == this and in-outPort == idx
    std::vector<Vnode*> fromOutPort(int idx) const;
};

// INPUT PORT
class Port
{
public:
    // there's a link connecting output port outPrev of prevDevice with input port idx (this) of device
    int idx;
    int outPrev; // idx of output port with which this input port is connected by link
    Device* const device;
    Device* prevDevice;
    std::map<int, Vnode*> vnodes; // get Vnode by Vlink id
    PortDelaysOwn delays; // delays until queuing in switch which contains this input port

    Port(Device* device, int number);
};

class DelayData
{
public:
    DelayData(): _dmin(-2.), _jit(-1.), _dmax(-1.), _ready(false) {}
    DelayData(double dmin, double jit): _dmin(dmin), _jit(jit), _dmax(dmin + jit), _ready(true) {}

    bool ready() const { return _ready; }

    double dmin() const { return _ready ? _dmin : -1.; }

    double jit() const { return _ready ? _jit : -1.; }

    double dmax() const { return _ready ? _dmax : -1.; }

private:
    double _dmin;
    double _jit;
    double _dmax;
    bool _ready;
};

class PortDelays
{
public:
    explicit PortDelays(Port* port): config(port->device->config), port(port), _ready(false) {}

    VlinkConfig* const config;
    Port* const port;

    void setInDelays(const std::map<int, DelayData>& delays) {
        inDelays = delays;
        _ready = true;
    }

    bool ready() { return _ready; }

    DelayData getDelay(int vl) const { return getFromMap(vl, delays); }

    virtual Error calcCommon(int vl) = 0;
    virtual Error calcFirst(int vl) = 0;

    // outDelay[vl] must have been calculated prior to call of this method
    virtual DelayData e2e(int vl) const = 0;

protected:
    std::map<int, DelayData> delays;
    std::map<int, DelayData> inDelays;
    bool _ready;

    static DelayData getFromMap(int vl, const std::map<int, DelayData>& delaysMap) {
        auto found = delaysMap.find(vl);
        if(found != delaysMap.end()) {
            return found->second;
        } else {
            std::cout << "no such vl in this port\n"; // DEBUG
            return DelayData();
        }
    }

    DelayData getInDelay(int vl) const { return getFromMap(vl, inDelays); }
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

    int outPrev; // == in->outPrev - idx of out port of prev device

    // e2e-delay, used only if this->device->type == Device::Dst
    DelayData e2e;

    Error calcE2e() {
        Error err = prepareCalc(1);
        if(err == Error::Success) {
            e2e = in->delays->e2e(vl->id);
        }
        return err;
    }

    Vnode* selectNext(int deviceId) {
        for(auto &own: next) {
            if(own->device->id == deviceId) {
                return own.get();
            }
        }
        return nullptr;
    }

private:
    // prepare input delay data for calculation of delay of this vnode AND calculate this delay
    Error prepareCalc(int chainSize) const;
};

class Mock : public PortDelays
{
public:
    Mock(Port* port): PortDelays(port) {}

protected:
    Error calcCommon(int vl) override {
        // TODO
        double dmin = 0, dmax = 0;
        for(auto [id, delay]: inDelays) {
            dmin += delay.dmin();
            dmax += delay.dmax();
        }
        dmin += 1;
        dmax += 2;
        delays[vl] = DelayData(dmin, dmax-dmin);
        return Error::Success;
    }

    Error calcFirst(int vl) override {
        // TODO
        delays[vl] = DelayData(0, config->getVlink(vl)->jit0);
        return Error::Success;
    }

    DelayData e2e(int vl) const override {
        // TODO
        return getDelay(vl);
    }
};

class VoqA : public PortDelays
{
public:
    VoqA(Port* port): PortDelays(port) {}

protected:
    Error calcCommon(int vl) override {
        // TODO
        return Error::Success;
    }

    Error calcFirst(int vl) override {
        // TODO
        return Error::Success;
    }

    DelayData e2e(int vl) const override {
        // TODO
        return DelayData();
    }
};

class OqPacket : public PortDelays
{
public:
    explicit OqPacket(Port* port, bool byTick = false)
            : PortDelays(port), bp(-1.), bpReady(false), byTick(byTick) {}

protected:
    Error calcCommon(int vl) override {
        // TODO
        return Error::Success;
    }

    Error calcFirst(int vl) override {
        // TODO
        return Error::Success;
    }

    DelayData e2e(int vl) const override {
        // TODO
        return DelayData();
    }

    double bp;
    bool bpReady;
    bool byTick; // if true, Ck* are used instead of Ck
};

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
        virtual PortDelaysOwn Create(Port*, bool) const = 0;
    };

    // class that create TPortDelays objects with its constructor arguments
    template<typename TPortDelays>
    class TCreator : public ICreator {
    public:
        PortDelaysOwn Create(Port*) const override;
        PortDelaysOwn Create(Port*, bool) const override;
    };

    // register TCreator<TPortDelays> at specified name
    template<typename TPortDelays>
    void AddCreator(const std::string& name);

    // calls corresponding Create method of Creator registered at specified name

    PortDelaysOwn Create(const std::string& name, Port*);
    PortDelaysOwn Create(const std::string& name, Port*, bool);

private:
    using TCreatorPtr = std::shared_ptr<ICreator>;
    std::map<std::string, TCreatorPtr> creators;
};

template<typename TPortDelays>
PortDelaysOwn PortDelaysFactory::TCreator<TPortDelays>::Create(Port* port) const {
    if constexpr (std::is_constructible_v<TPortDelays, decltype(port)>) {
        return std::make_unique<TPortDelays>(port);
    } else {
        throw std::logic_error("can't make PortDelays with these argument types");
    }
}

template<typename TPortDelays>
PortDelaysOwn PortDelaysFactory::TCreator<TPortDelays>::Create(Port* port, bool flag) const {
    if constexpr (std::is_constructible_v<TPortDelays, decltype(port), decltype(flag)>) {
        return std::make_unique<TPortDelays>(port, flag);
    } else {
        throw std::logic_error("can't make PortDelays with these argument types");
    }
}

template<typename TPortDelays>
void PortDelaysFactory::AddCreator(const std::string& name) {
    creators[name] = std::make_shared<PortDelaysFactory::TCreator<TPortDelays>>();
}

#endif //DELAYTOOL_ALGO_H
