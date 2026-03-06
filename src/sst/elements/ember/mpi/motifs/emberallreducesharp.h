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

#ifndef _H_EMBER_ALLREDUCE_SHARP_MOTIF
#define _H_EMBER_ALLREDUCE_SHARP_MOTIF

#include "mpi/embermpigen.h"

namespace SST {
namespace Ember {

class EmberAllreduceSharpGenerator : public EmberMessagePassingGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT(
        EmberAllreduceSharpGenerator,
        "ember",
        "AllreduceSharpMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs an Allreduce operation driven by byte count parameters",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {   "arg.bytes",               "Sets the number of bytes to reduce per rank",     nullptr},
        {   "arg.iterations",          "Sets the number of allreduce operations to perform", "1"},
        {   "arg.compute",             "Sets the time spent computing",                    "0"},
        {   "arg.collective_id_seed",  "Sets the collective id seed",                      "0"},
        {   "arg.group",               "Sets the communicator group to use",               "GroupWorld"},
        {   "arg.debug_trace",         "Enables debug trace output",                        "0"},
    )

public:
    EmberAllreduceSharpGenerator(SST::ComponentId_t id, Params& params);
    bool generate(std::queue<EmberEvent*>& evQ) override;

private:
    uint64_t m_startTime;
    uint64_t m_stopTime;
    uint64_t m_compute;
    uint64_t m_bytes;
    uint32_t m_iterations;
    uint32_t m_loopIndex;
    uint32_t m_collectiveIdSeed;
    uint32_t m_debugTrace;
    Communicator m_group;
    void* m_sendBuf;
    void* m_recvBuf;
};

}
}

#endif
