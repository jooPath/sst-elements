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


#ifndef COMPONENTS_FIREFLY_HADESMP_H
#define COMPONENTS_FIREFLY_HADESMP_H

#include <sst/core/params.h>

#include <deque>
#include <set>
#include <unordered_map>
#include <vector>

#include "sst/elements/hermes/msgapi.h"
#include "hades.h"
#include "functionSM.h"

using namespace Hermes;

namespace SST {
namespace Firefly {

class HadesMP : public MP::Interface
{
  public:
    SST_ELI_REGISTER_SUBCOMPONENT(
        HadesMP,
        "firefly",
        "hadesMP",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "",
        SST::Hermes::MP::Interface
    )
    SST_ELI_DOCUMENT_PARAMS(
        {"verboseLevel", "Sets the output verbosity of the component", "1"},
        {"verboseMask", "Sets the output mask of the component", "1"},
        {"defaultEnterLatency","Sets the default function enter latency","30000"},
        {"defaultReturnLatency","Sets the default function return latency","30000"},
        {"nodeId", "internal", ""},
        {"enterLatency","internal",""},
        {"returnLatency","internal",""},
        {"defaultModule","Sets the default function module","firefly"},
    )
  public:
    HadesMP(ComponentId_t, Params&);
    ~HadesMP() {}

    virtual std::string getName() { return "HadesMP"; }
    virtual std::string getType() { return "mpi"; }

	virtual void setup() {}
	virtual void finish() {}
	virtual void setOS( OS* os ) {
		m_os = static_cast<Hades*>(os);
		dbg().debug(CALL_INFO,2,0,"\n");
	}

	int sizeofDataType( MP::PayloadDataType type ) {
		return m_os->sizeofDataType(type);
	}

    virtual void init(MP::Functor*);
    virtual void fini(MP::Functor*);
    virtual void rank(MP::Communicator group, MP::RankID* rank,
                                                    MP::Functor*);
    virtual void size(MP::Communicator group, int* size, MP::Functor* );
    virtual void makeProgress(MP::Functor*);

    virtual void send(const Hermes::MemAddr&, uint32_t count,
        MP::PayloadDataType dtype, MP::RankID dest, uint32_t tag,
        MP::Communicator group, MP::Functor*);

    virtual void isend(const Hermes::MemAddr&, uint32_t count,
        MP::PayloadDataType dtype, MP::RankID dest, uint32_t tag,
        MP::Communicator group, MP::MessageRequest* req,
        MP::Functor*);

    virtual void recv(const Hermes::MemAddr&, uint32_t count,
        MP::PayloadDataType dtype, MP::RankID source, uint32_t tag,
        MP::Communicator group, MP::MessageResponse* resp,
        MP::Functor*);

    virtual void irecv(const Hermes::MemAddr&, uint32_t count,
        MP::PayloadDataType dtype, MP::RankID source, uint32_t tag,
        MP::Communicator group, MP::MessageRequest* req,
        MP::Functor*);

    virtual void allreduce(const Hermes::MemAddr&,
		const Hermes::MemAddr& result, uint32_t count,
        MP::PayloadDataType dtype, MP::ReductionOperation op,
        MP::Communicator group, MP::Functor*);

    virtual void allreduce_sharp(const Hermes::MemAddr&,
        const Hermes::MemAddr& result, uint64_t bytes,
        MP::ReductionOperation op, MP::Communicator group,
        uint64_t collectiveId, MP::Functor*);

    virtual void reduce(const Hermes::MemAddr&,
		const Hermes::MemAddr& result,
        uint32_t count, MP::PayloadDataType dtype,
        MP::ReductionOperation op, MP::RankID root,
        MP::Communicator group, MP::Functor*);

    virtual void bcast(const Hermes::MemAddr&,
        uint32_t count, MP::PayloadDataType dtype, MP::RankID root,
        MP::Communicator group, MP::Functor*);

    void scatter(
        const Hermes::MemAddr& sendBuf, uint32_t sendcnt, MP::PayloadDataType sendtype,
        const Hermes::MemAddr& recvBuf, uint32_t recvcnt, MP::PayloadDataType recvType,
        MP::RankID root, MP::Communicator group, MP::Functor*);

    void scatterv(
        const Hermes::MemAddr& sendBuf, int* sendcnt, int* displs, MP::PayloadDataType sendtype,
        const Hermes::MemAddr& recvBuf, int recvcnt, MP::PayloadDataType recvType,
        MP::RankID root, MP::Communicator group, MP::Functor*);

    virtual void allgather( const Hermes::MemAddr&, uint32_t sendcnt,
        MP::PayloadDataType sendtype,
        const Hermes::MemAddr&, uint32_t recvcnt,
        MP::PayloadDataType recvtype,
        MP::Communicator group, MP::Functor*);

    virtual void allgatherv( const Hermes::MemAddr&, uint32_t sendcnt,
        MP::PayloadDataType sendtype,
        const Hermes::MemAddr&, MP::Addr recvcnt, MP::Addr displs,
        MP::PayloadDataType recvtype,
        MP::Communicator group, MP::Functor*);

    virtual void gather( const Hermes::MemAddr&, uint32_t sendcnt,
        MP::PayloadDataType sendtype,
        const Hermes::MemAddr&, uint32_t recvcnt,
        MP::PayloadDataType recvtype,
        MP::RankID root, MP::Communicator group, MP::Functor*);

    virtual void gatherv( const Hermes::MemAddr&, uint32_t sendcnt,
        MP::PayloadDataType sendtype,
        const Hermes::MemAddr&, MP::Addr recvcnt, MP::Addr displs,
        MP::PayloadDataType recvtype,
        MP::RankID root, MP::Communicator group, MP::Functor*);

    virtual void barrier(MP::Communicator group, MP::Functor*);

    virtual void alltoall(
        const Hermes::MemAddr&, uint32_t sendcnt,
                        MP::PayloadDataType sendtype,
        const Hermes::MemAddr&, uint32_t
                        recvcnt, MP::PayloadDataType recvtype,
        MP::Communicator group, MP::Functor*);

    virtual void alltoallv(
        const Hermes::MemAddr&, MP::Addr sendcnts,
            MP::Addr senddispls, MP::PayloadDataType sendtype,
        const Hermes::MemAddr&, MP::Addr recvcnts,
            MP::Addr recvdispls, MP::PayloadDataType recvtype,
        MP::Communicator group, MP::Functor*);

    virtual void probe(MP::RankID source, uint32_t tag,
        MP::Communicator group, MP::MessageResponse* resp,
        MP::Functor*);

    // Added (but unused) to avoid compile warning on overloaded virtual function
    virtual void probe( int source, uint32_t tag,
        MP::Communicator group, MP::MessageResponse* resp, MP::Functor* ) {}

	virtual void cancel( MP::MessageRequest req, MP::Functor* );

    virtual void wait(MP::MessageRequest req,
        MP::MessageResponse* resp, MP::Functor*);

    virtual void waitany( int count, MP::MessageRequest req[], int *index,
                 MP::MessageResponse* resp, MP::Functor* );

    virtual void waitall( int count, MP::MessageRequest req[],
                 MP::MessageResponse* resp[], MP::Functor* );

    virtual void test(MP::MessageRequest req, int* flag,
        MP::MessageResponse* resp, MP::Functor*);

	virtual void testany( int count, MP::MessageRequest req[], int* indx, int* flag,
		   MP::MessageResponse* resp, MP::Functor* );

    // Added (but unused) to avoid compile warning on overloaded virtual function
    virtual void test(MP::MessageRequest* req, int& flag, MP::MessageResponse* resp,
        MP::Functor* ) {};

    virtual void comm_split( MP::Communicator, int color, int key,
        MP::Communicator*, MP::Functor* );

    virtual void comm_create( MP::Communicator, size_t nRanks, int* ranks,
        MP::Communicator*, MP::Functor* );

    virtual void comm_destroy( MP::Communicator, MP::Functor* );

  private:
    struct SharpKey {
        int rank;
        uint32_t group;
        uint64_t collectiveId;

        bool operator==( const SharpKey& o ) const {
            return rank == o.rank && group == o.group && collectiveId == o.collectiveId;
        }
    };

    struct SharpKeyHash {
        std::size_t operator()( const SharpKey& key ) const {
            std::size_t h1 = std::hash<int>{}( key.rank );
            std::size_t h2 = std::hash<uint32_t>{}( key.group );
            std::size_t h3 = std::hash<uint64_t>{}( key.collectiveId );
            return h1 ^ ( h2 << 1 ) ^ ( h3 << 3 );
        }
    };

    enum class SharpType : uint32_t { Data = 0, Ack = 1 };

    struct SharpPktHdr {
        uint32_t sharpType;
        uint32_t group;
        uint64_t collectiveId;
        uint64_t segId;
        uint32_t segmentBytes;
        int32_t srcRank;
        int32_t dstRank;
    };

    struct SharpReqState {
        uint64_t expectedAcks = 0;
        uint64_t ackCount = 0;
        std::set<uint64_t> ackedSegments;
        MP::Functor* retFunc = nullptr;
    };

    struct SharpRecvCtx {
        SharpType type;
        SharpKey key;
        uint64_t segId;
        std::vector<uint8_t> payload;
        MP::MessageRequest req = nullptr;
    };

    static constexpr uint64_t m_sharpSegmentBytes = 1024;
    static constexpr uint32_t m_sharpTagBase = 0x5A000000u;

    uint32_t makeSharpTag( SharpType type, Communicator group, uint64_t collectiveId, uint64_t segId ) const;
    bool handleSharpRecv( int retval, SharpRecvCtx* ctx );
    bool handleSharpSendDone( int retval, SharpPktHdr* hdr );
    void scheduleSharpCompletion( MP::Functor* retFunc );
    void processSharpCompletions();

    Output  m_dbg;
	Output& dbg() { return m_dbg; }
	FunctionSM& functionSM() { return m_os->getFunctionSM(); }
	Hades*	    m_os;

    std::unordered_map<SharpKey, SharpReqState, SharpKeyHash> m_sharpReqMap;
    std::deque<MP::Functor*> m_sharpCompletionQ;
    bool m_sharpCompletionScheduled = false;
};

} // namesapce Firefly
} // namespace SST

#endif
