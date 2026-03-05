// -*- mode: c++ -*-

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

#ifndef COMPONENTS_MERLIN_TOPOLOGY_GPU_H
#define COMPONENTS_MERLIN_TOPOLOGY_GPU_H

#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/params.h>

#include <string>

#include "sst/elements/merlin/router.h"

namespace SST {
namespace Merlin {

class topo_gpu : public Topology {
public:
    SST_ELI_REGISTER_SUBCOMPONENT(
        topo_gpu,
        "merlin",
        "gpu_topo",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "GPU <-> NVSwitch bipartite topology",
        SST::Merlin::Topology
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"num_gpu", "Number of GPU routers/endpoints."},
        {"num_nvswitches", "Number of NVSwitch routers."},
        {"routing_mode", "Routing mode from GPU to NVSwitch: packet_rr or flow_hash.", "packet_rr"}
    )

private:
    int num_ports;
    int router_id;
    int num_vns;

    int num_gpu;
    int num_nvswitches;
    bool packet_rr;

    uint64_t rr_counter;

    inline bool isGpuRouter() const { return router_id < num_gpu; }
    int selectSwitch(const internal_router_event* ev);

public:
    topo_gpu(ComponentId_t cid, Params& params, int num_ports, int rtr_id, int num_vns);
    ~topo_gpu() override;

    void route_packet(int port, int vc, internal_router_event* ev) override;
    internal_router_event* process_input(RtrEvent* ev) override;

    void routeUntimedData(int port, internal_router_event* ev, std::vector<int>& outPorts) override;
    internal_router_event* process_UntimedData_input(RtrEvent* ev) override;

    PortState getPortState(int port) const override;

    int getEndpointID(int port) override;
    std::pair<int,int> getDeliveryPortForEndpointID(int ep_id) override;

    void getVCsPerVN(std::vector<int>& vcs_per_vn) override {
        for ( int i = 0; i < num_vns; ++i ) {
            vcs_per_vn[i] = 1;
        }
    }
};

}
}

#endif
