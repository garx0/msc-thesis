#include <iostream>
#include <cstdio>
#include <vector>
#include "algo.h"

int treeSize(const Vnode* vnode) {
    int s = 1;
    for(const auto& own: vnode->next) {
        s += treeSize(own.get());
    }
    return s;
}

void VlinkConfig::calcChainMaxSize() {
    // посчитать сумму размеров деревьев vnode - 1
    int s = 0;
    for(auto vl: getAllVlinks()) {
        s += treeSize(vl->src.get()) - 1;
    }
    chainMaxSize = s;
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

Vlink::Vlink(VlinkConfig* config, int id, std::vector<std::vector<int>> paths,
    int bag, int smax, int smin, double jit0)
    : config(config), id(id), bag(bag), smax(smax), smin(smin), jit0(jit0)
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
      prev(prev), device(vlink->config->getDevice(deviceId)), e2e()
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

Error Vnode::prepareCalc(int chainSize, std::string debugPrefix) const { // prefix for DEBUG
    if(chainSize > config->chainMaxSize) {
        return Error::Cycle;
    }
    assert(prev != nullptr);
    if(prev->prev == nullptr) {
        in->delays->calc(vl->id, true);
    } else {
        Error err;
        std::map<int, DelayData> requiredDelays;
        auto fromOutPort = prev->device->fromOutPort(outPrev);
        for(auto nextToCur: fromOutPort) {
            auto curVnode = nextToCur->prev;
            printf("%svl %d-%d calling prepareCalc on vl %d-%d\n",
                   debugPrefix.c_str(), vl->id, device->id, curVnode->vl->id, curVnode->device->id); // DEBUG
            err = curVnode->prepareCalc(++chainSize, debugPrefix+"\t");
            if(err != Error::Success) {
                return err;
            }
            int vlId = curVnode->vl->id;
            requiredDelays[vlId] = curVnode->in->delays->getDelay(vlId);
            assert(curVnode->in->delays->getDelay(vlId).ready());
        }
        in->delays->setInDelays(requiredDelays);
        err = in->delays->calc(vl->id);
        if(err != Error::Success) {
            return err;
        }
    }
    return Error::Success;
}

Error VlinkConfig::calcE2e() {
    for(auto vl: getAllVlinks()) {
        for(auto [_, vnode]: vl->dst) {
            printf("\ncalling calcE2e on vlink %d device %d\n", vl->id, vnode->device->id); // DEBUG
            Error err = vnode->calcE2e();
            if(err != Error::Success) {
                return err;
            }
            DelayData e2e = vnode->e2e; // DEBUG
            printf("VL %d to %d: maxDelay = %f, jit = %f\n",
                    vl->id, vnode->device->id, e2e.dmax(), e2e.jit()); // DEBUG
        }
    }
    return Error::Success;
}

// register Creators for all PortDelays types
void PortDelaysFactory::RegisterAll() {
    AddCreator<Mock>("Mock");
    AddCreator<VoqA>("VoqA");
    AddCreator<OqPacket>("OqPacket");
}

PortDelaysOwn PortDelaysFactory::Create(const std::string& name, Port* port) {
    auto found = creators.find(name);
    if(found != creators.end()) {
        return found->second->Create(port);
    } else {
        throw std::logic_error("invalid PortDelays type");
    }
}

PortDelaysOwn PortDelaysFactory::Create(const std::string& name, Port* port, bool flag) {
    auto found = creators.find(name);
    if(found != creators.end()) {
        return found->second->Create(port, flag);
    } else {
        throw std::logic_error("invalid PortDelays type");
    }
}