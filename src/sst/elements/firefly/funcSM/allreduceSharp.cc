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

#include <sst_config.h>

#include <inttypes.h>

#include "funcSM/allreduceSharp.h"

using namespace SST::Firefly;

AllreduceSharpFuncSM::AllreduceSharpFuncSM( SST::Params& params ) :
    FunctionSMInterface( params ),
    m_event( NULL ),
    m_rank( 0 ),
    m_size( 0 ),
    m_dstRank( 0 ),
    m_srcRank( 0 ),
    m_iteration( 0 )
{
}

uint32_t AllreduceSharpFuncSM::genTag( uint32_t iteration ) const
{
    const uint32_t iterMask = 0xfff;
    return static_cast<uint32_t>(CtrlMsg::CollectiveTag |
        ((m_event->collectiveId & 0xfff) << 12) |
        (iteration & iterMask));
}

void AllreduceSharpFuncSM::handleStartEvent( SST::Event* ev, Retval& retval )
{
    assert( NULL == m_event );
    m_event = static_cast<AllreduceSharpStartEvent*>( ev );

    m_rank = m_info->getGroup( m_event->group )->getMyRank();
    m_size = m_info->getGroup( m_event->group )->getSize();
    m_dstRank = (m_rank + 1) % m_size;
    m_srcRank = (m_rank + m_size - 1) % m_size;
    m_iteration = 0;

    m_sendBuf.assign( m_event->bytes, 0 );
    m_recvBuf.assign( m_event->bytes, 0 );

    m_sendAddr.setSimVAddr( 1 );
    m_sendAddr.setBacking( m_sendBuf.data() );
    m_recvAddr.setSimVAddr( 1 );
    m_recvAddr.setBacking( m_recvBuf.data() );

    m_state = PostRecv;
    retval.setDelay( 0 );
}

void AllreduceSharpFuncSM::handleEnterEvent( Retval& retval )
{
    switch ( m_state ) {
      case PostRecv:
        if ( m_iteration >= 1 || m_size <= 1 ) {
            retval.setExit( 0 );
            delete m_event;
            m_event = NULL;
            return;
        }
        proto()->irecv( m_recvAddr, m_event->bytes, MP::CHAR, m_srcRank,
                        genTag(m_iteration), m_event->group, &m_recvReq );
        m_state = PostSend;
        break;

      case PostSend:
        proto()->isend( m_sendAddr, m_event->bytes, MP::CHAR, m_dstRank,
                        genTag(m_iteration), m_event->group, &m_sendReq,
                        true, m_event->collectiveId );
        m_state = WaitRecv;
        break;

      case WaitRecv:
        proto()->wait( m_recvReq, NULL );
        m_state = WaitSend;
        break;

      case WaitSend:
        proto()->wait( m_sendReq, NULL );
        if ( m_rank == 0 ) {
            m_dbg.output("AllreduceSharp iter %u done (bytes=%" PRIu64 ", collective=%" PRIu64 ")\n",
                         m_iteration, m_event->bytes, m_event->collectiveId);
        }
        ++m_iteration;
        m_state = PostRecv;
        break;
    }
}
