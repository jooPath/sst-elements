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

#include <algorithm>

#include "hadesMP.h"
#include "funcSM/event.h"
#include "sst/elements/hermes/functor.h"

using namespace SST::Firefly;
using namespace Hermes;
using namespace Hermes::MP;

uint32_t HadesMP::makeSharpTag( HadesMP::SharpType type, MP::Communicator group, uint64_t collectiveId, uint64_t segId ) const
{
    const uint32_t typeBits = ( static_cast<uint32_t>( type ) & 0x1u ) << 27;
    const uint32_t groupBits = ( group & 0xFu ) << 23;
    const uint32_t collBits = ( static_cast<uint32_t>( collectiveId ) & 0x7FFu ) << 12;
    const uint32_t segBits = static_cast<uint32_t>( segId ) & 0xFFFu;
    return m_sharpTagBase | typeBits | groupBits | collBits | segBits;
}

void HadesMP::initSharpSelfLink()
{
    m_sharpSelfLink = configureSelfLink( "sharpSelf", "1ns",
        new Event::Handler2<HadesMP,&HadesMP::handleSharpSelfEvent>( this ) );
    assert( m_sharpSelfLink );
}

bool HadesMP::handleSharpRecv( int retval, SharpRecvCtx* ctx )
{
    if ( retval != MP::SUCCESS ) {
        dbg().debug(CALL_INFO,1,1,"sharp recv callback got retval=%d\n", retval );
    }

    const SharpPktHdr* hdr = reinterpret_cast<const SharpPktHdr*>( ctx->payload.data() );

    if ( ctx->type == SharpType::Data ) {
        SharpPktHdr* ackHdr = new SharpPktHdr();
        ackHdr->sharpType = static_cast<uint32_t>( SharpType::Ack );
        ackHdr->group = hdr->group;
        ackHdr->collectiveId = hdr->collectiveId;
        ackHdr->segId = hdr->segId;
        ackHdr->segmentBytes = 0;
        ackHdr->srcRank = ctx->key.rank;
        ackHdr->dstRank = hdr->srcRank;

        const uint32_t tag = makeSharpTag( SharpType::Ack, hdr->group, hdr->collectiveId, hdr->segId );
        send( Hermes::MemAddr( ackHdr ), static_cast<uint32_t>( sizeof( SharpPktHdr ) ), MP::CHAR,
            static_cast<RankID>( hdr->srcRank ), tag, hdr->group,
            new ArgStatic_Functor<HadesMP, int, SharpPktHdr*, bool>( this, &HadesMP::handleSharpSendDone, ackHdr ) );
    } else {
        auto iter = m_sharpReqMap.find( ctx->key );
        if ( iter != m_sharpReqMap.end() ) {
            SharpReqState& req = iter->second;

            if ( req.ackedSegments.insert( ctx->segId ).second ) {
                ++req.ackCount;
            }

            if ( req.ackCount == req.expectedAcks ) {
                MP::Functor* ret = req.retFunc;
                m_sharpReqMap.erase( iter );
                scheduleSharpCompletion( ret );
            }
        }
    }

    delete ctx->req;
    delete ctx;
    return true;
}

bool HadesMP::handleSharpSendDone( int retval, SharpPktHdr* hdr )
{
    if ( retval != MP::SUCCESS ) {
        dbg().debug(CALL_INFO,1,1,"sharp send callback got retval=%d\n", retval );
    }
    delete hdr;
    return true;
}

void HadesMP::scheduleSharpCompletion( MP::Functor* retFunc )
{
    if ( retFunc ) {
        m_sharpCompletionQ.push_back( retFunc );
    }

    if ( m_sharpCompletionScheduled ) {
        return;
    }

    m_sharpCompletionScheduled = true;
    assert( m_sharpSelfLink );
    m_sharpSelfLink->send( 0, new SharpSelfEvent() );
    initSharpSelfLink();
}

void HadesMP::processSharpCompletions()
{
    m_sharpCompletionScheduled = false;
    while ( ! m_sharpCompletionQ.empty() ) {
        MP::Functor* retFunc = m_sharpCompletionQ.front();
        m_sharpCompletionQ.pop_front();
        (*retFunc)( MP::SUCCESS );
    }
}


HadesMP::HadesMP(ComponentId_t id, Params& params) :
    Interface(id), m_os(NULL)
{
}

#if PRINT_STATUS
void HadesMP::printStatus( Output& out )
{
    std::map<int,ProtocolAPI*>::iterator iter= m_protocolM.begin();
    for ( ; iter != m_protocolM.end(); ++iter ) {
        iter->second->printStatus(out);
    }
    m_functionSM->printStatus( out );
}
#endif

void HadesMP::init(Functor* retFunc )
{
    functionSM().start( FunctionSM::Init, retFunc, new InitStartEvent() );
}

void HadesMP::makeProgress(Functor* retFunc )
{
    functionSM().start( FunctionSM::MakeProgress, retFunc, new MakeProgressStartEvent() );
}

void HadesMP::fini(Functor* retFunc)
{
    functionSM().start( FunctionSM::Fini, retFunc, new FiniStartEvent() );
}

void HadesMP::rank(Communicator group, RankID* rank, Functor* retFunc)
{
    functionSM().start(FunctionSM::Rank, retFunc,
                            new RankStartEvent( group, (int*) rank) );
}

void HadesMP::size(Communicator group, int* size, Functor* retFunc )
{
    functionSM().start( FunctionSM::Size, retFunc,
                            new SizeStartEvent( group, size) );
}

void HadesMP::send(const Hermes::MemAddr& buf, uint32_t count,
        PayloadDataType dtype, RankID dest, uint32_t tag, Communicator group,
        Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"buf=%p count=%d dtype=%d dest=%d tag=%d "
                        "group=%d\n", &buf,count,dtype,dest,tag,group);
    functionSM().start( FunctionSM::Send, retFunc,
            new SendStartEvent( buf, count, dtype, dest, tag, group, NULL) );
}

void HadesMP::isend(const Hermes::MemAddr& buf, uint32_t count, PayloadDataType dtype,
        RankID dest, uint32_t tag, Communicator group,
        MessageRequest* req, Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"buf=%p count=%d dtype=%d dest=%d tag=%d "
                        "group=%d\n", &buf,count,dtype,dest,tag,group);
    functionSM().start( FunctionSM::Send, retFunc,
                new SendStartEvent( buf, count, dtype, dest, tag, group, req));
}

void HadesMP::recv(const Hermes::MemAddr& target, uint32_t count, PayloadDataType dtype,
        RankID source, uint32_t tag, Communicator group,
        MessageResponse* resp, Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"target=%p count=%d dtype=%d source=%d tag=%d "
                        "group=%d\n", &target,count,dtype,source,tag,group);
    functionSM().start( FunctionSM::Recv, retFunc,
      new RecvStartEvent(target, count, dtype, source, tag, group, NULL, resp));
}

void HadesMP::irecv(const Hermes::MemAddr& target, uint32_t count, PayloadDataType dtype,
        RankID source, uint32_t tag, Communicator group,
        MessageRequest* req, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"target=%p count=%d dtype=%d source=%d tag=%d "
                        "group=%d\n", &target,count,dtype,source,tag,group);
    functionSM().start( FunctionSM::Recv, retFunc,
      new RecvStartEvent(target, count, dtype, source, tag, group, req, NULL));
}

void HadesMP::allreduce(const Hermes::MemAddr& mydata,
		const Hermes::MemAddr& result, uint32_t count,
        PayloadDataType dtype, ReductionOperation op,
        Communicator group, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"in=%p out=%p count=%d dtype=%d\n",
                &mydata,&result,count,dtype);
    functionSM().start( FunctionSM::Allreduce, retFunc,
    new CollectiveStartEvent(mydata, result, count, dtype, op, 0, group,
                            CollectiveStartEvent::Allreduce));
}

void HadesMP::allreduce_sharp(const Hermes::MemAddr& mydata,
        const Hermes::MemAddr& result, uint64_t bytes,
        ReductionOperation op, Communicator group,
        uint64_t collectiveId, Functor* retFunc)
{
    (void) mydata;
    (void) result;
    (void) op;

    Group* groupInfo = m_os->getInfo()->getGroup( group );
    if ( nullptr == groupInfo ) {
        dbg().fatal(CALL_INFO,-1,"allreduce_sharp invalid group=%u\n", group );
    }

    const int rank = groupInfo->getMyRank();
    const int size = groupInfo->getSize();
    const int dstRank = ( rank + 1 ) % size;
    const int srcRank = ( rank + size - 1 ) % size;
    const uint64_t numSegments = std::max<uint64_t>( 1, ( bytes + m_sharpSegmentBytes - 1 ) / m_sharpSegmentBytes );

    SharpKey key{ rank, group, collectiveId };
    SharpReqState req;
    req.expectedAcks = numSegments;
    req.retFunc = retFunc;
    m_sharpReqMap[key] = std::move( req );

    dbg().debug(CALL_INFO,1,1,
        "allreduce_sharp issue rank=%d size=%d src=%d dst=%d bytes=%" PRIu64 " segs=%" PRIu64 " group=%u collective_id=%" PRIu64 "\n",
        rank, size, srcRank, dstRank, bytes, numSegments, group, collectiveId);

    for ( uint64_t segId = 0; segId < numSegments; ++segId ) {
        const uint32_t segmentBytes = static_cast<uint32_t>( std::min<uint64_t>( m_sharpSegmentBytes, bytes - ( segId * m_sharpSegmentBytes ) ) );

        auto* dataCtx = new SharpRecvCtx;
        dataCtx->type = SharpType::Data;
        dataCtx->key = SharpKey{ rank, group, collectiveId };
        dataCtx->segId = segId;
        dataCtx->payload.resize( sizeof( SharpPktHdr ) );
        dataCtx->req = new MessageRequestBase;

        irecv( Hermes::MemAddr( dataCtx->payload.data() ), static_cast<uint32_t>( dataCtx->payload.size() ), MP::CHAR,
            static_cast<RankID>( srcRank ), makeSharpTag( SharpType::Data, group, collectiveId, segId ), group,
            &dataCtx->req,
            new ArgStatic_Functor<HadesMP, int, SharpRecvCtx*, bool>( this, &HadesMP::handleSharpRecv, dataCtx ) );

        auto* ackCtx = new SharpRecvCtx;
        ackCtx->type = SharpType::Ack;
        ackCtx->key = key;
        ackCtx->segId = segId;
        ackCtx->payload.resize( sizeof( SharpPktHdr ) );
        ackCtx->req = new MessageRequestBase;

        irecv( Hermes::MemAddr( ackCtx->payload.data() ), static_cast<uint32_t>( ackCtx->payload.size() ), MP::CHAR,
            static_cast<RankID>( dstRank ), makeSharpTag( SharpType::Ack, group, collectiveId, segId ), group,
            &ackCtx->req,
            new ArgStatic_Functor<HadesMP, int, SharpRecvCtx*, bool>( this, &HadesMP::handleSharpRecv, ackCtx ) );

        SharpPktHdr* dataHdr = new SharpPktHdr();
        dataHdr->sharpType = static_cast<uint32_t>( SharpType::Data );
        dataHdr->group = group;
        dataHdr->collectiveId = collectiveId;
        dataHdr->segId = segId;
        dataHdr->segmentBytes = segmentBytes;
        dataHdr->srcRank = rank;
        dataHdr->dstRank = dstRank;

        send( Hermes::MemAddr( dataHdr ), static_cast<uint32_t>( sizeof( SharpPktHdr ) ), MP::CHAR,
            static_cast<RankID>( dstRank ), makeSharpTag( SharpType::Data, group, collectiveId, segId ), group,
            new ArgStatic_Functor<HadesMP, int, SharpPktHdr*, bool>( this, &HadesMP::handleSharpSendDone, dataHdr ) );
    }

    if ( bytes == 0 ) {
        auto iter = m_sharpReqMap.find( key );
        if ( iter != m_sharpReqMap.end() ) {
            MP::Functor* ret = iter->second.retFunc;
            m_sharpReqMap.erase( iter );
            scheduleSharpCompletion( ret );
        }
    }
}

void HadesMP::reduce(const Hermes::MemAddr& mydata,
		const Hermes::MemAddr& result, uint32_t count,
        PayloadDataType dtype, ReductionOperation op, RankID root,
        Communicator group, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"in=%p out=%p count=%d dtype=%d \n",
                &mydata,&result,count,dtype);
    functionSM().start( FunctionSM::Allreduce, retFunc,
        new CollectiveStartEvent(mydata, result, count,
                        dtype, op, root, group,
                            CollectiveStartEvent::Reduce) );
}

void HadesMP::bcast(const Hermes::MemAddr& mydata, uint32_t count,
        PayloadDataType dtype, RankID root,
        Communicator group, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"in=%p ount=%d dtype=%d \n",
                &mydata,count,dtype);

	Hermes::MemAddr addr(1,NULL);

    functionSM().start( FunctionSM::Allreduce, retFunc,
        new CollectiveStartEvent(mydata, addr, count,
                        dtype, NOP, root, group,
                            CollectiveStartEvent::Bcast) );
}

void HadesMP::scatter(
        const Hermes::MemAddr& sendBuf, uint32_t sendcnt, MP::PayloadDataType sendtype,
        const Hermes::MemAddr& recvBuf, uint32_t recvcnt, MP::PayloadDataType recvType,
        MP::RankID root, MP::Communicator group, MP::Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"sendcnt=%d recvcnt=%d\n",sendcnt,recvcnt);

    functionSM().start( FunctionSM::Scatterv, retFunc,
        new ScattervStartEvent(sendBuf, sendcnt, sendtype, recvBuf, recvcnt, recvType, root, group ) );
}

void HadesMP::scatterv(
        const Hermes::MemAddr& sendBuf, int* sendcnt, int* displs, MP::PayloadDataType sendtype,
        const Hermes::MemAddr& recvBuf, int recvcnt, MP::PayloadDataType recvType,
        MP::RankID root, MP::Communicator group, MP::Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"\n");

    functionSM().start( FunctionSM::Scatterv, retFunc,
        new ScattervStartEvent(sendBuf, sendcnt, displs, sendtype, recvBuf, recvcnt, recvType, root, group ) );
}

void HadesMP::allgather( const Hermes::MemAddr& sendbuf, uint32_t sendcnt, PayloadDataType sendtype,
        const Hermes::MemAddr& recvbuf, uint32_t recvcnt, PayloadDataType recvtype,
        Communicator group, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"\n");

    functionSM().start( FunctionSM::Allgather, retFunc,
        new GatherStartEvent( sendbuf, sendcnt, sendtype,
            recvbuf, recvcnt, recvtype, group ) );
}

void HadesMP::allgatherv(
        const Hermes::MemAddr& sendbuf, uint32_t sendcnt, PayloadDataType sendtype,
        const Hermes::MemAddr& recvbuf, Addr recvcnt, Addr displs, PayloadDataType recvtype,
        Communicator group, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"sendbuf=%p recvbuf=%p sendcnt=%d "
                    "recvcntPtr=%p\n", &sendbuf,&recvbuf,sendcnt,recvcnt);
    functionSM().start( FunctionSM::Allgather, retFunc,
        new GatherStartEvent( sendbuf, sendcnt, sendtype,
            recvbuf, recvcnt, displs, recvtype, group ) );
}

void HadesMP::gather( const Hermes::MemAddr& sendbuf, uint32_t sendcnt, PayloadDataType sendtype,
        const Hermes::MemAddr& recvbuf, uint32_t recvcnt, PayloadDataType recvtype,
        RankID root, Communicator group, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::Gatherv, retFunc,
        new GatherStartEvent(sendbuf, sendcnt, sendtype,
            recvbuf, recvcnt, recvtype, root, group ) );
}

void HadesMP::gatherv( const Hermes::MemAddr& sendbuf, uint32_t sendcnt, PayloadDataType sendtype,
        const Hermes::MemAddr& recvbuf, Addr recvcnt, Addr displs,
        PayloadDataType recvtype,
        RankID root, Communicator group, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"sendbuf=%p recvbuf=%p sendcnt=%d "
                    "recvcntPtr=%p\n", &sendbuf,&recvbuf,sendcnt,recvcnt);
    functionSM().start( FunctionSM::Gatherv, retFunc,
         new GatherStartEvent( sendbuf, sendcnt, sendtype,
            recvbuf, recvcnt, displs, recvtype, root, group ) );
}

void HadesMP::alltoall(
        const Hermes::MemAddr& sendbuf, uint32_t sendcnt, PayloadDataType sendtype,
        const Hermes::MemAddr& recvbuf, uint32_t recvcnt, PayloadDataType recvtype,
        Communicator group, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"sendbuf=%p recvbuf=%p sendcnt=%d "
                        "recvcnt=%d\n", &sendbuf,&recvbuf,sendcnt,recvcnt);
    functionSM().start( FunctionSM::Alltoallv, retFunc,
        new AlltoallStartEvent( sendbuf,sendcnt, sendtype, recvbuf,
                                    recvcnt, recvtype, group) );
}

void HadesMP::alltoallv(
        const Hermes::MemAddr& sendbuf, Addr sendcnts, Addr senddispls, PayloadDataType sendtype,
        const Hermes::MemAddr& recvbuf, Addr recvcnts, Addr recvdispls, PayloadDataType recvtype,
        Communicator group, Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"sendbuf=%p recvbuf=%p sendcntPtr=%p "
                        "recvcntPtr=%p\n", &sendbuf,&recvbuf,sendcnts,recvcnts);
    functionSM().start( FunctionSM::Alltoallv, retFunc,
        new AlltoallStartEvent( sendbuf, sendcnts, senddispls, sendtype,
            recvbuf, recvcnts, recvdispls, recvtype,
            group) );
}

void HadesMP::barrier(Communicator group, Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::Barrier, retFunc,
                            new BarrierStartEvent( group) );
}

void HadesMP::probe(RankID source, uint32_t tag,
        Communicator group, MessageResponse* resp, Functor* retFunc )
{
	assert(0);
}

void HadesMP::cancel( MessageRequest req, Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::Cancel, retFunc,
                             new CancelStartEvent( req ) );
}

void HadesMP::wait( MessageRequest req, MessageResponse* resp,
        Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::Wait, retFunc,
                             new WaitStartEvent( req, resp) );
}

void HadesMP::waitany( int count, MessageRequest req[], int* index,
                            MessageResponse* resp, Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::WaitAny, retFunc,
                             new WaitAnyStartEvent( count, req, index, resp) );
}

void HadesMP::waitall( int count, MessageRequest req[],
                            MessageResponse* resp[], Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::WaitAll, retFunc,
                             new WaitAllStartEvent( count, req, resp) );
}

void HadesMP::test(MessageRequest req, int* flag, MessageResponse* resp,
        Functor* retFunc)
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::Test, retFunc,
                             new TestStartEvent( req, flag, resp) );
}

void HadesMP::testany( int count, MP::MessageRequest req[], int* indx, int* flag,
           MP::MessageResponse* resp, MP::Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::Testany, retFunc,
                             new TestanyStartEvent( count, req, indx, flag, resp) );
}

void HadesMP::comm_split( Communicator oldComm, int color, int key,
                            Communicator* newComm, Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::CommSplit, retFunc,
                       new CommSplitStartEvent( oldComm, color, key, newComm) );
}

void HadesMP::comm_create( MP::Communicator oldComm, size_t nRanks, int* ranks,
        MP::Communicator* newComm, MP::Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::CommCreate, retFunc,
            new CommCreateStartEvent( oldComm, nRanks, ranks, newComm) );
}

void HadesMP::comm_destroy( MP::Communicator comm, MP::Functor* retFunc )
{
    dbg().debug(CALL_INFO,1,1,"\n");
    functionSM().start( FunctionSM::CommDestroy, retFunc,
            new CommDestroyStartEvent( comm ) );
}
