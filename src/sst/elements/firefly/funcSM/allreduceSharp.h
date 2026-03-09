// Copyright 2013-2025 NTESS.
#ifndef COMPONENTS_FIREFLY_FUNCSM_ALLREDUCE_SHARP_H
#define COMPONENTS_FIREFLY_FUNCSM_ALLREDUCE_SHARP_H

#include <vector>
#include <algorithm>

#include "funcSM/api.h"
#include "funcSM/event.h"
#include "ctrlMsg.h"

namespace SST { namespace Firefly {

class AllreduceSharpFuncSM : public FunctionSMInterface {
public:
    SST_ELI_REGISTER_MODULE(
        AllreduceSharpFuncSM, "firefly", "AllreduceSharp", SST_ELI_ELEMENT_VERSION(1,0,0), "", SST::Firefly::FunctionSMInterface)

    AllreduceSharpFuncSM(SST::Params& params) : FunctionSMInterface(params), m_event(nullptr),
        m_segSize(params.find<uint32_t>("segmentSize", 1024)),
        m_numSwitches(params.find<int>("sharpNumSwitches", -1)),
        m_switchBase(params.find<int>("sharpSwitchBase", -1)),
        m_segId(0), m_numSeg(0), m_segBytes(0), m_switchNode(-1), m_state(SendSeg) {}

    std::string protocolName() override { return "CtrlMsgProtocol"; }

    void handleStartEvent(SST::Event* ev, Retval& retval) override {
        (void)retval;
        m_event = static_cast<AllreduceSharpStartEvent*>(ev);
        m_groupRank = m_info->getGroup(m_event->group)->getMyRank();
        m_groupSize = m_info->getGroup(m_event->group)->getSize();
        if (m_numSwitches <= 0) m_numSwitches = 1;
        if (m_switchBase < 0) m_switchBase = m_groupSize;
        m_numSeg = (m_event->bytes + m_segSize - 1) / m_segSize;
        m_recvBuf.resize(m_segSize + sizeof(SharpHdr));
        m_sendBuf.resize(m_segSize + sizeof(SharpHdr));
        m_state = SendSeg;
        m_segId = 0;
        handleEnterEvent(retval);
    }

    void handleEnterEvent(Retval& retval) override {
        if (m_segId == m_numSeg && m_state == SendSeg) {
            delete m_event; m_event = nullptr; retval.setExit(0); return;
        }

        switch (m_state) {
        case SendSeg:
            prepSegment();
            proto()->send(Hermes::MemAddr(1, m_sendBuf.data()), sizeof(SharpHdr) + m_segBytes, m_switchNode, genTag(), 0);
            m_state = RecvSeg;
            return;
        case RecvSeg:
            proto()->recv(Hermes::MemAddr(1, m_recvBuf.data()), sizeof(SharpHdr) + m_segBytes, m_switchNode, genTag(), m_event->group);
            m_state = FinishSeg;
            return;
        case FinishSeg:
            if (m_event->result.getBacking()) {
                memcpy(static_cast<uint8_t*>(m_event->result.getBacking()) + m_segId * m_segSize,
                       m_recvBuf.data() + sizeof(SharpHdr), m_segBytes);
            }
            ++m_segId;
            m_state = SendSeg;
            handleEnterEvent(retval);
            return;
        }
    }

private:
    enum State { SendSeg, RecvSeg, FinishSeg };
    struct __attribute__((packed)) SharpHdr {
        uint32_t magic;
        uint16_t kind;
        uint16_t group;
        uint64_t collectiveId;
        uint64_t segId;
        uint64_t numSeg;
        uint32_t segmentBytes;
        uint32_t srcGpu;
    };
    static constexpr uint32_t kMagic = 0x53525031;

    void prepSegment() {
        const uint64_t off = m_segId * m_segSize;
        m_segBytes = static_cast<uint32_t>(std::min<uint64_t>(m_segSize, m_event->bytes - off));
        const int switchIndex = static_cast<int>(m_segId % static_cast<uint64_t>(m_numSwitches));
        m_switchNode = m_switchBase + switchIndex;

        SharpHdr hdr { kMagic, 1, (uint16_t)m_event->group, m_event->collectiveId, m_segId, m_numSeg, m_segBytes, (uint32_t)m_groupRank };
        memcpy(m_sendBuf.data(), &hdr, sizeof(hdr));
        memset(m_sendBuf.data() + sizeof(hdr), 0, m_segBytes);
        if (m_event->mydata.getBacking()) {
            memcpy(m_sendBuf.data() + sizeof(hdr), static_cast<uint8_t*>(m_event->mydata.getBacking()) + off, m_segBytes);
        }
    }

    uint64_t genTag() const {
        return (0x7ULL << 60) ^ ((uint64_t)m_event->group << 48) ^ ((m_event->collectiveId & 0xffffULL) << 32) ^ (m_segId & 0xffffffffULL);
    }

    CtrlMsg::API* proto() { return static_cast<CtrlMsg::API*>(m_proto); }

    AllreduceSharpStartEvent* m_event;
    uint32_t m_segSize;
    int m_numSwitches;
    int m_switchBase;
    int m_groupRank;
    int m_groupSize;
    uint64_t m_segId, m_numSeg;
    uint32_t m_segBytes;
    int m_switchNode;
    State m_state;
    std::vector<uint8_t> m_sendBuf, m_recvBuf;
};

}}
#endif
