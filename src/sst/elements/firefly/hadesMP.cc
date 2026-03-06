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

#include "hadesMP.h"
#include "funcSM/event.h"
#include "virtNic.h"

#include <algorithm>
#include <unordered_map>

using namespace SST::Firefly;
using namespace Hermes;
using namespace Hermes::MP;

namespace {

struct SharpReqState {
    MP::Functor* retFunc;
    uint64_t expectedAcks;
    uint64_t ackCount;
    uint64_t dataCount;
    bool done;
};

using SharpKey = uint64_t;

SharpKey makeSharpKey(int rank, uint64_t collectiveId)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(rank)) << 32) ^ collectiveId;
}

std::unordered_map<SharpKey, SharpReqState> g_sharpReqMap;

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
    (void) result;

    Group* grp = m_os->getInfo()->getGroup( group );
    if ( !grp ) {
        dbg().fatal(CALL_INFO, -1, "allreduce_sharp: invalid group=%u\n", group);
    }

    const int myRank = grp->getMyRank();
    const int size = grp->getSize();
    if ( size <= 0 ) {
        dbg().fatal(CALL_INFO, -1, "allreduce_sharp: invalid group size=%d\n", size);
    }

    const int dstRank = ( myRank + 1 ) % size;
    const int dstNode = grp->getMapping( dstRank );

    constexpr uint64_t kSharpMaxSegBytes = 1024;
    const uint64_t totalSegs = ( bytes + kSharpMaxSegBytes - 1 ) / kSharpMaxSegBytes;

    dbg().debug(CALL_INFO,1,1,
        "allreduce_sharp transport-validation: srcRank=%d dstRank=%d dstNode=%d bytes=%" PRIu64 " segs=%" PRIu64 " group=%u collective_id=%" PRIu64 "\n",
        myRank, dstRank, dstNode, bytes, totalSegs, group, collectiveId);

    SharpReqState& req = g_sharpReqMap[ makeSharpKey( myRank, collectiveId ) ];
    req.retFunc = retFunc;
    req.expectedAcks = totalSegs;
    req.ackCount = 0;
    req.dataCount = 0;
    req.done = false;

    if ( 0 == totalSegs ) {
        req.done = true;
        if ( req.retFunc ) {
            (*req.retFunc)(0);
        }
        return;
    }

    uint64_t offset = 0;
    uint64_t segId = 0;
    while ( offset < bytes ) {
        const uint32_t segBytes = static_cast<uint32_t>( std::min<uint64_t>(kSharpMaxSegBytes, bytes - offset) );

        std::vector<IoVec> vec(1);
        Hermes::MemAddr segAddr = mydata;
        vec[0].addr = segAddr.offset( offset );
        vec[0].len = segBytes;

        m_os->getNic()->pioSendSharp( 0, dstNode, 0, vec, NULL,
            false, collectiveId, segId, segBytes,
            static_cast<uint32_t>(group), static_cast<uint32_t>(op ? op->type : 0),
            myRank, dstRank );

        offset += segBytes;
        ++segId;
    }
}

void HadesMP::sharpNotifyAckReceived( int dstRank, uint64_t collectiveId,
                                        uint64_t segId )
{
    auto iter = g_sharpReqMap.find( makeSharpKey(dstRank, collectiveId) );
    if ( iter == g_sharpReqMap.end() ) {
        return;
    }

    SharpReqState& req = iter->second;
    if ( req.done ) {
        return;
    }

    ++req.ackCount;

    if ( req.ackCount >= req.expectedAcks ) {
        req.done = true;
        MP::Functor* retFunc = req.retFunc;

        dbg().debug(CALL_INFO,1,1,
            "SHARP COMPLETE rank=%d cid=%" PRIu64 " seg=%" PRIu64 " (erase pending)\n",
            dstRank, collectiveId, segId );

        g_sharpReqMap.erase( iter );

        dbg().debug(CALL_INFO,1,1,
            "SHARP COMPLETE rank=%d cid=%" PRIu64 " seg=%" PRIu64 " (erased)\n",
            dstRank, collectiveId, segId );

        if ( retFunc ) {
            (*retFunc)(0);
        }
        return;
    }

    (void) segId;
}

void HadesMP::sharpNotifyDataReceived( int dstRank, uint64_t collectiveId,
                                         uint64_t segId )
{
    auto iter = g_sharpReqMap.find( makeSharpKey(dstRank, collectiveId) );
    if ( iter == g_sharpReqMap.end() ) {
        return;
    }

    ++iter->second.dataCount;
    (void) segId;
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
