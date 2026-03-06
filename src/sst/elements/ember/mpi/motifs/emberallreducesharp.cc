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
#include "emberallreducesharp.h"

using namespace SST::Ember;

EmberAllreduceSharpGenerator::EmberAllreduceSharpGenerator(SST::ComponentId_t id,
                                        Params& params) :
    EmberMessagePassingGenerator(id, params, "AllreduceSharp"),
    m_localCollectiveSeq(0),
    m_loopIndex(0),
    m_sendBuf(NULL),
    m_recvBuf(NULL),
    m_op(Hermes::MP::SUM)
{
    m_bytes = (uint64_t) params.find<uint64_t>("arg.bytes", 0);
    if ( 0 == m_bytes ) {
        fatal(CALL_INFO, -1, "AllreduceSharp motif requires arg.bytes > 0\n");
    }

    m_iterations = (uint32_t) params.find("arg.iterations", 1);
    m_compute = (uint32_t) params.find("arg.compute", 0);
    m_collectiveIdSeed = (uint64_t) params.find<uint64_t>("arg.collective_id_seed", 0);
    m_group = (uint32_t) params.find<uint32_t>("arg.group", GroupWorld);
    m_debugTrace = params.find<bool>("arg.debug_trace", false);
}

bool EmberAllreduceSharpGenerator::generate( std::queue<EmberEvent*>& evQ)
{
    if ( m_loopIndex == m_iterations ) {
        if ( 0 == rank() && m_iterations > 0 ) {
            double latency = (double)(m_stopTime - m_startTime) / (double)m_iterations;
            latency /= 1000000000.0;
            output("%s: ranks %d, loop %d, %" PRIu64 " byte(s), latency %.3f us\n",
                getMotifName().c_str(), size(), m_iterations, m_bytes, latency * 1000000.0 );
        }
        return true;
    }

    if ( 0 == m_loopIndex ) {
        memSetBacked();
        m_sendBuf = memAlloc(m_bytes);
        m_recvBuf = memAlloc(m_bytes);
        enQ_getTime( evQ, &m_startTime );
    }

    const uint64_t collectiveId = m_collectiveIdSeed + m_localCollectiveSeq;

    if ( m_debugTrace ) {
        verbose(CALL_INFO, 1, MOTIF_MASK,
            "allreduce_sharp issue loop=%u collective_id=%" PRIu64 " bytes=%" PRIu64 " group=%u\n",
            m_loopIndex, collectiveId, m_bytes, m_group);
    }

    // Keep semantic separation from legacy enQ_allreduce(): one logical request + completion.
    enQ_compute( evQ, m_compute );
    enQ_allreduce_sharp( evQ, m_sendBuf, m_recvBuf, m_bytes, m_op, m_group, collectiveId );

    ++m_localCollectiveSeq;

    if ( ++m_loopIndex == m_iterations ) {
        enQ_getTime( evQ, &m_stopTime );
    }

    return false;
}
