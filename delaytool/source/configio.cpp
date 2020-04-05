#include <iostream>
#include <cstdio>
#include <vector>
#include <map>
#include <string>
#include <cassert>
#include <sstream>
#include <memory>
#include "configio.h"

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
    config->cellSize = 53; // TODO
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
        config->_portDevice[ports[0]] = number;
        config->devices[number] = std::make_unique<Device>(config.get(), Device::End, number);
        portNums[number] = ports;
    }

    for(auto res = resources->FirstChildElement("switch");
        res != nullptr;
        res = res->NextSiblingElement("switch"))
    {
        int number = std::stoi(res->Attribute("number"));
        std::vector<int> ports = TokenizeCsv(res->Attribute("ports"));
        for(auto portId: ports) {
            config->_portDevice[portId] = number;
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

void VlinkSubTreeDebugInfo(const Vnode* vnode) {
    printf("vnode %d, input port %d, after %d\n",
           vnode->device->id,
           vnode->in ? vnode->in->id : -1,
           vnode->prev ? vnode->prev->device->id : -1);
    for(const auto& nxt: vnode->next) {
        VlinkSubTreeDebugInfo(nxt.get());
    }
}

void VlinkPathDebugInfo(const Vnode* dest, const std::string& prefix) {
    if(dest->prev != nullptr) {
        VlinkPathDebugInfo(dest->prev, prefix);
        printf("%sdevice %d, prev output port %d, input port %d, after %d\n",
               prefix.c_str(), dest->device->id, dest->outPrev, dest->in->id, dest->prev->device->id);
    } else {
        printf("%sdevice %d, is source\n",
               prefix.c_str(), dest->device->id);
    }
}

void DebugInfo(const VlinkConfig* config) {
//    for(auto vl: config->getAllVlinks()) {
//        printf("vl %d:\n", vl->id);
//        VlinkSubTreeDebugInfo(vl->src.get());
//        printf("\n");
//    }
    for(auto vl: config->getAllVlinks()) {
        printf("vl %d:\n", vl->id);
        for(const auto [_, dst]: vl->dst) {
            printf("vl %d path to dst %d:\n", dst->vl->id, dst->device->id);
            VlinkPathDebugInfo(dst, "\t");
            printf("\n");
        }
        printf("\n");
    }
    printf("\n");
    for(auto device: config->getAllDevices()) {
        printf("device %d:\n", device->id);
        for(auto port: device->getAllPorts()) {
            printf("\tinput port %d:", port->id);
            auto inVnodes = port->getAllVnodes();
            if(!inVnodes.empty()) {
                printf(" vl");
                for(auto vnode: inVnodes) {
                    printf(" %d", vnode->vl->id);
                }
            }
            printf("\n");
            printf("\toutput port %d:", port->id);
            auto outVnodes = device->fromOutPort(port->id);
            if(!outVnodes.empty()) {
                printf(" vl");
                for(auto vnode: outVnodes) {
                    printf(" %d", vnode->vl->id);
                }
            }
            printf("\n");
        }
    }
    printf("\n");
}