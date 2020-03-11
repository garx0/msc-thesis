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

using VlinkPtr = std::shared_ptr<Vlink>;
using VnodePtr = std::shared_ptr<Vnode>;
using DevicePtr = std::shared_ptr<Device>;
using PortDelaysPtr = std::shared_ptr<PortDelays>;
using PortPtr = std::shared_ptr<Port>;
using VlinkConfigPtr = std::shared_ptr<VlinkConfig>;

enum class Error {Success, Cycle, BadForVoq};

// TODO здесь скорее всего возникнет циклич. зависимость по shared_ptr, разобраться с ней
//  если, напр, комм-р содержит порты, то это точно shared_ptr
//  если вк-нод ссылается на порт, то мб weak
//  и тд по такой логике

class VlinkConfig : std::enable_shared_from_this<VlinkConfig>
{
public:
    int chainMaxSize;
    double linkRate; // R
    double tick; // tau
};

class Vlink : std::enable_shared_from_this<Vlink>
{
public:
    int id;
    VnodePtr src;
    std::vector<VnodePtr> dst;
    double bag;
    double smax;
    double smin;
    double jit0;
    VlinkConfigPtr config;
};

class Device : std::enable_shared_from_this<Device>
{
public:
    enum Type {Src, Switch, Dst};

    int id;
    Type type;

    // input ports of switch / no input ports in ES-src / one input port in ES-dst
    std::vector<PortPtr> ports;
    // get in port idx by port numeration from input data
//    std::map<int, int> inPortIdx;
    // get out port idx by port numeration from input data
//    std::map<int, int> outPortIdx;

    // vnodes coming from output port [idx]
    // i.e. vnodes in which prev->device == this and in-outPort == idx
    std::vector<VnodePtr> fromOutPort(int idx);
};

// INPUT PORT
class Port : std::enable_shared_from_this<Port>
{
public:
    int idx;
    int outPrev; // idx of output port with which this input port is connected by link
    DevicePtr device;
    std::map<int, VnodePtr> vnodes; // get Vnode by Vlink id
    PortDelaysPtr delays; // delays until queuing in switch which contains this input port
};

class DelayData {
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

class PortDelays : std::enable_shared_from_this<PortDelays>
{
public:
    PortPtr port;

    void setInDelays(const std::map<int, DelayData>& delays) {
        inDelays = delays;
        _ready = true;
    }

    bool ready() { return _ready; }

    DelayData getDelay(int vl) { return getFromMap(vl, outDelays); }

    virtual Error calcCommon(int vl) = 0;
    virtual Error calcFirst(int vl) = 0;

    // outDelay[vl] must have been calculated prior to call of this method
    virtual DelayData e2eCommon(int vl) = 0;

private:
    std::map<int, DelayData> outDelays;
    std::map<int, DelayData> inDelays;
    bool _ready;

    DelayData getFromMap(int vl, const std::map<int, DelayData> delays) {
        auto found = delays.find(vl);
        if(found != delays.end()) {
            return found->second;
        } else {
            std::cout << "no such vl in this port\n"; // DEBUG
            return DelayData();
        }
    }

    DelayData getInDelay(int vl) { return getFromMap(vl, inDelays); }
};

class Vnode : std::enable_shared_from_this<Vnode>
{
public:
    int id;
    VlinkPtr vl;
    DevicePtr device;
    VnodePtr prev;
    std::vector<VnodePtr> next;
    PortPtr in; // in port of this device
    int outPrev; // idx of out port of prev device
    // Vnodes whose delays are required to calculate delay before this vnode
    // (i.e. vnodes which have this->outPrev among their destination out ports)
    std::vector<VnodePtr> requiredVnodes;

    // these two are < 0 if device->type != Device::Dst
    double e2eMaxDelay;
    double e2eJit;

    VlinkConfigPtr config;

private:

    // to be called in constructor
    void requiredVnodesInit() {
        if(device->type == Device::Src || prev->device->type == Device::Src) {
            return;
        }
        for(const auto& port: prev->device->ports) {
            for(const auto& pair: port->vnodes) {
                auto vnode = pair.second;
                for(const auto& next: vnode->next) {
                    if(next->outPrev == outPrev) {
                        requiredVnodes.push_back(vnode);
                        break;
                    }
                }
            }
        }
    }

    // prepare input delay data for calculation of delay before this vnode and calculate this delay
    Error prepareCalc(int chainSize) {
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
            for(const auto& vnode: requiredVnodes) {
                err = vnode->prepareCalc(++chainSize);
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
            DelayData e2eData = in->delays->e2eCommon(vl->id);
            e2eMaxDelay = e2eData.dmax();
            e2eJit = e2eData.jit();
        }
        return err;
    }
};

std::vector<VnodePtr> Device::fromOutPort(int idx) {
    std::vector<VnodePtr> ans;
    for(const auto& port: ports) {
        for(const auto& pair: port->vnodes) {
            auto vnode = pair.second;
            for(const auto& next: vnode->next) {
                if(next->outPrev == idx) {
                    ans.push_back(next);
                    break;
                }
            }
        }
    }
    return ans;
}

class OqPacket : PortDelays {
private:
    double bp;
    bool bpReady;
    bool byTick; // if true, Ck* are used instead of Ck

};

class VoqA : PortDelays {
private:

};

class VoqB : PortDelays {
private:

};

int main() {
    return 0;
}
