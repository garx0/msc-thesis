#include "algo.h"
#include "tinyxml2/tinyxml2.h"
#include "argparse/argparse.hpp"

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
        assert(device->type == Device::Src);
        in = nullptr;
        outPrev = -1;
    }
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

VlinkConfigOwn fromXml(tinyxml2::XMLDocument& doc, const std::string& scheme) {
    VlinkConfigOwn config = std::make_unique<VlinkConfig>();
    auto afdxxml = doc.FirstChildElement("afdxxml");
    auto resources = afdxxml->FirstChildElement("resources");
    double cap = std::stof(resources->FirstChildElement("link")->Attribute("capacity"));
    config->linkRate = cap;
    config->cellSize = 100; // TODO
    config->voqL = 50;
    config->scheme = scheme;

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

    std::map<int, std::vector<int>> portNums;

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
        config->sources[number] = std::make_unique<Device>(config.get(), Device::Src, number);
        config->devices[number] = std::make_unique<Device>(config.get(), Device::Dst, number);
        portNums[number] = ports;
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
        config->devices[number] = std::make_unique<Device>(config.get(), Device::Switch, number);
        portNums[number] = ports;
        printf("switch %s\n", res->Attribute("number"));
    }
    // add ports
    for(auto [num, ports] : portNums) {
        config->getDevice(num)->AddPorts(ports);
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
        int jit0 = 0; // TODO взять из xml, если есть, иначе 0 или некоторый jmax
        config->vlinks[number] = std::make_unique<Vlink>(config.get(), number, paths, bag, smax, 1, jit0);
    }
    config->calcChainMaxSize();
    printf("chainMaxSize = %d\n", config->chainMaxSize); // DEBUG
    return config;
}

// doc must already contain the resources and VL configuration
// (e.g. doc used for building config)
bool toXml(VlinkConfig* config, tinyxml2::XMLDocument& doc) {
    auto afdxxml = doc.FirstChildElement("afdxxml");
    auto resources = afdxxml->FirstChildElement("resources");
    for(auto res = resources->FirstChildElement("switch");
        res != nullptr;
        res = res->NextSiblingElement("switch"))
    {
        res->SetAttribute("scheme", config->scheme.c_str());
        if(config->scheme != "OqPacket") {
            res->SetAttribute("cellSize", config->cellSize);
        }
        if(config->scheme == "VoqA" || config->scheme == "VoqB") {
            res->SetAttribute("processingPeriod", config->voqL);
        }
    }
    auto vls = afdxxml->FirstChildElement("virtualLinks");
    for(auto vlEl = vls->FirstChildElement("virtualLink");
        vlEl != nullptr;
        vlEl = vlEl->NextSiblingElement("virtualLink"))
    {
        int number = std::stoi(vlEl->Attribute("number"));
        Vlink* vl = config->getVlink(number);
        for(auto path = vlEl->FirstChildElement("path");
            path != nullptr;
            path = path->NextSiblingElement("path"))
        {
            int deviceId = std::stoi(path->Attribute("dest"));
            auto found = vl->dst.find(deviceId);
            assert(found != vl->dst.end());
            Vnode* vnode = found->second;
            path->SetAttribute("maxDelay", vnode->e2e.dmax());
            path->SetAttribute("maxJit", vnode->e2e.jit());
        }
    }
    return true;
}

std::string strToLower(const std::string& str) {
    std::string str2 = str;
    std::for_each(str2.begin(), str2.end(), [](char& c){
        c = static_cast<char>(std::tolower(c));
    });
    return str2;
}

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("delaytool");

    program.add_argument("input")
        .help("input xml file with network resources and virtual links");

    program.add_argument("output")
        .help("output xml file with delays of all VLs to all their destinations");

    program.add_argument("-s", "--scheme")
        .help("scheme type: voqa|voqb|oqp|oqa|oqb")
        .default_value(std::string("Mock")) // TODO OqPacket
        .action([](const std::string& value) {
            static const std::map<std::string, std::string> mapping = {
                    {"voqa", "VoqA"},
                    {"voqb", "VoqB"},
                    {"oqp", "OqPacket"},
                    {"oqa", "OqA"},
                    {"oqb", "OqB"}
            };
            auto found = mapping.find(strToLower(value));
            if(found != mapping.end()) {
                return found->second;
            } else {
                throw std::runtime_error("bad value of -s");
            }
        });

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cout << program;
        return 0;
    }

    std::string fileIn = program.get<std::string>("input");
    std::string fileOut = program.get<std::string>("output");
    std::string scheme = program.get<std::string>("--scheme");
    tinyxml2::XMLDocument doc;
    auto err = doc.LoadFile(fileIn.c_str());
    if(err) {
        std::cerr << "error opening input file: " << tinyxml2::XMLDocument::ErrorIDToName(err) << std::endl;
        return 0;
    }
    FILE *fpOut = fopen(fileOut.c_str(), "w");
    if(fpOut == nullptr) {
        std::cerr << "error: cannot open " << fileOut << std::endl;
        return 0;
    }
    VlinkConfigOwn config = fromXml(doc, scheme);
    if(config == nullptr) {
        std::cerr << "error reading from xml" << std::endl;
        fclose(fpOut);
        return 0;
    }
    Error calcErr = config->calcE2e();
    if(calcErr != Error::Success) {
        // TODO error info
        std::cerr << "error calculating delay: bad VL configuration, code=" << static_cast<int>(calcErr) << std::endl;
        fclose(fpOut);
        return 0;
    }
    bool ok = toXml(config.get(), doc);
    if(!ok) {
        std::cerr << "error converting output to xml" << std::endl;
        fclose(fpOut);
        return 0;
    }
    err = doc.SaveFile(fpOut, false);
    if(err) {
        std::cerr << "error writing to output file: " << tinyxml2::XMLDocument::ErrorIDToName(err) << std::endl;
    }
    fclose(fpOut);
    return 0;
}
