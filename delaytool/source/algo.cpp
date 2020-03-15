#include "algo.h"
#include "tinyxml2/tinyxml2.h"

Device::Device(VlinkConfig* config, Type type, int id, const std::vector<int>& portNums)
    : config(config), id(id), type(type)
{
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
    delays = config->factory->Create(config->schemeType, this);
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
        Vnode* vnode;
        Vnode* vnodeNext = src.get();
        while(vnodeNext != nullptr) {
            assert(i < path.size() - 1);
            vnode = vnodeNext;
            vnodeNext = vnode->selectNext(path[++i]);
        }
        // now we need to add a new-made Vnode of device path[i] to vnode->next vector
        for(; i < path.size(); i++) {
            vnode->next.push_back(std::make_unique<Vnode>(this, path[i], vnode));
            vnode = vnode->next[vnode->next.size()-1].get();
        }
    }
}

Vnode::Vnode(Vlink* vlink, int deviceId, Vnode* prev)
    : config(vlink->config), vl(vlink),
      prev(prev), device(vlink->config->getDevice(deviceId)), e2e()
{
    // find port by prev->device and config->links
    for(auto& curPort: device->ports) {
        if(curPort->prevDevice->id == prev->device->id) {
            in = curPort.get();
            outPrev = in->outPrev;
            return;
        }
    }
    assert(false);
}

Error VlinkConfig::calcE2e() {
    for(auto& vlPair: vlinks) {
        auto vl = vlPair.second.get();
        for(auto vnode: vl->dst) {
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
    AddCreator<VoqA>("VoqA");
    AddCreator<OqPacket>("OqPacket");
}

PortDelaysSp PortDelaysFactory::Create(const std::string& name, Port* port) {
    auto found = creators.find(name);
    if(found != creators.end()) {
        return found->second->Create(port);
    } else {
        throw std::logic_error("invalid PortDelays type");
    }
}

PortDelaysSp PortDelaysFactory::Create(const std::string& name, Port* port, bool flag) {
    auto found = creators.find(name);
    if(found != creators.end()) {
        return found->second->Create(port, flag);
    } else {
        throw std::logic_error("invalid PortDelays type");
    }
}

std::vector<int> TokenizeCsv(const std::string& str) {
    std::vector<int> res;

    std::stringstream ss(str);
    int num;
    while(ss >> num) {
        res.push_back(num);
        if (ss.peek() == ',' || ss.peek() == ' ') {
            ss.ignore();
        }
    }
    return res;
}

VlinkConfigSp fromXml(tinyxml2::XMLDocument& doc) {
    VlinkConfigSp config = std::make_unique<VlinkConfig>();
    auto afdxxml = doc.FirstChildElement("afdxxml");
    auto resources = afdxxml->FirstChildElement("resources");
    double cap = std::stof(resources->FirstChildElement("link")->Attribute("capacity"));
    config->linkRate = cap;
    config->cellSize = 100; // TODO
    config->schemeType = "VoqA"; // TODO

    for(auto res = resources->FirstChildElement("link");
        res != nullptr;
        res = res->NextSiblingElement("link"))
    {
        double curCap = std::stof(res->Attribute("capacity"));
        if(curCap != cap) {
            std::cerr << "error: bad input resources - all links must have the same capacity" << std::endl;
            return nullptr;
        }
        int port1 = std::stoi(res->Attribute("from"));
        int port2 = std::stoi(res->Attribute("to"));
        config->links[port1] = port2;
        config->links[port2] = port1;
    }

    for(auto res = resources->FirstChildElement("endSystem");
        res != nullptr;
        res = res->NextSiblingElement("endSystem"))
    {
        printf("ES %s\n", res->Attribute("number"));
        std::vector<int> ports = TokenizeCsv(res->Attribute("ports"));
        if(ports.size() != 1) {
            std::cerr << "error: bad input - end systems must have one port" << std::endl;
            return nullptr;
        }
        int number = std::stoi(res->Attribute("number"));
        config->_portCoords[ports[0]] = {number, 0};
        config->devices[number] = std::make_unique<Device>(config.get(), Device::Src, number, std::vector<int>());
        config->devices[number] = std::make_unique<Device>(config.get(), Device::Dst, number, ports);
    }

    for(auto res = resources->FirstChildElement("switch");
        res != nullptr;
        res = res->NextSiblingElement("switch"))
    {
        int number = std::stoi(res->Attribute("number"));
        std::vector<int> ports = TokenizeCsv(res->Attribute("ports"));
        for(size_t idx = 0; idx < ports.size(); idx++) {
            config->_portCoords[ports[idx]] = {number, idx};
        }
        config->devices[number] = std::make_unique<Device>(config.get(), Device::Switch, number, ports);
        printf("switch %s\n", res->Attribute("number"));
    }

    auto vls = afdxxml->FirstChildElement("virtualLinks");
    for(auto vl = vls->FirstChildElement("virtualLink");
        vl != nullptr;
        vl = vl->NextSiblingElement("virtualLink"))
    {
        std::vector<std::vector<int>> paths;
        int number = std::stoi(vl->Attribute("number"));
        int bag = std::stoi(vl->Attribute("bag"));
        int smax = std::stoi(vl->Attribute("lmax"));
        for(auto pathEl = vl->FirstChildElement("path");
            pathEl != nullptr;
            pathEl = pathEl->NextSiblingElement("path"))
        {
            std::vector<int> path = TokenizeCsv(pathEl->Attribute("path"));
            assert(!path.empty() && path[path.size()-1] == std::stoi(pathEl->Attribute("dest")));
            paths.push_back(path);
            printf("VL %s path to %s\n", vl->Attribute("number"), pathEl->Attribute("dest"));
        }
        int jit0 = 0; // TODO откуда взять
        config->vlinks[number] = std::make_unique<Vlink>(config.get(), number, paths, bag, smax, 1, jit0);
    }
    return config;
}

bool toXml(VlinkConfig& config, tinyxml2::XMLDocument& doc) {
    // TODO мб писать в тот же xml в качестве атрибута?
    return false; // TODO
}

int main() {tinyxml2::XMLDocument doc_in;
    std::string file_in = "in.afdxxml";
    std::string file_out = "out.afdxxml";
    auto err = doc_in.LoadFile(file_in.c_str());
    if(err) {
        std::cerr << "error opening input file: " << tinyxml2::XMLDocument::ErrorIDToName(err) << std::endl;
        return 0;
    }
    tinyxml2::XMLDocument doc_out;
    FILE *fp_out = fopen(file_out.c_str(), "w");
    if(fp_out == nullptr) {
        std::cerr << "error: cannot open " << file_out << std::endl;
        return 0;
    }
    VlinkConfigSp config = fromXml(doc_in);
    if(config == nullptr) {
        std::cerr << "error reading from xml" << std::endl;
        fclose(fp_out);
        return 0;
    }
    Error calcErr = config->calcE2e();
    if(calcErr != Error::Success) {
        // TODO error info
        std::cerr << "error calculating delay: bad VL configuration" << std::endl;
        fclose(fp_out);
        return 0;
    }
    bool ok = toXml(*config.get(), doc_out);
    if(!ok) {
        std::cerr << "error converting to xml" << std::endl;
        fclose(fp_out);
        return 0;
    }
    err = doc_out.SaveFile(fp_out, false);
    if(err) {
        std::cerr << "error writing to output file: " << tinyxml2::XMLDocument::ErrorIDToName(err) << std::endl;
    }
    fclose(fp_out);
    return 0;
}
