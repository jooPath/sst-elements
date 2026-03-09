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
#include <cstring>

#include "hadesMP.h"
#include "funcSM/event.h"

using namespace SST::Firefly;
using namespace Hermes;
using namespace Hermes::MP;


HadesMP::HadesMP(ComponentId_t id, Params& params) :
    Interface(id), m_os(NULL),
    m_nextSharpRequestOrdinal(1),
    m_sharpCompletionLink(NULL),
    m_sharpCompletionEventScheduled(false)
{
    m_sharpCompletionLink = configureSelfLink(
        "HadesMP::SharpCompletion",
        "1ns",
        new Event::Handler2<HadesMP,&HadesMP::handleSharpCompletionEvent>(this));
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
    int rank = m_os->getRank();
    int size = m_os->getNumNodes();

    if ( bytes == 0 || size <= 1 ) {
        scheduleSharpCompletion(retFunc);
        return;
    }

    SharpReqKey key = { rank, group, collectiveId };
    if ( m_sharpReqMap.find(key) != m_sharpReqMap.end() ) {
        dbg().fatal(CALL_INFO, -1,
            "duplicate SHARP request key rank=%d group=%u cid=%" PRIu64 "\n",
            rank, group, collectiveId);
    }

    SharpReq req;
    req.mydata = mydata;
    req.result = result;
    req.bytes = bytes;
    req.op = op;
    req.group = group;
    req.collectiveId = collectiveId;
    req.retFunc = retFunc;
    req.rank = rank;
    req.size = size;
    req.dstRank = (rank + 1) % size;
    req.srcRank = (rank + size - 1) % size;
    req.numSegments = (bytes + kSharpSegSize - 1) / kSharpSegSize;
    req.dataRecvBuffers.resize(req.numSegments);
    req.ackRecvBuffers.resize(req.numSegments);
    req.dataResponses.resize(req.numSegments);
    req.ackResponses.resize(req.numSegments);

    dbg().debug(CALL_INFO,1,1,
        "SHARP ISSUE rank=%d group=%u cid=%" PRIu64 " bytes=%" PRIu64 " segs=%" PRIu64 "\n",
        rank, group, collectiveId, bytes, req.numSegments);

    m_sharpReqMap.emplace(key, std::move(req));
    SharpReq& cur = m_sharpReqMap.find(key)->second;

    for ( uint64_t segId = 0; segId < cur.numSegments; ++segId ) {
        uint64_t offset = segId * kSharpSegSize;
        uint32_t segBytes = std::min<uint64_t>(kSharpSegSize, bytes - offset);

        cur.dataRecvBuffers[segId].resize(sizeof(SharpPktHdr) + segBytes);
        cur.ackRecvBuffers[segId].resize(sizeof(SharpPktHdr));

        SharpCbCtx* dataCtx = new SharpCbCtx;
        dataCtx->key = key;
        dataCtx->segId = segId;
        dataCtx->request = new SharpRecvRequest();
        dataCtx->request->value = m_nextSharpRequestOrdinal++;

        recv(
            Hermes::MemAddr(cur.dataRecvBuffers[segId].data()),
            cur.dataRecvBuffers[segId].size(),
            MP::CHAR,
            cur.srcRank,
            kSharpDataTag,
            group,
            &cur.dataResponses[segId],
            new ArgStatic_Functor<HadesMP, int, uint64_t, bool>(
                this,
                &HadesMP::sharpOnDataRecv,
                reinterpret_cast<uint64_t>(dataCtx))
        );

        SharpCbCtx* ackCtx = new SharpCbCtx;
        ackCtx->key = key;
        ackCtx->segId = segId;
        ackCtx->request = new SharpRecvRequest();
        ackCtx->request->value = m_nextSharpRequestOrdinal++;

        recv(
            Hermes::MemAddr(cur.ackRecvBuffers[segId].data()),
            cur.ackRecvBuffers[segId].size(),
            MP::CHAR,
            cur.dstRank,
            kSharpAckTag,
            group,
            &cur.ackResponses[segId],
            new ArgStatic_Functor<HadesMP, int, uint64_t, bool>(
                this,
                &HadesMP::sharpOnAckRecv,
                reinterpret_cast<uint64_t>(ackCtx))
        );

        SharpSendCtx* sendCtx = new SharpSendCtx;
        sendCtx->packet.resize(sizeof(SharpPktHdr) + segBytes);

        SharpPktHdr hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.pktType = 0;
        hdr.isSharp = 1;
        hdr.collective_id = collectiveId;
        hdr.seg_id = segId;
        hdr.segment_bytes = segBytes;
        hdr.group = group;
        hdr.op = (op ? op->type : 0);
        hdr.srcRank = rank;
        hdr.dstRank = cur.dstRank;

        memcpy(sendCtx->packet.data(), &hdr, sizeof(hdr));
        if ( mydata.getBacking() ) {
            memcpy(sendCtx->packet.data() + sizeof(hdr), mydata.getBacking(offset), segBytes);
        } else {
            memset(sendCtx->packet.data() + sizeof(hdr), 0, segBytes);
        }

        send(
            Hermes::MemAddr(sendCtx->packet.data()),
            sendCtx->packet.size(),
            MP::CHAR,
            cur.dstRank,
            kSharpDataTag,
            group,
            new ArgStatic_Functor<HadesMP, int, uint64_t, bool>(
                this,
                &HadesMP::sharpOnDataSendDone,
                reinterpret_cast<uint64_t>(sendCtx))
        );

        dbg().debug(CALL_INFO,2,1,
            "SEND DATA rank=%d cid=%" PRIu64 " seg=%" PRIu64 " bytes=%u dst=%d\n",
            rank, collectiveId, segId, segBytes, cur.dstRank);
    }
}

bool HadesMP::sharpOnDataSendDone(int retval, uint64_t arg)
{
    (void) retval;
    SharpSendCtx* sendCtx = reinterpret_cast<SharpSendCtx*>(arg);
    delete sendCtx;
    return true;
}

bool HadesMP::sharpOnAckSendDone(int retval, uint64_t arg)
{
    (void) retval;
    SharpSendCtx* sendCtx = reinterpret_cast<SharpSendCtx*>(arg);
    delete sendCtx;
    return true;
}

bool HadesMP::sharpOnDataRecv(int retval, uint64_t arg)
{
    (void) retval;
    SharpCbCtx* ctx = reinterpret_cast<SharpCbCtx*>(arg);
    SharpReqKey key = ctx->key;
    uint64_t segId = ctx->segId;

    auto it = m_sharpReqMap.find(key);
    if ( it == m_sharpReqMap.end() ) {
        delete ctx->request;
        delete ctx;
        return true;
    }

    SharpReq& req = it->second;
    if ( segId >= req.dataRecvBuffers.size() ) {
        delete ctx->request;
        delete ctx;
        return true;
    }

    SharpPktHdr hdr;
    memcpy(&hdr, req.dataRecvBuffers[segId].data(), sizeof(hdr));

    if ( hdr.isSharp && hdr.pktType == 0 && hdr.collective_id == key.collectiveId && hdr.seg_id == segId ) {
        if ( req.recvDataSegments.insert(segId).second && req.result.getBacking() ) {
            if ( hdr.segment_bytes <= kSharpSegSize ) {
                memcpy(req.result.getBacking(segId * kSharpSegSize),
                    req.dataRecvBuffers[segId].data() + sizeof(SharpPktHdr),
                    hdr.segment_bytes);
            }
        }

        SharpSendCtx* ackSend = new SharpSendCtx;
        ackSend->packet.resize(sizeof(SharpPktHdr));

        SharpPktHdr ackHdr;
        memset(&ackHdr, 0, sizeof(ackHdr));
        ackHdr.pktType = 1;
        ackHdr.isSharp = 1;
        ackHdr.collective_id = hdr.collective_id;
        ackHdr.seg_id = hdr.seg_id;
        ackHdr.segment_bytes = 0;
        ackHdr.group = hdr.group;
        ackHdr.op = hdr.op;
        ackHdr.srcRank = req.rank;
        ackHdr.dstRank = req.srcRank;

        memcpy(ackSend->packet.data(), &ackHdr, sizeof(ackHdr));

        send(
            Hermes::MemAddr(ackSend->packet.data()),
            ackSend->packet.size(),
            MP::CHAR,
            req.srcRank,
            kSharpAckTag,
            req.group,
            new ArgStatic_Functor<HadesMP, int, uint64_t, bool>(
                this,
                &HadesMP::sharpOnAckSendDone,
                reinterpret_cast<uint64_t>(ackSend))
        );

        dbg().debug(CALL_INFO,2,1,
            "RECV DATA rank=%d cid=%" PRIu64 " seg=%" PRIu64 " -> SEND ACK to %d\n",
            req.rank, req.collectiveId, segId, req.srcRank);
    }

    sharpCheckAndComplete(key);

    delete ctx->request;
    delete ctx;
    return true;
}

bool HadesMP::sharpOnAckRecv(int retval, uint64_t arg)
{
    (void) retval;
    SharpCbCtx* ctx = reinterpret_cast<SharpCbCtx*>(arg);
    SharpReqKey key = ctx->key;
    uint64_t segId = ctx->segId;

    auto it = m_sharpReqMap.find(key);
    if ( it == m_sharpReqMap.end() ) {
        delete ctx->request;
        delete ctx;
        return true;
    }

    SharpReq& req = it->second;
    if ( segId < req.ackRecvBuffers.size() ) {
        SharpPktHdr hdr;
        memcpy(&hdr, req.ackRecvBuffers[segId].data(), sizeof(hdr));

        if ( hdr.isSharp && hdr.pktType == 1 &&
                hdr.collective_id == key.collectiveId && hdr.seg_id == segId ) {
            if ( req.ackedSegments.insert(segId).second ) {
                dbg().debug(CALL_INFO,2,1,
                    "RECV ACK rank=%d cid=%" PRIu64 " seg=%" PRIu64 "\n",
                    req.rank, req.collectiveId, segId);
            }
        }
    }

    sharpCheckAndComplete(key);

    delete ctx->request;
    delete ctx;
    return true;
}

void HadesMP::sharpCheckAndComplete(const SharpReqKey& key)
{
    auto it = m_sharpReqMap.find(key);
    if ( it == m_sharpReqMap.end() ) {
        return;
    }

    SharpReq& req = it->second;
    if ( req.recvDataSegments.size() == req.numSegments &&
            req.ackedSegments.size() == req.numSegments ) {
        MP::Functor* ret = req.retFunc;
        dbg().debug(CALL_INFO,1,1,
            "SHARP COMPLETE rank=%d cid=%" PRIu64 "\n",
            req.rank, req.collectiveId);
        m_sharpReqMap.erase(it);
        scheduleSharpCompletion(ret);
    }
}

void HadesMP::scheduleSharpCompletion(MP::Functor* ret)
{
    if ( !ret ) {
        return;
    }

    m_sharpCompletionQ.push_back(ret);
    if ( !m_sharpCompletionEventScheduled ) {
        m_sharpCompletionEventScheduled = true;
        m_sharpCompletionLink->send(0, new SharpCompletionEvent());
    }
}

void HadesMP::handleSharpCompletionEvent(SST::Event* ev)
{
    delete ev;

    m_sharpCompletionEventScheduled = false;
    while ( !m_sharpCompletionQ.empty() ) {
        MP::Functor* ret = m_sharpCompletionQ.front();
        m_sharpCompletionQ.pop_front();
        if ( ret ) {
            (*ret)(MP::SUCCESS);
        }
    }

    if ( !m_sharpCompletionQ.empty() ) {
        m_sharpCompletionEventScheduled = true;
        m_sharpCompletionLink->send(0, new SharpCompletionEvent());
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
