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

class Vlink;
class Vnode;
class Device;
class DelayTask;
class Port;
class VlinkConfig;
class CioqMap;
class PortsSubgraph;
class QRTA;

using VlinkOwn = std::unique_ptr<Vlink>;
using VnodeOwn = std::unique_ptr<Vnode>;
using DeviceOwn = std::unique_ptr<Device>;
using DelayTaskOwn = std::unique_ptr<DelayTask>;
using PortOwn = std::unique_ptr<Port>;
using VlinkConfigOwn = std::unique_ptr<VlinkConfig>;
using CioqMapOwn = std::unique_ptr<CioqMap>;
using PortsSubgraphOwn = std::unique_ptr<PortsSubgraph>;
using QRTAOwn = std::unique_ptr<QRTA>;

class Error {
public:
    enum ErrorType {Success, Cycle, VoqOverload, BpTooLong, BpEndless, CyclicTooLong};

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
            case BpEndless:
                return "BpEndless";
            case CyclicTooLong:
                return "CyclicTooLong";
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
    std::map<int, VlinkOwn> vlinks;
    std::map<int, DeviceOwn> devices;
    std::map<int, int> _portDevice; // get device ID by input/output port ID
    std::map<int, int> links; // port1 id -> id of port2 connected with port1 via link
    int n_fabrics;
    int n_queues;
    uint64_t bpMaxIter;
    uint64_t cyclicMaxIter;
    int n_tasks;

    std::vector<DelayTask*> tasks;
    std::vector<DelayTask*> acyclicTasksOrder;
    std::vector<DelayTask*> cyclicTasksOrder;

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
    Error calcDelays(bool print = false);

    Error buildTables(bool print = false);

    // calculate bwUsage() values on all input ports and return them as map by port number
    std::map<int, double> bwUsage();

    // convert from linkByte measure unit to ms
    // (by division by link rate in byte/ms)
    double linkByte2ms(int64_t linkByte) { return static_cast<double>(linkByte) / linkRate; }
private:
    Error buildDelayTasks();
    Error _buildDelayTasksCIOQ();
    Error _buildDelayTasksOQ();

    Error buildTasksOrder();
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
    enum type_t {Switch, End};
    enum elem_t {F, P}; // fabric, output port

    Device(VlinkConfig* config, type_t type, int id)
        : config(config), id(id), type(type), cioqMap(nullptr) {}

    // called when config->_portDevices is complete
    void AddPorts(const std::vector<int>& portIds);

    VlinkConfig* const config;
    const int id;
    const type_t type;

    std::map<int, PortOwn> ports; // input ports
//    std::map<int, Port*> outPorts; // input ports connected to this device's output ports
    std::vector<Vlink*> sourceFor; // Vlinks which have this device as source

    CioqMapOwn cioqMap;
    std::map<std::pair<elem_t, int>, QRTAOwn> qrtas;

    Port* getPort(int portId) const;

    std::vector<Port*> getAllPorts() const;

    std::vector<Port*> getAllOutPortsIn() const;

    std::vector<int> getAllPortIds() const; // sorted by number ascending

    std::vector<int> getAllOutPortPseudoIds() const; // sorted by number ascending

    // get input port connected with output port portId
    Port* fromOutPort(int portId) const;

    // get input port connected with output port with pseudo id portPseudoId (which is id of that input port)
    Port* fromOutPortByPseudoId(int portPseudoId) const;

    bool hasVlinks(int in_port_id, int out_port_pseudo_id) const;

    std::vector<Vnode*> getVlinks(int in_port_id, int out_port_pseudo_id) const;
};

// INPUT PORT
// an output port may be referred by either its id or its pseudo-id.
// pseudo-id of an output port is id of an input port connected with it by a link.
// so instead of any output port of a device we can refer to an input port of another device, and this mapping is bijective.
class Port
{
public:
    // there's a link connecting output port outPrev of prevDevice with input port id (this) of device
    int id;
    int outPrev; // id of output port with which this input port is connected by link
    Device* const device;
    Device* prevDevice;
    std::map<int, Vnode*> vnodes; // get Vnode by Vlink id

    Port(Device* device, int number);

    Vnode* getVnode(int vlId) const;

    std::vector<Vnode*> getAllVnodes() const;

    // usage ratio of the connected link bandwidth by VLs
    // must be in [0,1] if VL config is correct
    // calculated as sum of smax/bag/linkRate by VLs that using this port
    double bwUsage() const;
};

// time is measured in bytes through link = (time in ms) * (link rate in byte/ms) / (1 byte)
class DelayData
{
public:
    explicit DelayData(): _vl(nullptr), _dmin(-2), _jit(-1), _dmax(-1), _ready(false) {}
    explicit DelayData(Vlink* vl, int64_t dmin, int64_t jit): _vl(vl), _dmin(dmin), _jit(jit), _dmax(dmin + jit), _ready(true) {}

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

    // key is <element type, branchId>, where branchId == vnodeX->in->id, where vnodeX in this->next
    std::map<std::pair<Device::elem_t, int>, DelayTaskOwn> delayTasks;

    // e2e-delay, used only if this->device->schemeName == Device::Dst
    DelayData e2e;

    Error calcE2e();

    // portId is id of an input port in another device
    Vnode* selectNext(int portId) const;

    // get ids of all devices which are leafs of this node's subtree
    std::vector<const Vnode*> getAllDests() const;

private:
    // prepare input delay data for calculation of delay of this vnode AND calculate this delay
//    Error prepareCalc();

    // recursive helper function for getAllDests()
    void _getAllDests(std::vector<const Vnode*>& vec) const;
};

// delay for packets of a VL from generation to leaving a network element.
// destination output port of a VL in a device containing this network element is specified
// (but in form of vnode of this VL in the input port connected with this output port).
// type of the network element is either fabric (Device::F) or output port (Device::P).
class DelayTask
{
public:
    explicit DelayTask(Vlink* vl, Vnode* vnode_next, Device::elem_t elem, QRTA* qrta)
            : config(vl->config), vl(vl), vnode(vnode_next->prev), vnode_next(vnode_next),
              device(vnode_next->prev->device),
              elem(elem),
              in_id(vnode_next->prev->in != nullptr ? vnode_next->prev->in->id : -1),
              out_pseudo_id(vnode_next->in->id),
              id(std::make_tuple(vl->id, vnode_next->in->id, elem)),
              qrta(qrta), delay(vl, 0, 0),
              in_cycle(true), iter(0), cyclic_layer(-1), max_input_layer(-1) {}

    VlinkConfig* const config;
    Vlink* const vl;
    Vnode* const vnode;
    Vnode* const vnode_next;
    Device* const device;
    Device::elem_t const elem; // type of the network element: fabric (Device::F) or output port (Device::P)
    int const in_id; // input port id
    int const out_pseudo_id; // output port pseudo id
    std::tuple<int, int, Device::elem_t> const id; // elem, vl->id, out_pseudo_id
    QRTA* const qrta;
    DelayData delay;

    // Multiset of delay tasks containing input data for this delay task.
    // Let inputs[vl_id, branch_id] == delay_task, then:
    // vl_id == delay_task->vl->id
    // delay_task->vnode_next == this->vnode
    // delay_task->elemType != this->elemType
    // branch_id == vnodeX->in->id for some vnodeX in delay_task_vnode_next->next
    // If VL X splits in this->device, and N of its branches through this->device are concurring with
    // the branch of this->vl to this->vnode_next, then N copies of DelayTask VL X on previous device
    // are included in this map, and they are distinguished by branch_id.
    std::map<std::pair<int, int>, DelayTask*> inputs;

    // Set of delay tasks for which this delay task contains input data.
    // Let inputs[vl_id, branch_id] == delay_task, then:
    // vl_id == delay_task->vl->id
    // branch_id == delay_task->vnode_next->in->id
    // delay_task->elemType != this->elemType
    std::map<std::pair<int, int>, DelayTask*> output_for;

    bool in_cycle;
    int iter;
    int cyclic_layer;
    int max_input_layer;

    void get_input_data();
    void clear_bp();
    Error calc_delay_init();
    Error calc_delay_max();
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

    // input port id -> output port pseudo-id -> queue id
    // queue id = 0, 1, identifying it between queues of an input port
    std::map<int, std::map<int, int>> queueTable;

    // <input port id, queue id> -> fabric id
    std::map<std::pair<int, int>, int> fabricTable;

    std::vector<PortsSubgraphOwn> comps;

    // in_port_id, out_port_pseudo_id -> PortsSubgraph
    std::map<std::pair<int, int>, PortsSubgraph*> compsIndex;

    void setMap(const std::map<int, std::map<int, int>>& _queueTable,
                const std::map<std::pair<int, int>, int>& _fabricTable,
                bool print = false);

    int getQueueId(int in_port_id, int out_port_id) const;

    int getFabricId(int in_port_id, int queue_id) const;

    int getFabricIdByEdge(int in_port_id, int out_port_id) const;

    // find a component of a fabric-induced subgraph of the switch traffic graph, that has the specified input queue
    std::set<std::pair<int, int>> buildComp(int in_port_id, int queue_id) const;
};

void generateTableBasic(Device* device, int n_queues, int n_fabrics, bool print = false);

// bipartite graph
class PortsSubgraph
{
public:
    PortsSubgraph(int id): id(id) {}

    PortsSubgraph(int id, std::set<std::pair<int, int>> edges): id(id), edges(edges) {}

    int id;
    std::set<std::pair<int, int>> edges;

    bool isConnected(int node_in, int node_out) const;
};

// floor(x/y)
inline int64_t floordiv(int64_t x, int64_t y) {
    return x / y;
}

// ceil(x/y), x >= 0
inline int64_t ceildiv(int64_t x, int64_t y) {
    return x / y + (x % y != 0);
}

// = ceil(x/y + 0) = floor(x/y) + 1, x >= 0
inline int64_t ceildiv_up(int64_t x, int64_t y) {
    return x / y + 1;
}

inline int64_t numPackets(int64_t interval, int64_t bag, int64_t jit) {
    return ceildiv(interval + jit, bag);
}

// = numPackets(interval+0, ...) (limit from above)
inline int64_t numPacketsUp(int64_t interval, int64_t bag, int64_t jit) {
    return ceildiv_up(interval + jit, bag);
}

// round x to a next multiple of k
inline int64_t roundToMultiple(int64_t x, int64_t k) {
    return x + k * (x % k != 0) - x % k;
}

inline std::string OqOverloadVerbose(Vlink* vl, Device* device) {
    return std::string("overload for vl ")
           + std::to_string(vl->id)
           + " of switch "
           + std::to_string(device->id)
           + " because busy period calculation took over "
           + std::to_string(device->config->bpMaxIter)
           + " iterations";
}

class QRTA
{
public:
    QRTA(VlinkConfig* config): config(config), bp(-1) {}

    // == Rk,CVL(t) - Jk, k == vl->id
    int64_t delayFunc(int64_t t, Vlink* vl, int cur_branch_id) const;

    // == Rk,CVL(q)* - Jk, k == vl->id
    int64_t delayFuncRem(int q, Vlink* vl, int cur_branch_id) const;

    Error calc_bp();

    DelayData calc_result;

    void setInDelays(const std::map<std::pair<int, int>, DelayData>& _inDelays) {
        inDelays = _inDelays;
    }

    // recalculates bp only if it is empty
    Error calc(Vlink* curVl, int cur_branch_id);

    Error clear_bp();

    double total_rate();

private:
    VlinkConfig* config;
    int64_t bp;
    std::map<std::pair<int, int>, DelayData> inDelays;

    static int64_t busyPeriod(const std::map<std::pair<int, int>, DelayData>& inDelays, VlinkConfig* config);
};

#endif //DELAYTOOL_ALGO_H