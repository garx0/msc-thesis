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
    for(const auto& pair: vlinks) {
        Vlink* vl = pair.second.get();
        s += treeSize(vl->src.get()) - 1;
    }
    chainMaxSize = s;
}

void Device::AddPorts(const std::vector<int>& portNums) {
    for(int i: portNums) {
        // у нас есть только номер порта.
        ports.push_back(std::make_unique<Port>(this, i));
    }
}

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

Port::Port(Device* device, int number)
    : device(device)
{
    // calculate idx, outPrev, prevDevice by number and links and portCoords in device->config
    VlinkConfig* config = device->config;

    // TODO может забить на индексы и сделать тоже map ports в Device
    auto coords = config->portCoords(number);
    int deviceId = coords.first;
    idx = coords.second;
    assert(deviceId == device->id);

    int prevNumber = config->connectedPort(number);

    auto prevCoords = config->portCoords(prevNumber);
    int prevDeviceId = prevCoords.first;
    outPrev = prevCoords.second;

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
      prev(prev), device(vlink->config->getDevice(deviceId, prev == nullptr)), e2e()
{
    if(prev != nullptr) {
        // find port by prev->device
        assert(!device->ports.empty());
        for(auto& curPort: device->ports) {
            if(curPort->prevDevice->id == prev->device->id) {
                in = curPort.get();
                outPrev = in->outPrev;
                return;
            }
        }
        assert(false);
    } else {
        in = nullptr;
        outPrev = -1;
    }
}

Error Vnode::prepareCalc(int chainSize) const {
    printf("called prepareCalc on vl %d device %d\n", vl->id, device->id); // DEBUG
    if(chainSize > config->chainMaxSize) {
        return Error::Cycle;
    }

    if(device->type != Device::Src && prev->device->type == Device::Src) {
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

Error VlinkConfig::calcE2e() {
    for(auto& vlPair: vlinks) {
        Vlink* vl = vlPair.second.get();
        for(auto [deviceId, vnode]: vl->dst) {
            Error err = vnode->calcE2e();
            printf("called calcE2e on vnode\n"); // DEBUG
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