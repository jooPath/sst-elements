// Copyright 2009-2025 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2025, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>

#include <functional>

#include "gpu.h"

using namespace SST::Merlin;

topo_gpu::topo_gpu(ComponentId_t cid, Params& params, int num_ports, int rtr_id, int num_vns) :
    Topology(cid),
    num_ports(num_ports),
    router_id(rtr_id),
    num_vns(num_vns),
    num_gpu(params.find<int>("num_gpu", -1)),
    num_nvswitches(params.find<int>("num_nvswitches", -1)),
    packet_rr(params.find<std::string>("routing_mode", "packet_rr") != "flow_hash"),
    rr_counter(0)
{
    if ( num_gpu <= 0 || num_nvswitches <= 0 ) {
        output.fatal(CALL_INFO, -1, "topo_gpu requires positive num_gpu and num_nvswitches (num_gpu=%d, num_nvswitches=%d)\n", num_gpu, num_nvswitches);
    }

    if ( router_id < num_gpu ) {
        const int expected_ports = num_nvswitches + 1;
        if ( num_ports != expected_ports ) {
            output.fatal(CALL_INFO, -1, "topo_gpu GPU router %d requires radix %d (got %d)\n", router_id, expected_ports, num_ports);
        }
    }
    else if ( router_id < (num_gpu + num_nvswitches) ) {
        const int expected_ports = num_gpu + 1;
        if ( num_ports != expected_ports ) {
            output.fatal(CALL_INFO, -1, "topo_gpu NVSwitch router %d requires radix %d (got %d)\n", router_id, expected_ports, num_ports);
        }
    }
    else {
        output.fatal(CALL_INFO, -1, "topo_gpu router id %d out of range [0,%d)\n", router_id, num_gpu + num_nvswitches);
    }
}

topo_gpu::~topo_gpu() = default;

int topo_gpu::selectSwitch(const internal_router_event* ev)
{
    if ( packet_rr ) {
        return (rr_counter++) % num_nvswitches;
    }

    uint64_t key = static_cast<uint64_t>(static_cast<uint32_t>(ev->getSrc()));
    key = (key << 32) | static_cast<uint32_t>(ev->getDest());
    return static_cast<int>(std::hash<uint64_t>{}(key) % static_cast<uint64_t>(num_nvswitches));
}

void topo_gpu::route_packet(int port, int vc, internal_router_event* ev)
{
    (void)port;
    (void)vc;

    const int dst = ev->getDest();

    if ( isGpuRouter() ) {
        if ( dst >= 0 && dst < num_gpu ) {
            if ( dst == router_id ) {
                ev->setNextPort(num_nvswitches);
            } else {
                ev->setNextPort(selectSwitch(ev));
            }
            return;
        }

        if ( dst >= num_gpu && dst < num_gpu + num_nvswitches ) {
            ev->setNextPort(dst - num_gpu);
            return;
        }

        output.fatal(CALL_INFO, -1, "topo_gpu GPU router unsupported destination %d\n", dst);
    }
    else {
        const int local_switch_ep = num_gpu + (router_id - num_gpu);
        if ( dst == local_switch_ep ) {
            ev->setNextPort(num_gpu);
            return;
        }
        if ( dst >= 0 && dst < num_gpu ) {
            ev->setNextPort(dst);
            return;
        }
        output.fatal(CALL_INFO, -1, "topo_gpu NVSwitch router unsupported destination %d\n", dst);
    }
}

internal_router_event* topo_gpu::process_input(RtrEvent* ev)
{
    internal_router_event* ire = new internal_router_event(ev);
    ire->setVC(ire->getVN());
    return ire;
}

void topo_gpu::routeUntimedData(int port, internal_router_event* ev, std::vector<int>& outPorts)
{
    if ( ev->getDest() == UNTIMED_BROADCAST_ADDR ) {
        if ( isGpuRouter() ) {
            if ( port == num_nvswitches ) {
                for ( int p = 0; p < num_nvswitches; ++p ) {
                    outPorts.push_back(p);
                }
            }
            else {
                outPorts.push_back(num_nvswitches);
            }
        }
        else {
            for ( int p = 0; p < num_gpu; ++p ) {
                if ( p != port ) {
                    outPorts.push_back(p);
                }
            }
        }
    }
    else {
        route_packet(port, 0, ev);
        outPorts.push_back(ev->getNextPort());
    }
}

internal_router_event* topo_gpu::process_UntimedData_input(RtrEvent* ev)
{
    return new internal_router_event(ev);
}

Topology::PortState topo_gpu::getPortState(int port) const
{
    if ( isGpuRouter() ) {
        if ( port < 0 || port >= num_nvswitches + 1 ) {
            return UNCONNECTED;
        }
        return (port == num_nvswitches) ? R2N : R2R;
    }

    if ( port < 0 || port >= num_gpu + 1 ) {
        return UNCONNECTED;
    }
    return (port == num_gpu) ? R2N : R2R;
}

int topo_gpu::getEndpointID(int port)
{
    if ( isGpuRouter() ) {
        if ( port == num_nvswitches ) {
            return router_id;
        }
        return -1;
    }

    if ( port == num_gpu ) {
        return num_gpu + (router_id - num_gpu);
    }
    return -1;
}

std::pair<int,int> topo_gpu::getDeliveryPortForEndpointID(int ep_id)
{
    if ( isGpuRouter() ) {
        if ( ep_id < 0 || ep_id >= num_gpu ) {
            return std::make_pair(-1, -1);
        }
        if ( router_id == ep_id ) {
            return std::make_pair(num_nvswitches, 0);
        }
        return std::make_pair(-1, -1);
    }

    const int local_ep = num_gpu + (router_id - num_gpu);
    if ( ep_id == local_ep ) {
        return std::make_pair(num_gpu, 0);
    }
    return std::make_pair(-1, -1);
}
