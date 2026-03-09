#!/usr/bin/env python
#
# Copyright 2009-2025 NTESS. Under the terms
# of Contract DE-NA0003525 with NTESS, the U.S.
# Government retains certain rights in this software.
#
# Copyright (c) 2009-2025, NTESS
# All rights reserved.
#
# Portions are copyright of other developers:
# See the file CONTRIBUTORS.TXT in the top level directory
# of the distribution for more information.
#
# This file is part of the SST software package. For license
# information, see the LICENSE file in the top level directory of the
# distribution.

import sst
from sst.merlin.base import *


class topoGPU(Topology):
    def __init__(self):
        Topology.__init__(self)
        self._declareClassVariables(["link_latency", "host_link_latency", "routing_mode", "bundleEndpoints"])
        self._declareParams("main", ["num_gpu", "num_nvswitches"])
        self.routing_mode = "packet_rr"
        self.bundleEndpoints = True
        self._subscribeToPlatformParamSet("topology")

    def getName(self):
        return "GPU"

    def getNumNodes(self):
        return self.num_gpu

    def getRouterNameForId(self, rtr_id):
        if rtr_id < self.num_gpu:
            return "gpu_%d" % rtr_id
        return "nvswitch_%d" % (rtr_id - self.num_gpu)

    def _build_impl(self, endpoint):
        if self.host_link_latency is None:
            self.host_link_latency = self.link_latency

        links = dict()

        def getLink(g, s):
            key = (g, s)
            if key not in links:
                links[key] = sst.Link("link_gpu%d_nvs%d" % (g, s))
            return links[key]

        common_topo_params = self._getGroupParams("main")
        common_topo_params["routing_mode"] = self.routing_mode

        total_routers = self.num_gpu + self.num_nvswitches
        for rid in range(total_routers):
            if rid < self.num_gpu:
                radix = self.num_nvswitches + 1
            else:
                radix = self.num_gpu + 1

            rtr = self._instanceRouter(radix, rid)
            topo = rtr.setSubComponent(self.router.getTopologySlotName(), "merlin.gpu_topo")
            self._applyStatisticsSettings(topo)
            topo.addParams(common_topo_params)

            if rid < self.num_gpu:
                g = rid
                for s in range(self.num_nvswitches):
                    rtr.addLink(getLink(g, s), "port%d" % s, self.link_latency)

                ep, port_name = endpoint.build(g, {})
                if ep:
                    nic_link = sst.Link("nic_gpu_%d" % g)
                    if self.bundleEndpoints:
                        nic_link.setNoCut()
                    nic_link.connect((ep, port_name, self.host_link_latency),
                                     (rtr, "port%d" % self.num_nvswitches, self.host_link_latency))
            else:
                s = rid - self.num_gpu
                for g in range(self.num_gpu):
                    rtr.addLink(getLink(g, s), "port%d" % g, self.link_latency)

                sharp = sst.Component("nvswitch_collective_%d" % s, "merlin.nvswitch_collective_endpoint")
                sharp.addParams({
                    "num_gpu": self.num_gpu,
                    "num_nvswitches": self.num_nvswitches,
                    "switch_index": s,
                    "node_id": self.num_gpu + s,
                })

                sharp_if = sharp.setSubComponent("networkIF", "merlin.linkcontrol", 0)
                sharp_if.addParams({
                    "link_bw": self.router.link_bw,
                    "input_buf_size": "64kB",
                    "output_buf_size": "64kB",
                    "port_name": "rtr",
                })

                sharp_link = sst.Link("sharp_nvs_%d" % s)
                sharp_link.connect((sharp_if, "rtr", self.link_latency),
                                   (rtr, "port%d" % self.num_gpu, self.link_latency))
