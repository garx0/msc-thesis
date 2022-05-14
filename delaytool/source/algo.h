#pragma once
#ifndef DELAYTOOL_ALGO_H
#define DELAYTOOL_ALGO_H

#include <iostream>
#include <cstdio>
#include <memory>
#include <vector>
#include <map>
#include <cassert>
#include <set>
#include "algo.h"

class Vlink;
class Vnode;
class Device;
class GroupDelays;
class GroupDelaysFactory;
class Port;
class VlinkConfig;
class CioqMap;
class PortsSubgraph;

using VlinkOwn = std::unique_ptr<Vlink>;
using VnodeOwn = std::unique_ptr<Vnode>;
using DeviceOwn = std::unique_ptr<Device>;
using GroupDelaysOwn = std::unique_ptr<GroupDelays>;
using PortOwn = std::unique_ptr<Port>;
using VlinkConfigOwn = std::unique_ptr<VlinkConfig>;
using PortDelaysFactoryOwn = std::unique_ptr<GroupDelaysFactory>;
using CioqMapOwn = std::unique_ptr<CioqMap>;
using PortsSubgraphOwn = std::unique_ptr<PortsSubgraph>;

// returns shuffled {0, ..., size - 1} vector if shuffle=true, not shuffled otherwise
// uses C random tools
// shuffle used only for DeletePaths tool, not for delay calculation
std::vector<int> idxRange(int size, bool shuffle);

class Error {
public:
    enum ErrorType {Success, Cycle, VoqOverload, BpTooLong};

    Error(ErrorType type = Success, const std::string& verbose = "", const std::string& verboseRaw = "")
        : type(type), verbose(verbose), verboseRaw(verboseRaw) {}

    Error(Error& other) = default;

    Error(Error&& other) = default;

    Error& operator=(Error& other) = default;

    Error& operator=(Error&& other) = default;

    const std::string& Verbose() const {
        return verbose;
    }

    const std::string& VerboseRaw() const {
        return verboseRaw;
    }

    std::string TypeString() const {
        switch(type) {
            case Success:
                return "Success";
            case Cycle:
                return "Cycle";
            case VoqOverload:
                return "VoqOverload";
            case BpTooLong:
                return "BpTooLong";
        }
        return "";
    }

    operator bool() const {
        return type != Success;
    }

    bool operator==(Error& other) const {
        return type == other.type;
    }

    bool operator!=(Error& other) const {
        return type != other.type;
    }

    bool operator==(ErrorType other) const {
        return type == other;
    }

    bool operator!=(ErrorType other) const {
        return type != other;
    }

    friend bool operator==(ErrorType lhs, const Error& rhs);
    friend bool operator!=(ErrorType lhs, const Error& rhs);

private:
    ErrorType type;
    std::string verbose;
    std::string verboseRaw;
};

bool operator==(Error::ErrorType lhs, const Error& rhs);

bool operator!=(Error::ErrorType lhs, const Error& rhs);

class VlinkConfig
{
public:
    VlinkConfig();

    int64_t linkRate; // R, byte/ms
    std::string scheme;
    int cellSize; // σ, bytes
    int voqL; // for scheme == "VoqA", "VoqB"
    std::map<int, VlinkOwn> vlinks;
    std::map<int, DeviceOwn> devices;
    std::map<int, int> _portDevice; // get device ID by input/output port ID
    std::map<int, int> links; // port1 id -> id of port2 connected with port1 via link
    uint64_t bpMaxIter;
    PortDelaysFactoryOwn factory;

    Vlink* getVlink(int id) const;

    Device* getDevice(int id) const;

    // get id of input/output port connected by link with output/input port 'portId'
    int connectedPort(int portId) const;

    // get device ID by input/output port ID
    int portDevice(int portId) const;

    std::vector<Vlink*> getAllVlinks() const;

    std::vector<Device*> getAllDevices() const;

    // random order of vlinks and destinations processing if shuffle=true
    // prints delays if print=true
    Error calcE2e(bool print = false);

    Error detectCycles(bool shuffle = false);

    // calculate bwUsage() values on all input ports and return them as map by port number
    std::map<int, double> bwUsage(bool cells = false);

    // convert from linkByte measure unit to ms
    // (by division by link rate in byte/ms)
    double linkByte2ms(int64_t linkByte) { return static_cast<double>(linkByte) / linkRate; }
};

class Vlink
{
public:
    Vlink(VlinkConfig* config, int id, int srcId, std::vector<std::vector<int>> paths,
          int bag, int smax, int smin, double jit0);

    VlinkConfig* const config;
    const int id;
    VnodeOwn src; // tree root
    std::map<int, Vnode*> dst; // tree leaves, key is device id
    int bag; // in ms
    int64_t bagB; // in link-bytes, == bag * config->linkRate)
    int smax; // in bytes
    int smin; // in bytes
    double jit0; // jitter of start of packet transfer from source end system, in ms
    int64_t jit0b; // in link-bytes, == ceil(jit0 * config->linkRate)
};

class Device
{
public:
    enum Type {Switch, End};

    Device(VlinkConfig* config, Type type, int id)
        : config(config), id(id), type(type), cioqMap(std::make_unique<CioqMap>(this)) {}

    // called when config->_portDevices is complete
    void AddPorts(const std::vector<int>& portIds);

    VlinkConfig* const config;
    const int id;
    const Type type;

    std::map<int, PortOwn> ports; // input ports
    std::map<int, Port*> outPorts; // input ports connected to this device's output ports
    std::vector<Vlink*> sourceFor; // Vlinks which have this device as source

    CioqMapOwn cioqMap;

    Port* getPort(int portId) const;

    std::vector<Port*> getAllPorts() const;

    std::vector<int> getAllPortIds() const;

    // get input port connected with output port portId
    Port* fromOutPort(int portId) const;

    bool hasVlinks(int in_port_id, int out_port_id) const;

    std::vector<Vnode*> getVlinks(int in_port_id, int out_port_id) const;
};

// INPUT PORT
class Port
{
public:
    // there's a link connecting output port outPrev of prevDevice with input port id (this) of device
    int id;
    int outPrev; // id of output port with which this input port is connected by link
    Device* const device;
    Device* prevDevice;
    std::map<int, Vnode*> vnodes; // get Vnode by Vlink id
    GroupDelaysOwn delays; // delays until queuing in switch which contains this input port

    Port(Device* device, int number);

    Vnode* getVnode(int vlId) const;

    std::vector<Vnode*> getAllVnodes() const;

    // usage ratio of the connected link bandwidth by VLs
    // must be in [0,1] if VL config is correct
    // calculated as sum of smax/bag/linkRate by VLs that using this port
    double bwUsage(bool cells = false) const;
};

// time is measured in bytes through link = (time in ms) * (link rate in byte/ms) / (1 byte)
class DelayData
{
public:
    DelayData(): _vl(nullptr), _dmin(-2), _jit(-1), _dmax(-1), _ready(false) {}
    DelayData(Vlink* vl, int64_t dmin, int64_t jit): _vl(vl), _dmin(dmin), _jit(jit), _dmax(dmin + jit), _ready(true) {}

    bool ready() const { return _ready; }

    Vlink* vl() const { return _vl; }

    int64_t dmin() const { return _ready ? _dmin : -1; }

    int64_t jit() const { return _ready ? _jit : -1; }

    int64_t dmax() const { return _ready ? _dmax : -1; }

private:
    Vlink* _vl;
    int64_t _dmin;
    int64_t _jit;
    int64_t _dmax;
    bool _ready;
};

class GroupDelays
{
public:
    enum celltype_t {P, A, B};

    explicit GroupDelays(Device* device, const std::string& schemeName, celltype_t cellType)
    : config(device->config), device(device), schemeName(schemeName),
      cellType(cellType), _inputReady(false) {}

    VlinkConfig* const config;
//    Port* const port;
    Device* const device;
    std::string schemeName;
    celltype_t const cellType;

    void setInputReady() { _inputReady = true; }

    bool readyIn() { return _inputReady; }

    DelayData getDelay(int vl) const { return getFromMap(vl, delays); }

    void setDelay(DelayData delay);

    DelayData getInDelay(int vl) const { return getFromMap(vl, inDelays); }

    void setInDelay(DelayData delay);

    const std::map<int, DelayData>& getDelays() const { return delays; }

    const std::map<int, DelayData>& getInDelays() const { return inDelays; }

    void setInDelays(const std::map<int, DelayData>& values);

    // max time from start of transfer into link
    // to start of entering into queue of next switch
    int64_t trMax(Vlink* vl) const;

    // min time from start of transfer into link
    // to start of entering into queue of next switch
    int64_t trMin(Vlink* vl) const;

    // first == true if prev device is switch
    Error calc(Vlink* vl, bool first = false);

    // outDelay[vl] must have been calculated prior to call of this method
    DelayData e2e(int vl) const;

    // is called in calc if prev device is switch
    virtual Error calcCommon(Vlink* vl) = 0;

protected:
    std::map<int, DelayData> delays;
    std::map<int, DelayData> inDelays;
    bool _inputReady;

    static DelayData getFromMap(int vl, const std::map<int, DelayData>& delaysMap);
};

class Vnode
{
public:
    Vnode(Vlink* vlink, int portId, Vnode* prev);

    Vnode(Vlink* vlink, int srcId); // for source end system

    VlinkConfig* const config;
    Vlink* const vl;
    Device* const device; // == in->device
    Vnode* const prev; // (also == vnode of same Vlink from prev device's ports, which is unambiguous)
    Port* const in; // in port of this device
    std::vector<VnodeOwn> next;
    int outPrev; // == in->outPrev - id of out port of prev device
    enum {NotVisited, VisitedNotPrepared, Prepared} cycleState; // for cycle detecting
    bool calcPrepared;

    // e2e-delay, used only if this->device->schemeName == Device::Dst
    DelayData e2e;

    // random order of data dependency graph traversal if shuffle=true
    Error calcE2e();

    // find cycles in data dependency graph of VL configuration
    // random order of data dependency graph traversal if shuffle=true
    Error prepareTest(bool shuffle = false);

    // portId is id of an input port in another device
    Vnode* selectNext(int portId) const;

    // get ids of all devices which are leafs of this node's subtree
    std::vector<const Vnode*> getAllDests() const;

private:
    // prepare input delay data for calculation of delay of this vnode AND calculate this delay
    Error prepareCalc();

    // recursive helper function for getAllDests()
    void _getAllDests(std::vector<const Vnode*>& vec) const;
};

//Error completeCheckVoq(Device *device);

class GroupDelaysFactory {
public:

    GroupDelaysFactory() { RegisterAll(); }

    // register Creators for all GroupDelays types
    void RegisterAll();

    // interface for TCreator
    // is to unite TCreator objects creating different GroupDelays under one schemeName
    class ICreator {
    public:
        ICreator() = default;
        virtual ~ICreator() = default;
        virtual GroupDelaysOwn Create(Port*) const = 0;
    };

    // class that create TGroupDelays objects with its constructor arguments
    template<typename TGroupDelays>
    class TCreator : public ICreator {
    public:
        GroupDelaysOwn Create(Port* port) const override {
            if constexpr(std::is_constructible_v<TGroupDelays, decltype(port)>) {
                return std::make_unique<TGroupDelays>(port);
            } else {
                throw std::logic_error("can't make GroupDelays with these argument types");
            }
        }
    };

    // register TCreator<TGroupDelays> at specified name
    template<typename TGroupDelays>
    void AddCreator(const std::string& name) {
        creators[name] = std::make_shared<GroupDelaysFactory::TCreator<TGroupDelays>>();
    }

    // calls corresponding Create method of Creator registered at specified name
    GroupDelaysOwn Create(const std::string& name, Port*);

private:
    using TCreatorPtr = std::shared_ptr<ICreator>;
    std::map<std::string, TCreatorPtr> creators;
};

class CioqMap
{
public:
    CioqMap(Device* device, int n_queues = 2, int n_fabrics = 8)
            : config(device->config), device(device), n_queues(n_queues), n_fabrics(n_fabrics) {}

    VlinkConfig* const config;
    Device* const device;
    int n_queues;
    int n_fabrics;

    // input port id -> output port id -> queue id
    // queue id = 0, 1, identifying it between queues of an input port
    std::map<int, std::map<int, int>> queueTable;

    // <input port id, queue id> -> fabric id
    std::map<std::pair<int, int>, int> fabricTable;

    std::vector<PortsSubgraphOwn> comps;

    // in_port_id, out_port_id -> PortsSubgraph
    std::map<std::pair<int, int>, PortsSubgraph*> compsIndex;

    void setMap(std::map<int, std::map<int, int>> _queueTable, std::map<std::pair<int, int>, int> _fabricTable);

    int getQueueId(int in_port_id, int out_port_id) const;

    int getFabricId(int in_port_id, int queue_id) const;

    int getFabricIdByEdge(int in_port_id, int out_port_id) const;

    std::set<std::pair<int, int>> buildComp(int in_port_id, int queue_id) const;
};

void generateTableBasic(Device* device, int n_queues = 2, int n_fabrics = 8);

// bipartite graph
class PortsSubgraph
{
public:
    PortsSubgraph() {}

    PortsSubgraph(std::set<std::pair<int, int>> edges): edges(edges) {}

    std::set<std::pair<int, int>> edges;

    bool isConnected(int node_in, int node_out) const;
};

#endif //DELAYTOOL_ALGO_H