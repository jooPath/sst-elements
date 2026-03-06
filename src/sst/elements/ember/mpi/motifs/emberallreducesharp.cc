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

#include <inttypes.h>

#include "emberallreducesharp.h"

using namespace SST::Ember;

EmberAllreduceSharpGenerator::EmberAllreduceSharpGenerator(SST::ComponentId_t id, Params& params) :
    EmberMessagePassingGenerator(id, params, "AllreduceSharp"),
    m_loopIndex(0),
    m_group(GroupWorld),
    m_sendBuf(nullptr),
    m_recvBuf(nullptr)
{
    if (!params.contains("arg.bytes")) {
        fatal(CALL_INFO, -1, "Error: missing required parameter arg.bytes\n");
    }

    m_bytes = params.find<uint64_t>("arg.bytes", 0);
    if (0 == m_bytes) {
        fatal(CALL_INFO, -1, "Error: arg.bytes must be greater than 0\n");
    }

    m_iterations = params.find<uint32_t>("arg.iterations", 1);
    m_compute = params.find<uint64_t>("arg.compute", 0);
    m_collectiveIdSeed = params.find<uint32_t>("arg.collective_id_seed", 0);
    m_debugTrace = params.find<uint32_t>("arg.debug_trace", 0);

    const std::string group = params.find<std::string>("arg.group", "GroupWorld");
    if (group == "GroupWorld") {
        m_group = GroupWorld;
    } else {
        fatal(CALL_INFO, -1, "Error: unsupported arg.group value '%s'\n", group.c_str());
    }
}

bool EmberAllreduceSharpGenerator::generate(std::queue<EmberEvent*>& evQ)
{
    if (m_loopIndex == m_iterations) {
        if (0 == rank()) {
            double latency = static_cast<double>(m_stopTime - m_startTime) / static_cast<double>(m_iterations);
            latency /= 1000000000.0;
            output("%s: ranks %d, loop %d, bytes %" PRIu64 ", collective_id_seed %u, latency %.3f us\n",
                   getMotifName().c_str(),
                   size(),
                   m_iterations,
                   m_bytes,
                   m_collectiveIdSeed,
                   latency * 1000000.0);
        }
        return true;
    }

    if (0 == m_loopIndex) {
        memSetBacked();
        m_sendBuf = memAlloc(m_bytes);
        m_recvBuf = memAlloc(m_bytes);
        enQ_getTime(evQ, &m_startTime);

        if (0 == rank() && m_debugTrace) {
            output("%s: configured bytes=%" PRIu64 ", iterations=%u, compute=%" PRIu64 ", collective_id_seed=%u\n",
                   getMotifName().c_str(), m_bytes, m_iterations, m_compute, m_collectiveIdSeed);
        }
    }

    enQ_compute(evQ, m_compute);
    enQ_allreduce(evQ, m_sendBuf, m_recvBuf, m_bytes, CHAR, Hermes::MP::SUM, m_group);

    if (++m_loopIndex == m_iterations) {
        enQ_getTime(evQ, &m_stopTime);
    }

    return false;
}
