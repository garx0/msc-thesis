#include <iostream>
#include <cstdio>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
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

//Error Vnode::prepareTest(bool shuffle) {
//    if(cycleState == NotVisited) {
//        cycleState = VisitedNotPrepared;
//    } else if(cycleState == VisitedNotPrepared) {
//        std::string verbose =
//                std::string("cycle: entered VL ")
//                + std::to_string(vl->id)
//                + " device "
//                + std::to_string(device->id)
//                + " again. suggestion: delete VL "
//                + std::to_string(vl->id)
//                + " paths to end systems:";
//        std::string verboseRaw = std::to_string(vl->id);
//        for(auto dest: getAllDests()) {
//            verbose += " " + std::to_string(dest->device->id);
//            verboseRaw += " " + std::to_string(dest->device->id);
//        }
//        return Error(Error::Cycle, verbose, verboseRaw);
//    }
//    assert(prev != nullptr);
//    if(prev->prev != nullptr) {
//        Port* fromOutPort = prev->device->fromOutPort(outPrev);
//        auto vnodeNextList = fromOutPort->getAllVnodes();
//        auto order = idxRange(vnodeNextList.size(), shuffle);
//        for(auto idx: order) {
//            auto curVnode = vnodeNextList[idx]->prev;
//            if(curVnode->cycleState != Prepared) {
//                Error err = curVnode->prepareTest(shuffle);
//                if(err) {
//                    return err;
//                }
//            }
//        }
//    }
//    cycleState = Prepared;
//    return Error::Success;
//}

//Error Vnode::prepareCalc() {
//    assert(prev != nullptr);
//    Error err;
//    if(prev->prev == nullptr) {
//        err = in->delays->calc(vl, true);
//        if(err) {
//            return err;
//        }
//    } else {
//        std::map<int, DelayData> requiredDelays;
//        Port* fromOutPort = prev->device->fromOutPort(outPrev);
//        for(auto vnodeNext: fromOutPort->getAllVnodes()) {
//            auto curVnode = vnodeNext->prev;
//            if(!curVnode->calcPrepared) {
//                err = curVnode->prepareCalc();
//                if(err) {
//                    return err;
//                }
//            }
//            int vlId = curVnode->vl->id;
//            auto delay = curVnode->in->delays->getDelay(vlId);
//            assert(delay.ready());
//            in->delays->setInDelay(delay);
//        }
//        in->delays->setInputReady();
//        err = in->delays->calc(vl);
//        if(err) {
//            return err;
//        }
//    }
//    calcPrepared = true;
//    return Error::Success;
//}

//Error Vnode::calcDelays() {
//    Error err = prepareCalc();
//    if(!err) {
//        e2e = in->delays->e2e(vl->id);
//    }
//    return err;
//}

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

Error VlinkConfig::buildTables() {
    for(auto device: getAllDevices()) {
        if(device->type == Device::End) {
            continue;
        }
        device->cioqMap = std::make_unique<CioqMap>(device);
        generateTableBasic(device, n_queues, n_fabrics);
    }
    return Error::Success;
}

Error VlinkConfig::buildDelayTasks() {
    // create QRTA object for every independent component and every output port of every switch
    for(auto device: getAllDevices()) {
        if(device->type == Device::End) {
            continue;
        }
        for(const auto& compOwn: device->cioqMap->comps) {
            auto comp = compOwn.get();
            device->qrtas[std::make_pair(Device::F, comp->id)] = std::make_unique<QRTA>(this);
        }
        for(const auto& out_port_in: device->getAllOutPortsIn()) {
            int out_port_pseudo_id = out_port_in->id;
            device->qrtas[std::make_pair(Device::P, out_port_pseudo_id)] = std::make_unique<QRTA>(this);
        }
    }

    // create DelayTasks objects
    for(auto device: getAllDevices()) {
        for(auto port: device->getAllPorts()) {
            for(auto vnode: port->getAllVnodes()) {
                for(const auto& vnode_next_own: vnode->next) {
                    // device is either a SOURCE end system or a switch
                    auto vnode_next = vnode_next_own.get();
                    int in_id = vnode->in->id;
                    int out_pseudo_id = vnode_next->in->id;

                    QRTA *qrta_p = nullptr;
                    if(device->type == Device::Switch) {
                        auto comp = device->cioqMap->compsIndex[std::make_pair(in_id, out_pseudo_id)];
                        QRTA *qrta_f = device->qrtas[std::make_pair(Device::F, comp->id)].get();
                        vnode->delayTasks[std::make_pair(Device::F, out_pseudo_id)] =
                                std::make_unique<DelayTask>(vnode->vl, vnode_next, Device::F, qrta_f);
                        qrta_p = device->qrtas[std::make_pair(Device::P, out_pseudo_id)].get();
                    }
                    vnode->delayTasks[std::make_pair(Device::P, out_pseudo_id)] =
                            std::make_unique<DelayTask>(vnode->vl, vnode_next, Device::P, qrta_p);
                }
            }
        }
    }

    // fill data dependencies between DelayTasks objects (in inputs fields)
    for(auto device: getAllDevices()) {
        for(auto port: device->getAllPorts()) {
            for(auto vnode: port->getAllVnodes()) {
                for(const auto& vnode_next_own: vnode->next) {
                    // device is either a SOURCE end system or a switch
                    auto vnode_next = vnode_next_own.get();
                    int in_id = vnode->in->id;
                    int out_pseudo_id = vnode_next->in->id;

                    if(device->type == Device::Switch) {
                        auto delayTask_f = vnode->delayTasks[std::make_pair(Device::F, out_pseudo_id)].get();
                        auto comp = device->cioqMap->compsIndex[std::make_pair(in_id, out_pseudo_id)];
                        // get all vl branches through this switch in this independent component
                        for(auto [cur_in_id, cur_out_pseudo_id]: comp->edges) {
                            for(auto cur_vnode: device->getVlinks(cur_in_id, cur_out_pseudo_id)) {
                                assert(cur_vnode->in->id == cur_in_id); // DEBUG
                                auto curDelayTaskPrev = cur_vnode->prev->delayTasks[std::make_pair(Device::P, cur_in_id)].get();
                                delayTask_f->inputs[std::make_pair(Device::P, cur_out_pseudo_id)] = curDelayTaskPrev;
                            }
                        }

                        auto delayTask_p = vnode->delayTasks[std::make_pair(Device::P, out_pseudo_id)].get();
                        auto out_port_in = device->fromOutPortByPseudoId(out_pseudo_id);
                        // get all vls through this switch and its output port out_pseudo_id
                        for(auto cur_vnode_next: out_port_in->getAllVnodes()) {
                            auto cur_vnode = cur_vnode_next->prev;
                            auto curDelayTaskPrev = cur_vnode->delayTasks[std::make_pair(Device::F, out_pseudo_id)].get();
                            delayTask_p->inputs[std::make_pair(Device::F, out_pseudo_id)] = curDelayTaskPrev;
                        }
                    }
                }
            }
        }
    }

    // fill data dependencies between DelayTasks objects (in outputs fields)
    for(auto device: getAllDevices()) {
        for(auto port: device->getAllPorts()) {
            for(auto vnode: port->getAllVnodes()) {
                for(const auto& vnode_next_own: vnode->next) {
                    // device is either a SOURCE end system or a switch
                    auto vnode_next = vnode_next_own.get();
                    int out_pseudo_id = vnode_next->in->id;
                    if(device->type == Device::Switch) {
                        auto delayTask_f = vnode->delayTasks[std::make_pair(Device::F, out_pseudo_id)].get();
                        for(auto [vlBranch, curDelayTask]: delayTask_f->inputs) {
                            curDelayTask->output_for[std::make_pair(delayTask_f->vl->id, delayTask_f->vnode_next->in->id)] = delayTask_f;
                        }
                    }
                    auto delayTask_p = vnode->delayTasks[std::make_pair(Device::P, out_pseudo_id)].get();
                    for(auto [vlBranch, curDelayTask]: delayTask_p->inputs) {
                        assert(device->type == Device::Switch);
                        curDelayTask->output_for[std::make_pair(delayTask_p->vl->id, delayTask_p->vnode_next->in->id)] = delayTask_p;
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
    std::set<std::tuple<int, int, Device::elem_t>> tasksToVisitSet;
    int n_visited = 0;
    std::vector<DelayTask*> acyclicTasksOrder;
    std::set<std::tuple<int, int, Device::elem_t>> acyclicTasksSet;
    // fetching all delay tasks without inputs
    for(auto vl: getAllVlinks()) {
        auto vnode = vl->src.get();
        for(const auto& vnode_next_own: vnode->next) {
            auto vnode_next = vnode_next_own.get();
            int out_pseudo_id = vnode_next->in->id;
            auto delayTask = vnode->delayTasks[std::make_pair(Device::P, out_pseudo_id)].get();
            assert(delayTask->inputs.empty());
            tasksToVisitSet.insert(delayTask->id);
            tasksToVisit.push_back(delayTask);
        }
    }
    while(n_visited < tasksToVisit.size()) {
        int size_frozen = tasksToVisit.size();
        for(int i = n_visited; i < size_frozen; i++) {
            auto delayTask = tasksToVisit[i];
            assert(delayTask->in_cycle); // TODO ?
            bool all_inputs_are_not_in_cycle = true;
            for(auto[_, curDelayTask]: delayTask->inputs) {
                if(curDelayTask->in_cycle) {
                    all_inputs_are_not_in_cycle = false;
                    break;
                }
            }
            if(all_inputs_are_not_in_cycle) {
                delayTask->in_cycle = false;
                delayTask->cyclic_layer = 0;
                acyclicTasksSet.insert(delayTask->id);
                acyclicTasksOrder.push_back(delayTask);
                for(auto[_, curDelayTask]: delayTask->output_for) {
                    if(tasksToVisitSet.find(curDelayTask->id) == tasksToVisitSet.end()) {
                        tasksToVisitSet.insert(curDelayTask->id);
                        tasksToVisit.push_back(curDelayTask);
                    }
                }
            }
            n_visited++;
        }
    }
    bool acyclic = tasksToVisit.size() <= acyclicTasksOrder.size();
    if(!acyclic) {
        printf("%zu delay tasks found, but only %zu of them are not cyclic dependent!\n", tasksToVisit.size(), acyclicTasksOrder.size());
    } else {
        assert(tasksToVisit.size() == acyclicTasksOrder.size());
        printf("%zu delay tasks found, no cyclic dependencies\n", tasksToVisit.size());
    }

    printf("acyclic tasks order:\n"); // DEBUG
    for(auto delayTask: acyclicTasksOrder) {
        printf("task vl=%d to %d (%s)\n",
               delayTask->vl->id, delayTask->out_pseudo_id, delayTask->elem == Device::F ? "F" : "D"); // DEBUG
    }
    printf("\n"); // DEBUG

    this->tasks = tasksToVisit;
    this->acyclicTasksOrder = acyclicTasksOrder;
    if(acyclic) {
        return Error::Success;
    }

    // build a set of delay tasks with in_cycle=true ("cyclic" tasks), and label each cyclic task with
    // minimum hop distance to subgraph of acyclic tasks (cyclic_layer value)
//    std::vector<DelayTask*> cyclicTasksOrder1;
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
        int size_frozen = cyclicTasksToVisit.size();
        for(int i = n_visited; i < size_frozen; i++) {
            auto delayTask = cyclicTasksToVisit[i];
            delayTask->cyclic_layer = cyclic_layer;
//            cyclicTasksOrder1.push_back(delayTask);
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
    assert(cyclicTasksToVisitSet.size() + acyclicTasksSet.size() == tasksToVisitSet.size());
    assert(cyclicTasksToVisitSet.size() == cyclicTasksToVisit.size());
    assert(tasksToVisitSet.size() == tasksToVisit.size());
    assert(acyclicTasksOrder.size() == acyclicTasksSet.size());

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

    printf("cyclic tasks order:\n"); // DEBUG
    for(auto delayTask: cyclicTasksToVisit) {
        printf("task vl=%d to %d (%s), layer=%d, max_input_layer=%d\n",
               delayTask->vl->id, delayTask->out_pseudo_id, delayTask->elem == Device::F ? "F" : "D",
               delayTask->cyclic_layer, delayTask->max_input_layer); // DEBUG
    }
    printf("\n"); // DEBUG
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
        int n_visited = 0;
        while(n_visited < vnodes_order.size()) {
            int size_frozen = vnodes_order.size();
            for(int i = n_visited; i < size_frozen; i++) {
                auto cur_vnode = vnodes_order[i];
                for(const auto& vnode_next_own: cur_vnode->next) {
                    auto vnode_next = vnode_next_own.get();
                    vnodes_order.push_back(vnode_next);
                    auto found1 = cur_vnode->delayTasks.find(std::make_pair(Device::F, vnode_next->in->id));
                    if(found1 != cur_vnode->delayTasks.end()) {
                        auto delayTask = found1->second.get();
                        Error err = delayTask->calc_delay_init();
                        if(err) {
                            return err;
                        }
                    } else {
                        assert(cur_vnode->device->type == Device::End);
                    }
                    auto found2 = cur_vnode->delayTasks.find(std::make_pair(Device::P, vnode_next->in->id));
                    assert(found2 != cur_vnode->delayTasks.end());
                    auto delayTask = found2->second.get();
                    Error err = delayTask->calc_delay_init();
                    if(err) {
                        return err;
                    }
                    if(print) {
                        printf("vl %d to port %d: dmin=%ld, prelim jit=%ld\n", vl->id, vnode_next->in->id,
                               delayTask->delay.dmin(), delayTask->delay.jit()); // DEBUG
                    }
                }
            }
        }
    }
    // calculating max delays that are computable in one iteration
    for(auto delayTask: acyclicTasksOrder) {
        delayTask->get_input_data();
        Error err = delayTask->calc_delay_max();
        if(err) {
            return err;
        }
        delayTask->iter++;
        if(print) {
            printf("vl %d to port %d (%s): dmin=%ld, prelim jit=%ld\n", delayTask->vl->id, delayTask->out_pseudo_id,
                   delayTask->elem == Device::F ? "F" : "P", delayTask->delay.dmin(), delayTask->delay.jit());
        }
    }

    // calculating the rest of max delays iteratively, if there are cyclic data dependencies
    if(!cyclicTasksOrder.empty()) {
        int64_t sum = 0;
        int64_t sum_pre = -1;
        int n_iter = 0;
        while(sum_pre < sum && n_iter < cyclicMaxIter) {
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
                delayTask->iter++;
                if(print) {
                    printf("vl %d to port %d (%s): dmin=%ld, prelim jit=%ld\n", delayTask->vl->id, delayTask->out_pseudo_id,
                           delayTask->elem == Device::F ? "F" : "P", delayTask->delay.dmin(), delayTask->delay.jit());
                }
                sum += delayTask->delay.jit();
            }
            n_iter++;
            assert(sum_pre <= sum);
        }
        if(sum_pre < sum) {
            return Error::CyclicTooLong;
        }
    }

    for(auto vl: getAllVlinks()) {
        for(auto [_, vnode]: vl->dst) {
            vnode->e2e = vnode->prev->delayTasks[std::make_pair(Device::P, vnode->in->id)].get()->delay;
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

VlinkConfig::VlinkConfig() {}

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
    for(auto[vlBranch, delaytask]: inputs) {
        input_data[vlBranch] = delaytask->delay;
    }
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
        auto vlBranch = std::make_pair(vl->id, vnode_next->in->id);
        dmin = inputs[vlBranch]->delay.dmin() + vl->smin;
        dmax = inputs[vlBranch]->delay.dmax() + vl->smax;
    }
    delay = DelayData(vl, vnode_next, dmin, dmax-dmin);
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
    delay = DelayData(vl, vnode_next, dmin, dmax-dmin);
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
    auto found = fabricTable.find(std::pair(in_port_id, queue_id));
    assert(found != fabricTable.end());
    return found->second;
}

int CioqMap::getFabricIdByEdge(int in_port_id, int out_port_pseudo_id) const {
    return getFabricId(in_port_id, getQueueId(in_port_id, out_port_pseudo_id));
}

void CioqMap::setMap(const std::map<int, std::map<int, int>>& _queueTable, const std::map<std::pair<int, int>, int>& _fabricTable) {
    queueTable = _queueTable;
    fabricTable = _fabricTable;

    // build all comps (= independent components = components of fabric-induced subgraphs of the switch traffic graph)
    auto device_port_ids = device->getAllPortIds();
    for(auto in_id: device_port_ids) {
        for(int queue_id = 0; queue_id < n_queues; queue_id++) {
            // find any output port from this input queue
            int out_id = -1;
            for(auto cur_out_id: device_port_ids) {
                if(getQueueId(in_id, cur_out_id) == queue_id) {
                    out_id = cur_out_id;
                    break;
                }
            }
            auto found = compsIndex.find(std::pair(in_id, out_id));
            if(found != compsIndex.end()) continue;
            auto comp_edges = buildComp(in_id, queue_id);
            if(comp_edges.empty()) continue;
            comps.push_back(std::make_unique<PortsSubgraph>(comps.size(), comp_edges));
            PortsSubgraph* comp = comps[comps.size()-1].get();
            for(auto edge: comp_edges) {
                assert(compsIndex.find(edge) == compsIndex.end());
                compsIndex[edge] = comp;
            }
        }
    }

    // DEBUG assertions about compsIndex
    for(auto in_id: device_port_ids) {
        for(auto out_id: device_port_ids) {
            if(compsIndex.find(std::pair(in_id, out_id)) == compsIndex.end()) {
                assert(!device->hasVlinks(in_id, out_id));
                auto comp_edges = buildComp(in_id, getQueueId(in_id, out_id));
                assert(comp_edges.empty());
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
                if(device->hasVlinks(in_id, out_id) && getFabricIdByEdge(in_id, out_id) == fabric_id) {
                    edges.insert(std::pair(in_id, out_id));
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
                if(device->hasVlinks(in_id, out_id) && getFabricIdByEdge(in_id, out_id) == fabric_id) {
                    edges.insert(std::pair(in_id, out_id));
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

void generateTableBasic(Device* device, int n_queues, int n_fabrics) {
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
            int fabricId = i % n_fabrics + queueId * (((i % n_fabrics) % n_queues) * n_queues - 1);
            assert(fabricId < n_fabrics);
            fabricTable[std::pair(in_port_id, queueId)] = fabricId;
        }
        for(int j = 0; j < n_ports; j++) {
            int out_port_id = in_ports_ids[j];
            int out_port_pseudo_id = device->config->connectedPort(out_port_id);
            int queueId = j % 2;
            assert(queueId < n_queues);
            portQueueTable[out_port_pseudo_id] = queueId;
        }
        queueTable[in_port_id] = portQueueTable;
    }
    device->cioqMap->setMap(queueTable, fabricTable);
}

bool PortsSubgraph::isConnected(int node_in, int node_out) const {
    auto found = edges.find(std::pair(node_in, node_out));
    return (found != edges.end());
}
