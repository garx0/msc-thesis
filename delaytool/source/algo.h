#pragma once
#include <iostream>
#include <cstdio>
#include <memory>
#include <vector>
#include <map>
#include <cassert>
#include <sstream>

class Vlink;
class Vnode;
class Device;
class PortDelays;
class PortDelaysFactory;
class Port;
class VlinkConfig;

using VlinkSp = std::unique_ptr<Vlink>;
using VnodeSp = std::unique_ptr<Vnode>;
using DeviceSp = std::unique_ptr<Device>;
using PortDelaysSp = std::unique_ptr<PortDelays>;
using PortSp = std::unique_ptr<Port>;
using VlinkConfigSp = std::unique_ptr<VlinkConfig>;
using PortDelaysFactorySp = std::unique_ptr<PortDelaysFactory>;

enum class Error {Success, Cycle, BadForVoq};

class VlinkConfig
{
public:
    // TODO ?
    VlinkConfig(): factory(std::make_unique<PortDelaysFactory>()) {}

    int chainMaxSize;
    double linkRate; // R
    std::string schemeType;
    double cellSize; // sigma
    int voqL; // for schemeType == "VoqA", "VoqB"
    std::map<int, VlinkSp> vlinks;
    std::map<int, DeviceSp> devices;
    std::map<int, std::pair<int, int>> _portCoords; // get input/output port's device ID and local idx
                                                    // by global key of corresponding duplex-port
    std::map<int, int> links;
    PortDelaysFactorySp factory;

    Vlink* getVlink(int id) {
        auto found = vlinks.find(id);
        assert(found != vlinks.end());
        return found->second.get();
    }

    Device* getDevice(int id) {
        auto found = devices.find(id);
        assert(found != devices.end());
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


    Error calcE2e();
};

class Vlink
{
public:
    Vlink(VlinkConfig* config, int id, std::vector<std::vector<int>> paths,
          int bag, int smax, int smin, double jit0);

    VlinkConfig* const config;
    const int id;
    VnodeSp src;
    std::vector<Vnode*> dst;
    int bag;
    int smax;
    int smin;
    double jit0;
};

class Device
{
public:
    enum Type {Src, Switch, Dst};

    Device(VlinkConfig* config, Type type, int id, const std::vector<int>& portNums);

    VlinkConfig* const config;
    const int id;
    const Type type;

    // input ports of switch / no input ports in ES-src / one input port in ES-dst
    std::vector<PortSp> ports;

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
    PortDelaysSp delays; // delays until queuing in switch which contains this input port

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
    PortDelays(Port* port): config(port->device->config), port(port), _ready(false) {}

    VlinkConfig* const config;
    Port* const port;

    void setInDelays(const std::map<int, DelayData>& delays) {
        inDelays = delays;
        _ready = true;
    }

    bool ready() { return _ready; }

    DelayData getDelay(int vl) const { return getFromMap(vl, outDelays); }

    virtual Error calcCommon(int vl) = 0;
    virtual Error calcFirst(int vl) = 0;

    // outDelay[vl] must have been calculated prior to call of this method
    virtual DelayData e2eCommon(int vl) const = 0;

protected:
    std::map<int, DelayData> outDelays;
    std::map<int, DelayData> inDelays;
    bool _ready;

    static DelayData getFromMap(int vl, const std::map<int, DelayData> delays) {
        auto found = delays.find(vl);
        if(found != delays.end()) {
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
    std::vector<VnodeSp> next;
    Vnode* const prev; // (also == vnode of same Vlink from prev device's ports, which is unambiguous)
    Device* const device; // == in->device

    int outPrev; // == in->outPrev - idx of out port of prev device

    // e2e-delay, used only if this->device->type == Device::Dst
    DelayData e2e;

    Error calcE2e() {
        Error err = prepareCalc(1);
        if(err == Error::Success) {
            e2e = in->delays->e2eCommon(vl->id);
        }
        return err;
    }

    Vnode* selectNext(int deviceId) {
        for(auto &sp: next) {
            if(sp->device->id == deviceId) {
                return sp.get();
            }
        }
        return nullptr;
    }

private:
    // prepare input delay data for calculation of delay of this vnode AND calculate this delay
    Error prepareCalc(int chainSize) const {
        if(chainSize > config->chainMaxSize) {
            return Error::Cycle;
        }

        if(device->type == Device::Src) {
            ;
        } else if(prev->device->type == Device::Src) {
            in->delays->calcFirst(vl->id);
        } else {
            Error err;
            std::map<int, DelayData> requiredDelays;
            auto fromOutPort = device->fromOutPort(outPrev);
            for(const auto& vnode: fromOutPort) {
                err = vnode->prev->prepareCalc(++chainSize);
                if(err != Error::Success) {
                    return err;
                }
                int vlId = vnode->vl->id;
                requiredDelays[vlId] = vnode->in->delays->getDelay(vlId);
                assert(vnode->in->delays->getDelay(vlId).ready());
            }
            in->delays->setInDelays(requiredDelays);
            err = in->delays->calcCommon(vl->id);
            if(err != Error::Success) {
                return err;
            }
        }
        return Error::Success;
    }
};

class OqPacket : public PortDelays
{
public:
    OqPacket(Port* port, bool byTick = false)
            : PortDelays(port), bp(-1.), bpReady(false), byTick(byTick) {}

protected:
    virtual Error calcCommon(int vl) override {
        // TODO
        return Error::Success;
    }

    virtual Error calcFirst(int vl) override {
        // TODO
        return Error::Success;
    }

    virtual DelayData e2eCommon(int vl) const override {
        // TODO
        return DelayData();
    }

    double bp;
    bool bpReady;
    bool byTick; // if true, Ck* are used instead of Ck
};

class VoqA : public PortDelays
{
public:
    VoqA(Port* port): PortDelays(port) {}
protected:
    virtual Error calcCommon(int vl) override {
        // TODO
        return Error::Success;
    }

    virtual Error calcFirst(int vl) override {
        // TODO
        return Error::Success;
    }

    virtual DelayData e2eCommon(int vl) const override {
        // TODO
        return DelayData();
    }
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
        virtual PortDelaysSp Create(Port*) const = 0;
        virtual PortDelaysSp Create(Port*, bool) const = 0;
    };

    // class that create TPortDelays objects with its constructor arguments
    template<typename TPortDelays>
    class TCreator : public ICreator {
    public:
        PortDelaysSp Create(Port*) const override;
        PortDelaysSp Create(Port*, bool) const override;
    };

    // register TCreator<TPortDelays> at specified name
    template<typename TPortDelays>
    void AddCreator(const std::string& name);

    // calls corresponding Create method of Creator registered at specified name

    PortDelaysSp Create(const std::string& name, Port*);
    PortDelaysSp Create(const std::string& name, Port*, bool);

private:
    using TCreatorPtr = std::shared_ptr<ICreator>;
    std::map<std::string, TCreatorPtr> creators;
};

template<typename TPortDelays>
PortDelaysSp PortDelaysFactory::TCreator<TPortDelays>::Create(Port* port) const {
    if constexpr (std::is_constructible_v<TPortDelays, decltype(port)>) {
        return std::make_unique<TPortDelays>(port);
    } else {
        throw std::logic_error("can't make PortDelays with these argument types");
    }
}

template<typename TPortDelays>
PortDelaysSp PortDelaysFactory::TCreator<TPortDelays>::Create(Port* port, bool flag) const {
    if constexpr (std::is_constructible_v<TPortDelays, decltype(port), decltype(flag)>) {
        return std::make_unique<TPortDelays>(port, flag);
    } else {
        throw std::logic_error("can't make PortDelays with these argument types");
    }
}

template<typename TPortDelays>
void PortDelaysFactory::AddCreator(const std::string& name) {
    // TODO тут выдаёт ошибку что не кастится к std::shared_ptr<ICreator> !
    creators[name] = std::make_shared<PortDelaysFactory::TCreator<TPortDelays>>();
}

#ifndef DELAYTOOL_ALGO_H
#define DELAYTOOL_ALGO_H

#endif //DELAYTOOL_ALGO_H
