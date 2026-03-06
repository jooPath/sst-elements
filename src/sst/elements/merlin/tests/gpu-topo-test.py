#!/usr/bin/env python
#
# Copyright 2009-2025 NTESS. Under the terms
# of Contract DE-NA0003525 with NTESS, the U.S.
# Government retains certain rights in this software.
#
# Copyright (c) 2009-2025, NTESS
# All rights reserved.

import sst
from sst.merlin.base import *
from sst.merlin.endpoint import *
from sst.merlin.interface import *
from sst.merlin.topology import *

from sst.ember import *

if __name__ == "__main__":
    PlatformDefinition.setCurrentPlatform("firefly-defaults")

    # GPU / NVSwitch counts
    NUM_GPU = 8
    NUM_NVSWITCH = 2

    topo = topoGPU()
    topo.num_gpu = NUM_GPU
    topo.num_nvswitches = NUM_NVSWITCH
    topo.routing_mode = "packet_rr"   # or "flow_hash"

    router = hr_router()
    router.link_bw = "900GB/s"
    router.flit_size = "32B"
    router.xbar_bw = "1TB/s"
    router.input_latency = "10ns"
    router.output_latency = "10ns"
    router.input_buf_size = "64kB"
    router.output_buf_size = "64kB"
    router.num_vns = 2
    router.xbar_arb = "merlin.xbar_arb_lru"

    topo.router = router
    topo.link_latency = "20ns"
    topo.host_link_latency = "20ns"

    networkif = ReorderLinkControl()
    networkif.link_bw = "900GB/s"
    networkif.input_buf_size = "64kB"
    networkif.output_buf_size = "64kB"

    ep = EmberMPIJob(0, topo.getNumNodes())
    ep.network_interface = networkif
    ep.addMotif("Init")
    ep.addMotif("Allreduce")
    ep.addMotif("Fini")

    # NVLink payload size: larger data is segmented by packetSize and
    # can be striped over multiple NVSwitches by the GPU topology router.
    ep.nic.packetSize = "16kB"

    system = System()
    system.setTopology(topo)
    system.allocateNodes(ep, "linear")
    system.build()
