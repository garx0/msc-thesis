#include <iostream>
#include <memory>
#include <vector>
#include <map>
#include <cassert>

class Vlink;
class Vnode;
class Device;
class PortDelays;
class Port;
class VlinkConfig;

using VlinkSp = std::unique_ptr<Vlink>;
using VnodeSp = std::unique_ptr<Vnode>;
using DeviceSp = std::unique_ptr<Device>;
using PortDelaysSp = std::unique_ptr<PortDelays>;
using PortSp = std::unique_ptr<Port>;
using VlinkConfigSp = std::unique_ptr<VlinkConfig>;

enum class Error {Success, Cycle, BadForVoq};

class VlinkConfig
{
public:
    int chainMaxSize;
    double linkRate; // R
    double tick; // tau
    std::map<int, VlinkSp> vlinks;
    std::map<int, DeviceSp> sources;
    std::map<int, DeviceSp> switches;
    std::map<int, DeviceSp> dests;
    std::map<int, std::pair<int, int>> inPortCoords; // get input port's device ID and local idx
                                                     // by global key of corresponding duplex-port
    std::map<int, std::pair<int, int>> outPortCoords; // get output port's device ID and local idx
                                                      // by global key of corresponding duplex-port
};

class Vlink
{
public:
    const int id;
    const VnodeSp src;
    std::vector<Vnode*> dst;
    double bag;
    double smax;
    double smin;
    double jit0;
    const VlinkConfig* config;
};

class Device
{
public:
    enum Type {Src, Switch, Dst};

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
    const int idx;
    const int outPrev; // idx of output port with which this input port is connected by link
    const Device* device;
    const Device* prevDevice;
    std::map<int, Vnode*> vnodes; // get Vnode by Vlink id
    PortDelaysSp delays; // delays until queuing in switch which contains this input port
};

class DelayData
{
public:
    DelayData(): _ready(false), _dmin(-2.), _dmax(-1.), _jit(-1.) {}
    DelayData(double dmin, double jit): _ready(true), _dmin(dmin), _jit(jit), _dmax(dmin + jit) {}

//    void set(double dmin, double jit) {
//        _dmin = dmin;
//        _jit = jit;
//        _ready = true;
//    }

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
    const Port* port;

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

private:
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
    int id;
    const Vlink* vl;
    const Port* in; // in port of this device
    std::vector<VnodeSp> next;
    const Vnode* prev; // (also == vnode of same Vlink from prev device's ports, which is unambiguous)
    const Device* device; // == in->device
    int outPrev; // == in->outPrev - idx of out port of prev device

    // e2e-delay, used only if this->device->type == Device::Dst
    DelayData e2e;

    const VlinkConfig* config;

private:

    // prepare input delay data for calculation of delay of this vnode AND calculate this delay
    Error prepareCalc(int chainSize) const {
        if(chainSize > config->chainMaxSize) {
            return Error::Cycle;
        };

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

    Error calcE2e() {
        Error err = prepareCalc(1);
        if(err == Error::Success) {
            e2e = in->delays->e2eCommon(vl->id);
        }
        return err;
    }
};

std::vector<Vnode*> Device::fromOutPort(int idx) const {
    std::vector<Vnode*> ans;
    for(const auto& port: ports) {
        for(auto pair: port->vnodes) {
            auto vnode = pair.second;
            for(const auto& next: vnode->next) {
                if(next->outPrev == idx) {
                    ans.push_back(next.get());
                    break;
                }
            }
        }
    }
    return ans;
}

class OqPacket : PortDelays
{
private:
    double bp;
    bool bpReady;
    bool byTick; // if true, Ck* are used instead of Ck

};

class VoqA : PortDelays
{
private:

};

class VoqB : PortDelays
{
private:

};

int main() {
    return 0;
}
