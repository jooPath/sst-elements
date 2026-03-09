// Copyright 2013-2025 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2013-2025, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef COMPONENTS_FIREFLY_FUNCSM_ALLREDUCESHARP_H
#define COMPONENTS_FIREFLY_FUNCSM_ALLREDUCESHARP_H

#include <vector>

#include "funcSM/api.h"
#include "funcSM/event.h"
#include "ctrlMsg.h"

namespace SST {
namespace Firefly {

#undef FOREACH_ENUM

#define FOREACH_ENUM(NAME) \
    NAME(PostRecv) \
    NAME(PostSend) \
    NAME(WaitRecv) \
    NAME(WaitSend) \

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

class AllreduceSharpFuncSM : public FunctionSMInterface
{
  public:
    SST_ELI_REGISTER_MODULE(
        AllreduceSharpFuncSM,
        "firefly",
        "AllreduceSharp",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "",
        SST::Firefly::FunctionSMInterface
    )

  public:
    AllreduceSharpFuncSM( SST::Params& params );

    virtual void handleStartEvent( SST::Event*, Retval& );
    virtual void handleEnterEvent( Retval& );

    virtual std::string protocolName() { return "CtrlMsgProtocol"; }

  private:
    typedef CtrlMsg::API Proto;

    enum StateEnum {
        FOREACH_ENUM(GENERATE_ENUM)
    } m_state;

    uint32_t genTag( uint32_t iteration ) const;

    Proto* proto() { return static_cast<Proto*>(m_proto); }

    AllreduceSharpStartEvent* m_event;
    std::vector<uint8_t> m_sendBuf;
    std::vector<uint8_t> m_recvBuf;

    Hermes::MemAddr m_sendAddr;
    Hermes::MemAddr m_recvAddr;

    uint32_t m_rank;
    uint32_t m_size;
    uint32_t m_dstRank;
    uint32_t m_srcRank;
    uint32_t m_iteration;

    MP::MessageRequest m_sendReq;
    MP::MessageRequest m_recvReq;
};

}
}

#endif
