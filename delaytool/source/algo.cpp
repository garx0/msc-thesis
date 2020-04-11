#include <iostream>
#include <cstdio>
#include <vector>
#include <cmath>
#include <random>
#include <chrono>
#include "algo.h"
#include "delay.h"

std::vector<int> idxRange(int size, bool shuffle) {
    std::vector<int> res;
    res.reserve(size);
    for(int i = 0; i < size; i++) {
        res.push_back(i);
    }
    if(shuffle) {
        for(int i = size - 1; i > 0; i--) {
            std::swap(res[i], res[rand() % i]);
        }
    }
    return res;
}

bool operator==(Error::ErrorType lhs, const Error& rhs) {
    return lhs == rhs.type;
}

bool operator!=(Error::ErrorType lhs, const Error& rhs) {
    return lhs != rhs.type;
}

void Device::AddPorts(const std::vector<int>& portIds) {
    for(int i: portIds) {
        // у нас есть только номер порта.
        ports[i] = std::make_unique<Port>(this, i);
    }
}

std::vector<Vnode*> Device::fromOutPort(int portId) const {
    std::vector<Vnode*> ans;
    for(auto port: getAllPorts()) {
        for(auto vnode: port->getAllVnodes()) {
            for(const auto &next: vnode->next) {
                if(next->outPrev == portId) {
                    ans.push_back(next.get());
                    break;
                }
            }
        }
    }
    for(auto vl: sourceFor) {
        assert(vl->src->next.size() == 1);
        ans.push_back(vl->src->next[0].get());
    }
    return ans;
}

Port* Device::getPort(int portId) const {
    auto found = ports.find(portId);
    assert(found != ports.end());
    return found->second.get();
}

std::vector<Port*> Device::getAllPorts() const {
    std::vector<Port*> res;
    res.reserve(ports.size());
    for(const auto& pair: ports) {
        res.push_back(pair.second.get());
    }
    return res;
}

Port::Port(Device* device, int id)
    : id(id), device(device)
{
    // calculate outPrev, prevDevice by number and links and portDevice in device->config
    VlinkConfig* config = device->config;
    outPrev = config->connectedPort(id);
    int prevDeviceId = config->portDevice(outPrev);
    prevDevice = config->getDevice(prevDeviceId);
    delays = config->factory->Create(config->scheme, this);
}

Vnode* Port::getVnode(int vlId) const {
    auto found = vnodes.find(vlId);
    assert(found != vnodes.end());
    return found->second;
}

std::vector<Vnode*> Port::getAllVnodes() const {
    std::vector<Vnode*> res;
    res.reserve(vnodes.size());
    for(const auto& pair: vnodes) {
        res.push_back(pair.second);
    }
    return res;
}

Vlink::Vlink(VlinkConfig* config, int id, std::vector<std::vector<int>> paths,
    int bag, int smax, int smin, double jit0)
    : config(config), id(id), bag(bag), bagB(bag * config->linkRate),
    smax(smax), smin(smin), jit0(jit0), jit0b(std::ceil(jit0 * config->linkRate))
{
    assert(!paths.empty() && paths[0].size() >= 2);
    int srcId = paths[0][0];
    src = std::make_unique<Vnode>(this, srcId, nullptr);
    src->device->sourceFor.push_back(this);

    for(const auto& path: paths) {
        assert(path[0] == srcId);
        size_t i = 0;
        Vnode* vnodeNext = src.get();
        Vnode* vnode = vnodeNext; // unnecessary if src != nullptr
        while(vnodeNext != nullptr) {
            assert(i < path.size() - 1);
            vnode = vnodeNext;
            vnodeNext = vnode->selectNext(path[++i]);
        }
        // vnode is initialized if make_unique didnt returned nullptr
        // now we need to add a new-made Vnode of device path[i] to vnode->next vector
        for(; i < path.size(); i++) {
            vnode->next.push_back(std::make_unique<Vnode>(this, path[i], vnode));
            vnode = vnode->next[vnode->next.size()-1].get();
        }
        // vnode is a leaf
        dst[vnode->device->id] = vnode;
    }
}

Vnode::Vnode(Vlink* vlink, int deviceId, Vnode* prev)
    : config(vlink->config), vl(vlink),
      prev(prev), device(vlink->config->getDevice(deviceId)), cycleState(NotVisited), calcPrepared(false), e2e()
{
    if(prev != nullptr) {
        // find port by prev->device
        assert(!device->ports.empty());
        for(auto curPort: device->getAllPorts()) {
            assert(curPort->vnodes.find(vl->id) == curPort->vnodes.end()); // else Vlink has cycles by devices
            if(curPort->prevDevice->id == prev->device->id) {
                in = curPort;
                outPrev = in->outPrev;
                in->vnodes[vl->id] = this;
                return;
            }
        }
        assert(false);
    } else {
        in = nullptr;
        outPrev = -1;
    }
}

Error Vnode::prepareTest(bool shuffle) {
    if(cycleState == NotVisited) {
        cycleState = VisitedNotPrepared;
    } else if(cycleState == VisitedNotPrepared) {
        std::string verbose =
                std::string("cycle: entered VL ")
                + std::to_string(vl->id)
                + " device "
                + std::to_string(device->id)
                + " again. suggestion: delete VL "
                + std::to_string(vl->id)
                + " paths to end systems:";
        std::string verboseRaw = std::to_string(vl->id);
        for(auto dest: getAllDests()) {
            verbose += " " + std::to_string(dest->device->id);
            verboseRaw += " " + std::to_string(dest->device->id);
        }
        return Error(Error::Cycle, verbose, verboseRaw);
    }
    assert(prev != nullptr);
    if(prev->prev != nullptr) {
        auto fromOutPort = prev->device->fromOutPort(outPrev);
        auto order = idxRange(fromOutPort.size(), shuffle);
        for(auto idx: order) {
            auto curVnode = fromOutPort[idx]->prev;
            if(curVnode->cycleState != Prepared) {
                Error err = curVnode->prepareTest(shuffle);
                if(err) {
                    return err;
                }
            }
//            printf("%svl %d-%d calling prepareCalc on vl %d-%d\n",
//                   "", vl->id, device->id, curVnode->vl->id, curVnode->device->id); // DEBUG
        }
    }
    cycleState = Prepared;
    return Error::Success;
}

Error Vnode::prepareCalc(std::string debugPrefix) { // DEBUG in signature
    assert(prev != nullptr);
    if(prev->prev == nullptr) {
        in->delays->calc(vl->id, true);
    } else {
        Error err;
        std::map<int, DelayData> requiredDelays;
        for(auto vnodeNext: prev->device->fromOutPort(outPrev)) {
            auto curVnode = vnodeNext->prev;
            if(!curVnode->calcPrepared) {
                err = curVnode->prepareCalc(debugPrefix+"  ");
                if(err) {
                    return err;
                }
            }
//            printf("%svl %d-%d calling prepareCalc on vl %d-%d\n",
//                   debugPrefix.c_str(), vl->id, device->id, curVnode->vl->id, curVnode->device->id); // DEBUG
//            printf("%svl %d-%d calling prepareCalc on vl %d-%d\n",
//                   "", vl->id, device->id, curVnode->vl->id, curVnode->device->id); // DEBUG
            int vlId = curVnode->vl->id;
            auto delay = curVnode->in->delays->getDelay(vlId);
            assert(delay.ready());
            requiredDelays[vlId] = delay;
        }
        in->delays->setInDelays(requiredDelays);
        err = in->delays->calc(vl->id);
        if(err) {
            return err;
        }
            // DEBUG
//        for(auto [vlId, delay]: requiredDelays) {
//            printf("in delays: [vl %d] dmax=%ld dmin=%ld\n", vlId, delay.dmax(), delay.dmin());
//        }
//        auto outDelay = in->delays->getDelay(vl->id);
//        printf("out delay: [vl %d] dmax=%ld dmin=%ld\n", vl->id, outDelay.dmax(), outDelay.dmin());
            // /DEBUG
    }
    calcPrepared = true;
    return Error::Success;
}

Error Vnode::calcE2e() {
    Error err = prepareCalc();
    if(!err) {
        e2e = in->delays->e2e(vl->id);
    }
    return err;
}

Vnode* Vnode::selectNext(int deviceId) const {
    for(const auto &own: next) {
        if(own->device->id == deviceId) {
            return own.get();
        }
    }
    return nullptr;
}

std::vector<const Vnode*> Vnode::getAllDests() const {
    std::vector<const Vnode*> vec;
    _getAllDests(vec);
    return vec;
}

void Vnode::_getAllDests(std::vector<const Vnode*> &vec) const {
    if(next.empty()) {
        assert(device->type == Device::End);
        vec.push_back(this);
        return;
    }
    for(const auto& nxt: next) {
        nxt->_getAllDests(vec);
    }
}

Error VlinkConfig::calcE2e(bool print) {
    for(auto vl: getAllVlinks()) {
        for(auto [_, vnode]: vl->dst) {
            //        printf("\ncalling calcE2e on vlink %d device %d\n", vnode->vl->id, vnode->device->id); // DEBUG
            Error err = vnode->calcE2e();
            if(err) {
                return err;
            }
            if(print) {
                DelayData e2e = vnode->e2e;
                printf("VL %d to %d: maxDelay = %li lB (%.1f us), jit = %li lB (%.1f us)\n",
                       vnode->vl->id, vnode->device->id,
                       e2e.dmax(), linkByte2ms(e2e.dmax()) * 1e3,
                       e2e.jit(), linkByte2ms(e2e.jit()) * 1e3);
            }
        }
    }
    if(scheme == "voqa" || scheme == "voqb") {
        for(auto device: getAllDevices()) {
            // проверить суммы по входным портам коммутатора
            if(device->type != Device::Switch) {
                continue;
            }
            Error err = Voq::completeCheck(device);
            if(err) {
                return err;
            }
        }
    }
    return Error::Success;
}

Error VlinkConfig::detectCycles(bool shuffle) {
    std::vector<Vnode*> dests;
    for(auto vl: getAllVlinks()) {
        for(auto [_, vnode]: vl->dst) {
            dests.push_back(vnode);
        }
    }
    auto order = idxRange(dests.size(), shuffle);
    for(auto idx: order) {
        auto vnode = dests[idx];
//        printf("\ncalling calcE2e on vlink %d device %d\n", vnode->vl->id, vnode->device->id); // DEBUG
        Error err = vnode->prepareTest(shuffle);
        if(err) {
            return err;
        }
    }
    return Error::Success;
}

Vlink* VlinkConfig::getVlink(int id) const {
    auto found = vlinks.find(id);
    assert(found != vlinks.end());
    return found->second.get();
}

Device* VlinkConfig::getDevice(int id) const {
    auto found = devices.find(id);
    assert(found != devices.end());
    return found->second.get();
}

int VlinkConfig::connectedPort(int portId) const {
    auto found = links.find(portId);
    assert(found != links.end());
    return found->second;
}

int VlinkConfig::portDevice(int portId) const {
    auto found = _portDevice.find(portId);
    assert(found != _portDevice.end());
    return found->second;
}

std::vector<Vlink*> VlinkConfig::getAllVlinks() const {
    std::vector<Vlink*> res;
    res.reserve(vlinks.size());
    for(const auto& pair: vlinks) {
        res.push_back(pair.second.get());
    }
    return res;
}

std::vector<Device*> VlinkConfig::getAllDevices() const {
    std::vector<Device*> res;
    res.reserve(devices.size());
    for(const auto& pair: devices) {
        res.push_back(pair.second.get());
    }
    return res;
}

void PortDelays::setInDelays(const std::map<int, DelayData> &values) {
    inDelays = values;
    _ready = true;
}

Error PortDelays::calc(int vl, bool first) {
    if(delays[vl].ready()) {
        return Error::Success;
    }
    if(!first) {
//            auto start = std::chrono::high_resolution_clock::now(); // DEBUG
        auto err = calcCommon(vl);
//            printf("[port %d %*s] (vl %d calcCommon) %lu indelays, %lu delays : %ld us\n", port->id, 40, type.c_str(), vl, inDelays.size(), delays.size(),
//                   std::chrono::duration_cast<std::chrono::microseconds>((std::chrono::high_resolution_clock::now() - start)).count()); // DEBUG
        return err;
    } else {
        return calcFirst(vl);
    }
}

DelayData PortDelays::getFromMap(int vl, const std::map<int, DelayData> &delaysMap) {
    auto found = delaysMap.find(vl);
    assert(found != delaysMap.end());
    return found->second;
}

// register Creators for all PortDelays types
void PortDelaysFactory::RegisterAll() {
    AddCreator<Mock>("Mock");
    AddCreator<VoqA>("VoqA");
    AddCreator<VoqB>("VoqB");
    AddCreator<OqPacket<>>("OqPacket");
    AddCreator<OqA>("OqA");
    AddCreator<OqB>("OqB");
    AddCreator<OqCellB>("OqC"); // DEBUG
}

PortDelaysOwn PortDelaysFactory::Create(const std::string& name, Port* port) {
    auto found = creators.find(name);
    if(found != creators.end()) {
        return found->second->Create(port);
    } else {
        throw std::logic_error("invalid PortDelays type");
    }
}

int defaultIntMap::Get(int key) {
    auto found = find(key);
    return found == end() ? 0 : found->second;
}

void defaultIntMap::Inc(int key, int val) {
    auto found = find(key);
    if(found == end()) {
        (*this)[key] = val;
    } else {
        (*this)[key] += val;
    }
}
