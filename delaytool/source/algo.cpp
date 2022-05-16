#include <iostream>
#include <cstdio>
#include <vector>
#include <cmath>
//#include <random>
#include <algorithm>
#include "algo.h"
//#include "delay.h"

bool operator==(Error::ErrorType lhs, const Error& rhs) {
    return lhs == rhs.type;
}

bool operator!=(Error::ErrorType lhs, const Error& rhs) {
    return lhs != rhs.type;
}

void Device::AddPorts(const std::vector<int>& portIds) {
    for(int i: portIds) {
        // we only have port number
        ports[i] = std::make_unique<Port>(this, i);
    }
}

Port* Device::fromOutPort(int outPortId) const {
    auto inPortId = config->connectedPort(outPortId);
    auto deviceId = config->portDevice(inPortId);
    return config->getDevice(deviceId)->getPort(inPortId);
}

Port* Device::fromOutPortByPseudoId(int outPortPseudoId) const {
    auto deviceId = config->portDevice(outPortPseudoId);
    return config->getDevice(deviceId)->getPort(outPortPseudoId);
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

std::vector<Port*> Device::getAllOutPortsIn() const {
    std::vector<Port*> res;
    res.reserve(ports.size());
    for(const auto& pair: ports) {
        auto out_port_id = pair.second->id;
        auto out_port_in = fromOutPort(out_port_id);
        res.push_back(out_port_in);
    }
    return res;
}

std::vector<int> Device::getAllPortIds() const {
    std::vector<int> res;
    res.reserve(ports.size());
    for(const auto& pair: ports) {
        res.push_back(pair.second->id);
    }
    std::sort(res.begin(), res.end());
    return res;
}

std::vector<int> Device::getAllOutPortPseudoIds() const {
    std::vector<int> res;
    res.reserve(ports.size());
    for(const auto& pair: ports) {
        auto out_port_id = pair.second->id;
        auto out_port_pseudo_id = config->connectedPort(out_port_id);
        auto out_port_in = fromOutPort(out_port_id); // DEBUG
        assert(out_port_in->id == out_port_pseudo_id); // DEBUG
        res.push_back(out_port_pseudo_id);
    }
    std::sort(res.begin(), res.end());
    return res;
}

bool Device::hasVlinks(int in_port_id, int out_port_pseudo_id) const {
    auto in_port = getPort(in_port_id);
    auto out_port_in = fromOutPortByPseudoId(out_port_pseudo_id);
    for(auto vnode: in_port->getAllVnodes()) {
        if(vnode->selectNext(out_port_in->id) != nullptr) {
            return true;
        }
    }
    return false;
}

std::vector<Vnode*> Device::getVlinks(int in_port_id, int out_port_pseudo_id) const {
    auto in_port = getPort(in_port_id);
    auto out_port_in = fromOutPortByPseudoId(out_port_pseudo_id);
    std::vector<Vnode*> res;
    for(auto vnode: in_port->getAllVnodes()) {
        if(vnode->selectNext(out_port_in->id) != nullptr) {
            res.push_back(vnode);
        }
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

double Port::bwUsage() const {
    double sum = 0;
    for(auto vnode: getAllVnodes()) {
        int s = vnode->vl->smax;
        sum += static_cast<double>(s) / vnode->vl->bagB;
    }
    return sum;
}

Vlink::Vlink(VlinkConfig* config, int id, int srcId, std::vector<std::vector<int>> paths,
    int bag, int smax, int smin, double jit0)
    : config(config), id(id), bag(bag), bagB(bag * config->linkRate),
    smax(smax), smin(smin), jit0(jit0), jit0b(std::ceil(jit0 * config->linkRate))
{
    assert(!paths.empty());
    src = std::make_unique<Vnode>(this, srcId);
    src->device->sourceFor.push_back(this);

    for(const auto& path: paths) {
        int i = -1;
        Vnode* vnodeNext = src.get();
        Vnode* vnode = vnodeNext; // unnecessary if src != nullptr
        while(vnodeNext != nullptr) {
//            assert(i < path.size() - 1);
            vnode = vnodeNext;
            vnodeNext = vnode->selectNext(path[++i]);
        }
        // vnode is initialized if make_unique didnt returned nullptr
        // now we need to add a new-made Vnode of input port path[i] to vnode->next vector
        for(size_t j = i; j < path.size(); j++) {
            vnode->next.push_back(std::make_unique<Vnode>(this, path[j], vnode));
            vnode = vnode->next[vnode->next.size()-1].get();
        }
        // vnode is a leaf
        dst[vnode->device->id] = vnode;
    }
}

Vnode::Vnode(Vlink* vlink, int portId, Vnode* prev)
    : config(vlink->config), vl(vlink),
      device(vlink->config->getDevice(vlink->config->portDevice(portId))),
      prev(prev),
      in(prev != nullptr ? device->getPort(portId) : nullptr),
      outPrev(in != nullptr ? in->outPrev : -1), e2e()
{
    in->vnodes[vl->id] = this;
}

// for source end system
Vnode::Vnode(Vlink* vlink, int srcId)
    : config(vlink->config), vl(vlink),
      device(vlink->config->getDevice(srcId)),
      prev(nullptr), in(nullptr), outPrev(-1), e2e()
{}

Vnode* Vnode::selectNext(int portId) const {
    for(const auto &own: next) {
        if(own->in->id == portId) {
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

Error VlinkConfig::buildTables(bool print) {
    if(scheme == "OQ") {
        return Error::Success;
    }
    assert(scheme == "CIOQ");
    for(auto device: getAllDevices()) {
        if(device->type == Device::End) {
            continue;
        }
        device->cioqMap = std::make_unique<CioqMap>(device);
        generateTableBasic(device, n_queues, n_fabrics, print);
    }
    return Error::Success;
}

Error VlinkConfig::buildDelayTasks() {
    if(scheme == "OQ") {
        return _buildDelayTasksOQ();
    } else {
        assert(scheme == "CIOQ");
        return _buildDelayTasksCIOQ();
    }
}

Error VlinkConfig::_buildDelayTasksCIOQ() {
    // create QRTA object for every independent component and every output port of every switch
    for(auto device: getAllDevices()) {
        if(device->type == Device::End) {
            continue;
        }
        for(const auto& compOwn: device->cioqMap->comps) {
            auto comp = compOwn.get();
            device->qrtas[{Device::F, comp->id}] = std::make_unique<QRTA>(this);
        }
        for(const auto& out_port_in: device->getAllOutPortsIn()) {
            int out_port_pseudo_id = out_port_in->id;
            device->qrtas[{Device::P, out_port_pseudo_id}] = std::make_unique<QRTA>(this);
        }
    }

    // create DelayTasks objects
    for(auto device_next: getAllDevices()) {
        for(auto port: device_next->getAllPorts()) {
            for(auto vnode_next: port->getAllVnodes()) {
                auto vnode = vnode_next->prev;
                auto device = vnode->device;
                // device is either a SOURCE end system or a switch
                int out_pseudo_id = vnode_next->in->id;
                QRTA* qrta_p = nullptr;
                if(device->type == Device::Switch) {
                    int in_id = vnode->in->id;
                    assert(device->hasVlinks(in_id, out_pseudo_id));
                    auto found1 = device->cioqMap->compsIndex.find({in_id, out_pseudo_id});
                    assert(found1 != device->cioqMap->compsIndex.end());
                    auto comp = found1->second;
                    auto found2 = device->qrtas.find({Device::F, comp->id});
                    assert(found2 != device->qrtas.end());
                    QRTA* qrta_f = found2->second.get();
                    vnode->delayTasks[{Device::F, out_pseudo_id}] =
                            std::make_unique<DelayTask>(vnode->vl, vnode_next, Device::F, qrta_f);
                    n_tasks++;
//                    printf("created delayTask type F, vl %d, psout %d, device %d\n",
//                           vnode->vl->id, out_pseudo_id, vnode->device->id);
                    qrta_p = device->qrtas[{Device::P, out_pseudo_id}].get();
                }
                assert(vnode != nullptr);
                assert(vnode->delayTasks.find({Device::P, out_pseudo_id}) == vnode->delayTasks.end());
                vnode->delayTasks[{Device::P, out_pseudo_id}] =
                        std::make_unique<DelayTask>(vnode->vl, vnode_next, Device::P, qrta_p);
                n_tasks++;
//                printf("created delayTask type P, vl %d, psout %d, device %d\n",
//                       vnode->vl->id, out_pseudo_id, vnode->device->id);
            }
        }
    }

    // fill data dependencies between DelayTasks objects (in inputs fields)
    for(auto device: getAllDevices()) {
        for(auto port: device->getAllPorts()) {
            for(auto vnode: port->getAllVnodes()) {
                for(const auto& vnode_next_own: vnode->next) {
                    // device is a switch
                    assert(device->type == Device::Switch);
                    auto vnode_next = vnode_next_own.get();
                    int in_id = vnode->in->id;
                    int out_pseudo_id = vnode_next->in->id;
                    auto delayTask_f = vnode->delayTasks[{Device::F, out_pseudo_id}].get();
                    auto comp = device->cioqMap->compsIndex[{in_id, out_pseudo_id}];
                    // get all vl branches through this switch in this independent component
                    for(auto [cur_in_id, cur_out_pseudo_id]: comp->edges) {
                        for(auto cur_vnode: device->getVlinks(cur_in_id, cur_out_pseudo_id)) {
                            assert(cur_vnode->in->id == cur_in_id); // DEBUG
                            auto found = cur_vnode->prev->delayTasks.find({Device::P, cur_in_id});
                            assert(found != cur_vnode->prev->delayTasks.end());
                            auto curDelayTaskPrev = found->second.get();
                            assert(curDelayTaskPrev != nullptr); // DEBUG
                            delayTask_f->inputs[{curDelayTaskPrev->vl->id, cur_out_pseudo_id}] = curDelayTaskPrev;
//                            printf("F-task vl %d, in %d, out %d, inputs[vl %d, out %d] = P-task vl %d, in %d, out %d\n",
//                                   delayTask_f->vl->id, delayTask_f->in_id,
//                                   vnode->config->connectedPort(delayTask_f->out_pseudo_id),
//                                   curDelayTaskPrev->vl->id, vnode->config->connectedPort(cur_out_pseudo_id),
//                                   curDelayTaskPrev->vl->id, curDelayTaskPrev->in_id,
//                                   vnode->config->connectedPort(curDelayTaskPrev->out_pseudo_id)); // DEBUG;
                        }
                    }

                    auto delayTask_p = vnode->delayTasks[{Device::P, out_pseudo_id}].get();
                    auto out_port_in = device->fromOutPortByPseudoId(out_pseudo_id);
                    // get all vls through this switch and its output port out_pseudo_id
                    for(auto cur_vnode_next: out_port_in->getAllVnodes()) {
                        auto cur_vnode = cur_vnode_next->prev;
                        auto found = cur_vnode->delayTasks.find({Device::F, out_pseudo_id});
                        assert(found != cur_vnode->delayTasks.end());
                        auto curDelayTaskPrev = found->second.get();
                        assert(curDelayTaskPrev != nullptr); // DEBUG
                        delayTask_p->inputs[{curDelayTaskPrev->vl->id, out_pseudo_id}] = curDelayTaskPrev;
//                        printf("P-task vl %d, in %d, out %d, inputs[vl %d, out %d] = F-task vl %d, in %d, out %d\n",
//                               delayTask_p->vl->id, delayTask_p->in_id,
//                               vnode->config->connectedPort(delayTask_p->out_pseudo_id),
//                               curDelayTaskPrev->vl->id, vnode->config->connectedPort(out_pseudo_id),
//                               curDelayTaskPrev->vl->id, curDelayTaskPrev->in_id,
//                               vnode->config->connectedPort(curDelayTaskPrev->out_pseudo_id)); // DEBUG;
                    }
                }
            }
        }
    }
//    printf("filled inputs of delaytasks!\n");

    // fill data dependencies between DelayTasks objects (in outputs fields)
    for(auto device: getAllDevices()) {
        for(auto port: device->getAllPorts()) {
            for(auto vnode: port->getAllVnodes()) {
                for(const auto& vnode_next_own: vnode->next) {
                    // device is a switch
                    assert(device->type == Device::Switch);
                    auto vnode_next = vnode_next_own.get();
                    int out_pseudo_id = vnode_next->in->id;
                    auto delayTask_f = vnode->delayTasks[{Device::F, out_pseudo_id}].get();
                    for(auto [_, curDelayTask]: delayTask_f->inputs) {
                        curDelayTask->output_for[{delayTask_f->vl->id, delayTask_f->out_pseudo_id}] = delayTask_f;
                    }
                    auto delayTask_p = vnode->delayTasks[{Device::P, out_pseudo_id}].get();
                    for(auto [_, curDelayTask]: delayTask_p->inputs) {
                        curDelayTask->output_for[{delayTask_p->vl->id, delayTask_p->out_pseudo_id}] = delayTask_p;
                    }
                }
            }
        }
    }
    return Error::Success;
}

Error VlinkConfig::_buildDelayTasksOQ() {
    // create QRTA object for every output port of every switch
    for(auto device: getAllDevices()) {
        if(device->type == Device::End) {
            continue;
        }
        for(const auto& out_port_in: device->getAllOutPortsIn()) {
            int out_port_pseudo_id = out_port_in->id;
            device->qrtas[{Device::P, out_port_pseudo_id}] = std::make_unique<QRTA>(this);
        }
    }

    // create DelayTasks objects
    for(auto device_next: getAllDevices()) {
        for(auto port: device_next->getAllPorts()) {
            for(auto vnode_next: port->getAllVnodes()) {
                auto vnode = vnode_next->prev;
                auto device = vnode->device;
                // device is either a SOURCE end system or a switch
//                printf("device %d, psout %d\n", vnode->device->id, vnode_next->in->id);
                int out_pseudo_id = vnode_next->in->id;
                QRTA* qrta_p = nullptr;
                if(device->type == Device::Switch) {
                    int in_id = vnode->in->id;
                    assert(device->hasVlinks(in_id, out_pseudo_id));
                    qrta_p = device->qrtas[{Device::P, out_pseudo_id}].get();
                }
                assert(vnode != nullptr);
                assert(vnode->delayTasks.find({Device::P, out_pseudo_id}) == vnode->delayTasks.end());
                vnode->delayTasks[{Device::P, out_pseudo_id}] =
                        std::make_unique<DelayTask>(vnode->vl, vnode_next, Device::P, qrta_p);
                n_tasks++;
//                printf("created delayTask type P, vl %d, psout %d, device %d\n",
//                       vnode->vl->id, out_pseudo_id, vnode->device->id);
            }
        }
    }

    // fill data dependencies between DelayTasks objects (in inputs fields)
    for(auto device: getAllDevices()) {
        for(auto port: device->getAllPorts()) {
            for(auto vnode: port->getAllVnodes()) {
                for(const auto& vnode_next_own: vnode->next) {
                    // device is a switch
                    assert(device->type == Device::Switch);
                    auto vnode_next = vnode_next_own.get();
                    int out_pseudo_id = vnode_next->in->id;
                    auto delayTask_p = vnode->delayTasks[{Device::P, out_pseudo_id}].get();
                    auto out_port_in = device->fromOutPortByPseudoId(out_pseudo_id);
                    // get all vls through this switch and its output port out_pseudo_id
                    for(auto cur_vnode_next: out_port_in->getAllVnodes()) {
                        auto cur_vnode = cur_vnode_next->prev;
                        auto cur_vnode_prev = cur_vnode->prev;
                        assert(cur_vnode_prev != nullptr);
                        auto found = cur_vnode_prev->delayTasks.find({Device::P, cur_vnode->in->id});
                        assert(found != cur_vnode->delayTasks.end());
                        auto curDelayTaskPrev = found->second.get();
                        assert(curDelayTaskPrev != nullptr); // DEBUG
                        delayTask_p->inputs[{curDelayTaskPrev->vl->id, out_pseudo_id}] = curDelayTaskPrev;
//                        printf("P-task vl %d, in %d, out %d, inputs[vl %d, out %d] = P-task vl %d, in %d, out %d\n",
//                               delayTask_p->vl->id, delayTask_p->in_id,
//                               vnode->config->connectedPort(delayTask_p->out_pseudo_id),
//                               curDelayTaskPrev->vl->id, vnode->config->connectedPort(out_pseudo_id),
//                               curDelayTaskPrev->vl->id, curDelayTaskPrev->in_id,
//                               vnode->config->connectedPort(curDelayTaskPrev->out_pseudo_id)); // DEBUG;
                    }
                }
            }
        }
    }
//    printf("filled inputs of delaytasks!\n");

    // fill data dependencies between DelayTasks objects (in outputs fields)
    for(auto device: getAllDevices()) {
        for(auto port: device->getAllPorts()) {
            for(auto vnode: port->getAllVnodes()) {
                for(const auto& vnode_next_own: vnode->next) {
                    // device is a switch
                    assert(device->type == Device::Switch);
                    auto vnode_next = vnode_next_own.get();
                    int out_pseudo_id = vnode_next->in->id;
                    auto delayTask_p = vnode->delayTasks[{Device::P, out_pseudo_id}].get();
                    for(auto [_, curDelayTask]: delayTask_p->inputs) {
                        curDelayTask->output_for[{delayTask_p->vl->id, delayTask_p->out_pseudo_id}] = delayTask_p;
                    }
                }
            }
        }
    }
    return Error::Success;
}

Error VlinkConfig::buildTasksOrder() {
    // for all DelayTasks fill in_cycle values,
    // build a set of delay tasks with false in_cycle values ("acyclic" tasks),
    // and save the order of filling false in_cycle values (this will be the delay computation order among acyclic delay tasks).
    std::vector<DelayTask*> tasksToVisit;
    std::vector<DelayTask*> tasksToVisitNext;
    std::set<std::tuple<int, int, Device::elem_t>> tasksToVisitSet;
    size_t n_visited = 0;
    std::vector<DelayTask*> acyclicTasksOrder;
    std::set<std::tuple<int, int, Device::elem_t>> acyclicTasksSet;
    // fetching all delay tasks without inputs
    for(auto vl: getAllVlinks()) {
        auto vnode = vl->src.get();
        for(const auto& vnode_next_own: vnode->next) {
            auto vnode_next = vnode_next_own.get();
            int out_pseudo_id = vnode_next->in->id;
            auto delayTask = vnode->delayTasks[{Device::P, out_pseudo_id}].get();
            assert(delayTask->inputs.empty());
            tasksToVisitSet.insert(delayTask->id);
            tasksToVisit.push_back(delayTask);
        }
    }
    while(!tasksToVisit.empty()) {
        size_t acyclic_size_frozen = acyclicTasksOrder.size();
        for(auto delayTask: tasksToVisit) {
            if(!delayTask->in_cycle) {
                continue;
            }
            bool has_inputs_in_cycle = false;
            for(auto[_, curDelayTask]: delayTask->inputs) {
                if(curDelayTask->in_cycle) {
                    has_inputs_in_cycle = true;
                    break;
                }
            }
            if(!has_inputs_in_cycle) {
                delayTask->in_cycle = false;
                delayTask->cyclic_layer = 0;
                acyclicTasksSet.insert(delayTask->id);
                acyclicTasksOrder.push_back(delayTask);
            }
            n_visited++;
        }
        if(acyclicTasksOrder.size() == acyclic_size_frozen) {
            break;
        }
        for(auto delayTask: acyclicTasksOrder) {
            for(auto[_, curDelayTask]: delayTask->output_for) {
                if(curDelayTask->in_cycle) {
                    tasksToVisitSet.insert(curDelayTask->id);
                    tasksToVisitNext.push_back(curDelayTask);
                }
            }
        }
        tasksToVisit = std::move(tasksToVisitNext);
        tasksToVisitNext.clear();
    }
    bool acyclic = tasksToVisitSet.size() <= acyclicTasksOrder.size();
    if(!acyclic) {
//        printf("%zu delay tasks found, but only %zu of them are not cyclic dependent!\n", tasksToVisitSet.size(), acyclicTasksOrder.size());
    } else {
        assert(tasksToVisitSet.size() == acyclicTasksOrder.size());
//        printf("%zu delay tasks found, no cyclic dependencies\n", tasksToVisit.size());
    }

//    printf("acyclic tasks order:\n"); // DEBUG
//    for(auto delayTask: acyclicTasksOrder) {
//        printf("task vl=%d to %d (%s)\n",
//               delayTask->vl->id, delayTask->out_pseudo_id, delayTask->elem == Device::F ? "F" : "D"); // DEBUG
//    }
//    printf("\n"); // DEBUG/

    this->tasks = tasksToVisit;
    this->acyclicTasksOrder = acyclicTasksOrder;
    if(acyclic) {
        return Error::Success;
    }

    // build a set of delay tasks with in_cycle=true ("cyclic" tasks), and label each cyclic task with
    // minimum hop distance to subgraph of acyclic tasks (cyclic_layer value)
    std::vector<DelayTask*> cyclicTasksToVisit;
    std::set<std::tuple<int, int, Device::elem_t>> cyclicTasksToVisitSet;
    for(auto delayTask: acyclicTasksOrder) {
        for(auto [_, curDelayTask]: delayTask->output_for) {
            if(curDelayTask->in_cycle) {
                if(cyclicTasksToVisitSet.find(curDelayTask->id) == cyclicTasksToVisitSet.end()) {
                    cyclicTasksToVisitSet.insert(curDelayTask->id);
                    cyclicTasksToVisit.push_back(curDelayTask);
                }
            }
        }
    }
    int cyclic_layer = 1;
    n_visited = 0;
    while(n_visited < cyclicTasksToVisit.size()) {
        size_t size_frozen = cyclicTasksToVisit.size();
        for(size_t i = n_visited; i < size_frozen; i++) {
            auto delayTask = cyclicTasksToVisit[i];
            delayTask->cyclic_layer = cyclic_layer;
            for(auto[_, curDelayTask]: delayTask->output_for) {
                if(curDelayTask->in_cycle &&
                    cyclicTasksToVisitSet.find(curDelayTask->id) == cyclicTasksToVisitSet.end())
                {
                    cyclicTasksToVisitSet.insert(curDelayTask->id);
                    cyclicTasksToVisit.push_back(curDelayTask);
                }
            }
            n_visited++;
        }
        cyclic_layer += 1;
    }

    // assertions about acyclic and cyclic tasks sets
//    printf("%d; %lu, %lu, %lu, %lu, %lu, %lu\n",
//           n_tasks,
//           tasksToVisit.size(), tasksToVisitSet.size(),
//           acyclicTasksOrder.size(), acyclicTasksSet.size(),
//           cyclicTasksToVisit.size(), cyclicTasksToVisitSet.size());
    assert(cyclicTasksToVisitSet.size() == cyclicTasksToVisit.size());

    for(auto delayTask: acyclicTasksOrder) {
        assert(!delayTask->in_cycle);
        assert(delayTask->cyclic_layer == 0);
        bool has_cyclic_inputs = false;
        for(auto [_, curDelayTask]: delayTask->inputs) {
            assert(curDelayTask->cyclic_layer >= 0);
            if(curDelayTask->in_cycle) {
                has_cyclic_inputs = true;
            }
        }
        assert(!has_cyclic_inputs);
    }

    for(auto delayTask: cyclicTasksToVisit) {
        assert(delayTask->in_cycle);
        assert(delayTask->cyclic_layer > 0);
        bool has_cyclic_inputs = false;
        for(auto [_, curDelayTask]: delayTask->inputs) {
            assert(curDelayTask->cyclic_layer >= 0);
            if(curDelayTask->in_cycle) {
                has_cyclic_inputs = true;
                break;
            }
        }
        assert(has_cyclic_inputs);
    }
    assert(acyclicTasksOrder.size() == acyclicTasksSet.size());
    assert(cyclicTasksToVisitSet.size() + acyclicTasksSet.size() == static_cast<uint64_t>(n_tasks));
//    printf("%lu + %lu = %lu >= %lu ?\n",
//           cyclicTasksToVisitSet.size(), acyclicTasksSet.size(),
//           cyclicTasksToVisitSet.size() + acyclicTasksSet.size(), tasksToVisitSet.size());

    // label each cyclic task with maximum cyclic layer among its input tasks (max_input_layer),
    // and sort cyclic tasks by max_input_layer
    for(auto delayTask: cyclicTasksToVisit) {
        int max_layer = -1;
        for(auto [_, curDelayTask]: delayTask->inputs) {
            if(curDelayTask->cyclic_layer > max_layer) {
                max_layer = curDelayTask->cyclic_layer;
            }
        }
        assert(max_layer > -1);
        delayTask->max_input_layer = max_layer;
    }
    std::sort(cyclicTasksToVisit.begin(), cyclicTasksToVisit.end(),
    [](DelayTask* a, DelayTask* b) -> bool { return a->max_input_layer < b->max_input_layer; } );

    this->cyclicTasksOrder = cyclicTasksToVisit;

//    printf("acyclic tasks order:\n"); // DEBUG
//    for(auto delayTask: this->cyclicTasksOrder) {
//        printf("task vl=%d to %d (%s)\n",
//               delayTask->vl->id, delayTask->out_pseudo_id, delayTask->elem == Device::F ? "F" : "P"); // DEBUG
//    }
//    printf("\n"); // DEBUG

//    printf("cyclic tasks order:\n"); // DEBUG
//    for(auto delayTask: cyclicTasksToVisit) {
//        printf("task vl=%d to %d (%s), layer=%d, max_input_layer=%d\n",
//               delayTask->vl->id, delayTask->out_pseudo_id, delayTask->elem == Device::F ? "F" : "P",
//               delayTask->cyclic_layer, delayTask->max_input_layer); // DEBUG
//    }
//    printf("\n"); // DEBUG
    return Error::Success;
}

Error VlinkConfig::calcDelays(bool print) {
    buildDelayTasks();
    buildTasksOrder();

    // calculate all final minimum delay estimates and preliminary maximum delay/jitter estimates
    for(auto vl: getAllVlinks()) {
        std::vector<Vnode*> vnodes_order; // breadth-first
        auto vnode = vl->src.get();
        vnodes_order.push_back(vnode);
        size_t n_visited = 0;
        while(n_visited < vnodes_order.size()) {
            size_t size_frozen = vnodes_order.size();
            for(size_t i = n_visited; i < size_frozen; i++) {
                auto cur_vnode = vnodes_order[i];
                for(const auto& vnode_next_own: cur_vnode->next) {
                    auto vnode_next = vnode_next_own.get();
                    vnodes_order.push_back(vnode_next);
//                    printf("vl %d: add vnode in device %d to queue: size = %lu\n", vl->id, vnode_next->device->id, vnodes_order.size());
                    std::vector<Device::elem_t> elem_types;
                    if(scheme == "CIOQ") {
                        elem_types = {Device::F, Device::P};
                    } else if(scheme == "OQ") {
                        elem_types = {Device::P};
                    } else {
                        assert(false);
                    }
                    for(auto elem: elem_types) {
                        auto found = cur_vnode->delayTasks.find({elem, vnode_next->in->id});
                        if(found != cur_vnode->delayTasks.end()) {
                            auto delayTask = found->second.get();
                            Error err = delayTask->calc_delay_init();
                            if(err) {
                                return err;
                            }
                            if(print) {
//                                printf("init:   vl %d to port %d: dmin=%ld, prelim jit=%ld\n", vl->id, vnode_next->in->id,
//                                       delayTask->delay.dmin(), delayTask->delay.jit()); // DEBUG
                            }
                        } else {
                            if(elem == Device::F) {
                                assert(cur_vnode->device->type == Device::End);
                            } else {
                                assert(false);
                            }
                        }
                    }
                }
                n_visited++;
            }
        }
    }
//    printf("calculating MIN delays -- DONE\n");

    // calculating max delays that are computable in one iteration
    for(auto delayTask: acyclicTasksOrder) {
//        printf("get input data...\n");
        delayTask->get_input_data();
//        printf("get input data done\n");
//        printf("calc delay max...\n");
        Error err = delayTask->calc_delay_max();
//        printf("calc delay max done\n");
        if(err) {
            return err;
        }
        delayTask->iter++;
//        if(print) {
//            printf("acyclic: vl %d to port %d (%s): dmin=%ld, prelim jit=%ld\n", delayTask->vl->id, delayTask->out_pseudo_id,
//                   delayTask->elem == Device::F ? "F" : "P", delayTask->delay.dmin(), delayTask->delay.jit());
//        }
    }
//    printf("calculating acyclic tasks -- DONE\n");

    uint64_t n_iter = 0;
    // calculating the rest of max delays iteratively, if there are cyclic data dependencies
    if(!cyclicTasksOrder.empty()) {
        int64_t sum = 0;
        int64_t sum_pre = -1;
        printf("some delay calculation subtasks have cyclic data dependency,\nthey will be calculated iteratively.\n");
        while(sum_pre < sum && n_iter < cyclicMaxIter) {
            printf("iteration %lu... (interrupt after %lu)\n", n_iter+1, cyclicMaxIter);
            sum_pre = sum;
            sum = 0;
            for(auto delayTask: cyclicTasksOrder) {
                delayTask->clear_bp();
            }
            for(auto delayTask: cyclicTasksOrder) {
                delayTask->get_input_data();
                Error err = delayTask->calc_delay_max();
                if(err) {
                    return err;
                }
//                if(print) {
//                    printf("cyclic:  vl %d to port %d (%s): dmin=%ld, prelim jit=%ld [iter %d]\n",
//                           delayTask->vl->id, delayTask->out_pseudo_id,
//                           delayTask->elem == Device::F ? "F" : "P",
//                           delayTask->delay.dmin(), delayTask->delay.jit(), delayTask->iter);
//                }
                sum += delayTask->delay.jit();
            }
            n_iter++;
            assert(sum_pre <= sum);
        }
        if(sum_pre < sum) {
            std::string verbose =
                    std::string("iterative calculation of delays with cyclic data dependency took too much iterations (over ") +
                    std::to_string(cyclicMaxIter) +
                    "), maybe it's divergent";
            return Error(Error::CyclicTooLong, verbose);
        }
    }
//    printf("calculating cyclic tasks -- DONE\n");

    for(auto vl: getAllVlinks()) {
        for(auto [_, vnode]: vl->dst) {
            vnode->e2e = vnode->prev->delayTasks[{Device::P, vnode->in->id}].get()->delay;
            DelayData e2e = vnode->e2e;
            if(print) {
                printf("VL %d to %d: maxDelay = %li lB (%.0f us), jit = %li lB (%.0f us), minDelay = %li lB (%.0f us)\n",
                       vnode->vl->id, vnode->device->id,
                       e2e.dmax(), linkByte2ms(e2e.dmax()) * 1e3,
                       e2e.jit(), linkByte2ms(e2e.jit()) * 1e3,
                       e2e.dmax() - e2e.jit(), linkByte2ms(e2e.dmax() - e2e.jit()) * 1e3);
            }
        }
    }
    if(print) {
        printf("\n");
    }
//    printf("obtaining E2E delay values -- DONE\n");
    assert(cyclicTasksOrder.empty() == (n_iter == 0));
    printf("Calculated %d local delays, %lu without cyclic data dependencies and %lu with cyclic data dependencies.\n",
           n_tasks, acyclicTasksOrder.size(), cyclicTasksOrder.size());
    if(n_iter > 0) {
        printf("There were cyclic data dependencies between local delay calculation subtasks,\n  but those subtasks were calculated in %lu iterations.\n",
               n_iter);
    } else {
        printf("There were no cyclic data dependencies between local delay calculation subtasks.\n");
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

VlinkConfig::VlinkConfig(): scheme("CIOQ"), n_tasks(0) {}

std::map<int, double> VlinkConfig::bwUsage() {
    std::map<int, double> res;
    for(auto device: getAllDevices()) {
        for(auto port: device->getAllPorts()) {
            res[port->id] = port->bwUsage();
        }
    }
    return res;
}

void DelayTask::clear_bp() {
    qrta->clear_bp();
}

void DelayTask::get_input_data() {
    std::map<std::pair<int, int>, DelayData> input_data;
    if(inputs.empty()) {
        return;
    }
    for(auto[vlBranch, delaytask]: inputs) {
        assert(delaytask != nullptr);
        input_data[vlBranch] = delaytask->delay;
    }
    assert(qrta != nullptr);
    qrta->setInDelays(input_data);
}

// calculate real delay_min and initial version of delay_max
Error DelayTask::calc_delay_init() {
    int64_t dmin, dmax;
    if(inputs.empty()) {
        // source end system
        dmin = vl->smin;
        dmax = vl->smax + vl->jit0b;
    } else {
//        printf("i am vl %d psout %d (%s), find input for vl %d to psout %d. inputs are:\n", vl->id, out_pseudo_id,
//               elem == Device::F ? "F" : "P", vl->id, out_pseudo_id);
//        for(auto [vlBranch, curDelayTask]: inputs) {
//            auto [cur_vl_id, cur_out_psid] = vlBranch; // DEBUG
//            printf("vl %d to psout %d,\n", cur_vl_id, cur_out_psid); // DEBUG
//        }
        auto found = inputs.find({vl->id, out_pseudo_id});
        assert(found != inputs.end());
        auto prevDelayTask = found->second;
        dmin = prevDelayTask->delay.dmin() + vl->smin;
        dmax = prevDelayTask->delay.dmax() + vl->smax;
    }
    delay = DelayData(vl, dmin, dmax-dmin);
    return Error::Success;
}

// after calc_delay_init is called for all DelayTasks
Error DelayTask::calc_delay_max() {
    int64_t dmin, dmax;
    dmin = delay.dmin();
    if(inputs.empty()) {
        dmax = vl->smax + vl->jit0b;
    } else {
        get_input_data();
        Error err = qrta->calc(vl, vnode_next->in->id); // TODO check if right second arg
        if(err) {
            return err;
        }
        assert(dmin == qrta->calc_result.dmin());
        dmax = qrta->calc_result.dmax();
    }
    delay = DelayData(vl, dmin, dmax-dmin);
    iter++;
    return Error::Success;
}

int CioqMap::getQueueId(int in_port_id, int out_port_pseudo_id) const {
    auto found = queueTable.find(in_port_id);
    assert(found != queueTable.end());
    auto found2 = found->second.find(out_port_pseudo_id);
    assert(found2 != found->second.end());
    return found2->second;
}

int CioqMap::getFabricId(int in_port_id, int queue_id) const {
    auto found = fabricTable.find({in_port_id, queue_id});
    assert(found != fabricTable.end());
    return found->second;
}

int CioqMap::getFabricIdByEdge(int in_port_id, int out_port_pseudo_id) const {
    return getFabricId(in_port_id, getQueueId(in_port_id, out_port_pseudo_id));
}

void CioqMap::setMap(const std::map<int, std::map<int, int>>& _queueTable, const std::map<std::pair<int, int>, int>& _fabricTable, bool print) {
    queueTable = _queueTable;
    fabricTable = _fabricTable;
    if(print) {
        printf("==== device %d CIOQ mapping (%d fabrics, %d queues per input port):\n", device->id, config->n_fabrics, config->n_queues);
        printf("==== queue mapping:\n");
        for(auto [key1, map1]: queueTable) {
            printf("\tinput port %d: \n", key1);
            for(auto [key2_pseudo, val]: map1) {
                int key2 = config->connectedPort(key2_pseudo);
                printf("output port %d -- queue %d\n", key2, val);
            }
        }
        printf("==== fabric mapping:\n");
        for(auto [key, val]: fabricTable) {
            auto [key1, key2] = key;
            printf("input port %d queue %d -- fabric %d\n", key1, key2, val);
        }
        printf("\n");
    }

    // build all comps (= independent components = components of fabric-induced subgraphs of the switch traffic graph)
    auto device_port_ids = device->getAllPortIds();
    for(auto in_id: device_port_ids) {
        for(int queue_id = 0; queue_id < n_queues; queue_id++) {
//            printf("in %d queue %d, find any output port from this input queue\n", in_id, queue_id);
            // find any output port from this input queue with any vlinks to this output port
            int out_pseudo_id = -1;
            for(auto cur_out_id: device_port_ids) {
                int cur_out_pseudo_id = config->connectedPort(cur_out_id);
//                printf("getQueueId(%d -> %d (non-pseudo: %d)) == queue %d ?\n", in_id, cur_out_pseudo_id, cur_out_id, queue_id); // DEBUG
                if(getQueueId(in_id, cur_out_pseudo_id) == queue_id &&
                    device->hasVlinks(in_id, cur_out_pseudo_id))
                {
                    out_pseudo_id = cur_out_pseudo_id;
                    break;
                }
            }
            if(out_pseudo_id == -1) {
                continue;
            }
//            printf("in %d queue %d, found out_pseudo_id = %d\n", in_id, queue_id, out_pseudo_id);
            auto found = compsIndex.find({in_id, out_pseudo_id});
            if(found != compsIndex.end()) {
                continue;
            } else {
//                printf("no comps yet build with edge %d -> %d\n", in_id, out_pseudo_id);
            }
            auto comp_edges = buildComp(in_id, queue_id);
            if(comp_edges.empty()) {
                continue;
            }
            comps.push_back(std::make_unique<PortsSubgraph>(comps.size(), comp_edges));
            PortsSubgraph* comp = comps[comps.size()-1].get();
//            printf("for i %d, j_ps %d (q %d): built comp %d\n", in_id, out_pseudo_id, queue_id, comp->id); // DEBUG
            std::set<std::pair<int, int>> comp_queues;
            for(auto edge: comp_edges) {
//                int fabric2 = getFabricIdByEdge(edge.first, edge.second);
//                printf("edge %d -> %d\n", edge.first, edge.second); // DEBUG
                assert(compsIndex.find(edge) == compsIndex.end());
                // DEBUG:
//                if(compsIndex.find(edge) != compsIndex.end()) {
//                    int fabric1;
//                    auto comp_other = compsIndex.find(edge)->second;
//                    printf("this edge is intersecting with comp %d\n", comp_other->id);
//                    for(auto edge1: comp_other->edges) {
//                        fabric1 = getFabricIdByEdge(edge1.first, edge1.second);
//                        break;
//                    }
//                    assert(fabric1 == fabric2);
//                }
                compsIndex[edge] = comp;
            }
        }
    }

    // DEBUG assertions about compsIndex
    for(auto in_id: device_port_ids) {
        for(auto out_id: device_port_ids) {
//            printf("cycle iteration %d %d\n", in_id, out_id);
            int out_pseudo_id = config->connectedPort(out_id);
//            printf("or %d %d\n", in_id, out_pseudo_id);
            if(compsIndex.find({in_id, out_pseudo_id}) == compsIndex.end()) {
//                printf("not found a comp for edge %d -> %d\n", in_id, out_pseudo_id); // DEBUG
                if(device->hasVlinks(in_id, out_pseudo_id)) {
//                    printf("building comp for %d -> %d\n", in_id, out_pseudo_id); // DEBUG
                    auto comp_edges = buildComp(in_id, getQueueId(in_id, out_pseudo_id));
//                    printf("for i %d, j_ps %d: built comp (again)\n", in_id, out_pseudo_id); // DEBUG
//                    for(auto edge: comp_edges) {
//                        printf("edge %d -> %d\n", edge.first, edge.second); // DEBUG
//                    }
                    assert(comp_edges.empty());
                }
            } else {
//                printf("found a comp for edge %d -> %d\n", in_id, out_pseudo_id); // DEBUG
            }
        }
    }
}

std::set<std::pair<int, int>> CioqMap::buildComp(int in_port_id, int queue_id) const {
    std::map<int, bool> in_port_ids_seen;
    std::map<int, bool> out_port_ids_seen;
    std::set<std::pair<int, int>> edges;
    int fabric_id = getFabricId(in_port_id, queue_id);
    auto device_in_ids = device->getAllPortIds();
    auto device_out_pseudo_ids = device->getAllOutPortPseudoIds();

    in_port_ids_seen[in_port_id] = false;
    bool has_unseen = true;
    while(has_unseen) {
        has_unseen = false;

        for(auto& it: in_port_ids_seen) {
            auto[in_id, in_seen] = it;
            if(in_seen) continue;
            for(auto out_id: device_out_pseudo_ids) {
                if(device->hasVlinks(in_id, out_id) &&
                    getFabricIdByEdge(in_id, out_id) == fabric_id)
                {
//                    if(edges.find({in_id, out_id}) == edges.end()) {
//                        printf("from input %d found edge %d -> %d\n", in_id, in_id, out_id);
//                    }
                    edges.insert({in_id, out_id});
                    auto found = out_port_ids_seen.find(out_id);
                    if(found == out_port_ids_seen.end()) {
                        out_port_ids_seen[out_id] = false;
                        has_unseen = true;
                    }
                }
            }
            it.second = true;
        }

        for(auto& it: out_port_ids_seen) {
            auto[out_id, out_seen] = it;
            if(out_seen) continue;
            for(auto in_id: device_in_ids) {
                if(device->hasVlinks(in_id, out_id) &&
                    getFabricIdByEdge(in_id, out_id) == fabric_id)
                {
//                    if(edges.find({in_id, out_id}) == edges.end()) {
//                        printf("from output %d found edge %d -> %d\n", out_id, in_id, out_id);
//                    }
                    edges.insert({in_id, out_id});
                    auto found = in_port_ids_seen.find(in_id);
                    if(found == in_port_ids_seen.end()) {
                        in_port_ids_seen[in_id] = false;
                        has_unseen = true;
                    }
                }
            }
            it.second = true;
        }
    }
    for(auto[id, seen]: in_port_ids_seen) {
        assert(seen); // DEBUG
    }
    for(auto[id, seen]: out_port_ids_seen) {
        assert(seen); // DEBUG
    }
    return edges;
}

void generateTableBasic(Device* device, int n_queues, int n_fabrics, bool print) {
    assert(n_queues == 2);
    assert(n_fabrics % n_queues == 0);

    auto in_ports_ids = device->getAllPortIds();
    int n_ports = in_ports_ids.size();

    std::map<int, std::map<int, int>> queueTable;
    std::map<std::pair<int, int>, int> fabricTable;

    for(int i = 0; i < n_ports; i++) {
        int in_port_id = in_ports_ids[i];
        // out port -> queue id
        std::map<int, int> portQueueTable;
        for(int queueId = 0; queueId < n_queues; queueId++) {
            int fabricId = i % n_fabrics + queueId * (1 - ((i % n_fabrics) % n_queues) * n_queues);
            assert(fabricId >= 0);
            assert(fabricId < n_fabrics);
            fabricTable[{in_port_id, queueId}] = fabricId;
        }
        for(int j = 0; j < n_ports; j++) {
            int out_port_id = in_ports_ids[j];
            int out_port_pseudo_id = device->config->connectedPort(out_port_id);
            int queueId = j % 2;
            assert(queueId >= 0);
            assert(queueId < n_queues);
            portQueueTable[out_port_pseudo_id] = queueId;
        }
        queueTable[in_port_id] = portQueueTable;
    }
    device->cioqMap->setMap(queueTable, fabricTable, print);
}

bool PortsSubgraph::isConnected(int node_in, int node_out) const {
    auto found = edges.find({node_in, node_out});
    return (found != edges.end());
}

int64_t QRTA::busyPeriod(const std::map<std::pair<int,int>, DelayData>& inDelays, VlinkConfig* config) {
    uint64_t it = 1;
    int64_t bp = 1;
    int64_t bpPrev = 0;
    for(; bp != bpPrev; it++) {
        bpPrev = bp;
        bp = 0;
        for(auto [vlBranch, delay]: inDelays) {
            auto vl = delay.vl();
            bp += numPackets(bpPrev, vl->bagB, delay.jit()) * vl->smax;
        }
        if(config->bpMaxIter != 0 && it >= config->bpMaxIter) {
            return -1;
        }
    }
    return bp;
}

// == Rk,j(t) - Jk, k == curVlId
int64_t QRTA::delayFunc(int64_t t, Vlink* curVl, int curBranchId) const {
    int64_t res = -t;
    for(auto [vlBranch, delay]: inDelays) {
        auto [vlId, branchId] = vlBranch;
        auto vl = delay.vl();
        assert(vl->id == vlId);
        bool cur = (vl->id == curVl->id) && (branchId == curBranchId);
        res += numPacketsUp(t, vl->bagB, (!cur) * delay.jit()) * vl->smax;
    }
    return res;
}

// == Rk,j(q)* - Jk, k == curVlId
int64_t QRTA::delayFuncRem(int q, Vlink* curVl, int curBranchId) const {
    int64_t value = 0;
    int64_t bags = (q - 1) * curVl->bagB;
    for(auto [vlBranch, delay]: inDelays) {
        auto [vlId, branchId] = vlBranch;
        auto vl = delay.vl();
        assert(vl->id == vlId);
        bool cur = (vl->id == curVl->id) && (branchId == curBranchId);
        value += vl->smax * (
                !cur
                ? numPacketsUp(std::min(bp - curVl->smax, bags), vl->bagB, delay.jit())
                : q);
    }
    return std::min(bp, value) - bags;
}

Error QRTA::clear_bp() {
    bp = -1;
    return Error::Success;
}

Error QRTA::calc_bp() {
    if(bp < 0) {
        double rate_ratio = total_rate();
        if(rate_ratio >= 1.) {
            std::string verbose =
                    "QRTA preconditions are not met: concurring VLs total speed exceeds speed of a link ("
                    + std::to_string(rate_ratio)
                    + " times bigger)";
            return Error(Error::BpEndless, verbose);
        }
        bp = busyPeriod(inDelays, config);
        if(bp < 0) {
            std::string verbose =
                    "QRTA calculation of busy period is converging too long (over "
                    + std::to_string(config->bpMaxIter)
                    + "iterations), concurring VLs total speed to a link speed ratio is "
                    + std::to_string(rate_ratio);
            return Error(Error::BpTooLong, verbose);
        }
    }
    return Error::Success;
}

Error QRTA::calc(Vlink* curVl, int curBranchId) {
    Error err = calc_bp();
    if(err) {
        return err;
    }

    int64_t delayFuncMax = -1;
    int64_t delayFuncValue;

    // calc delayFunc in chosen points, part 1
    for(int64_t t = 0; t <= bp - curVl->smax; t += curVl->bagB) {
        delayFuncValue = delayFunc(t, curVl, curBranchId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }

    // calc delayFunc in chosen points, part 2
    DelayData curDelay;
    bool curDelay_filled = false;
    for(auto [vlBranch, delay]: inDelays) {
        auto[vlId, branchId] = vlBranch;
        if(vlId == curVl->id && branchId == curBranchId) {
            curDelay = delay;
            curDelay_filled = true;
            continue;
        }
        auto vl = delay.vl();
        for(int64_t t = roundToMultiple(delay.jit(), vl->bagB) - delay.jit();
            t <= bp - curVl->smax;
            t += vl->bagB)
        {
            delayFuncValue = delayFunc(t, curVl, curBranchId);
            if(delayFuncValue > delayFuncMax) {
                delayFuncMax = delayFuncValue;
            }
        }
    }
    assert(curDelay_filled);

    // calc delayFuncRem in chosen points
    int qMin = numPacketsUp(bp - curVl->smin, curVl->bagB, 0);
    int qMax = numPackets(bp, curVl->bagB, curDelay.jit());
    for(int q = qMin; q <= qMax; q++) {
        delayFuncValue = delayFuncRem(q, curVl, curBranchId);
        if(delayFuncValue > delayFuncMax) {
            delayFuncMax = delayFuncValue;
        }
    }
    assert(delayFuncMax >= 0);
    int64_t dmax = delayFuncMax + curDelay.dmax();
    int64_t dmin = curDelay.dmin() + curVl->smin;
    assert(dmax >= dmin);
    calc_result = DelayData(curVl, dmin, dmax-dmin);
    return Error::Success;
}

// sum BW of concurring virtual links / link rate
double QRTA::total_rate() {
    double s = 0;
    for(auto [vlBranch, delay]: inDelays) {
        auto vl = delay.vl();
        s += static_cast<double>(vl->smax) / vl->bagB;
    }
    return s;
}
