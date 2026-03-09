#include <sst_config.h>

#include "nvswitch_collective_endpoint.h"
#include "sst/elements/firefly/merlinEvent.h"

using namespace SST::Merlin;
using namespace SST::Interfaces;
using SST::Firefly::FireflyNetworkEvent;

NVSwitchCollectiveEndpoint::NVSwitchCollectiveEndpoint(ComponentId_t id, Params& params) :
    Component(id), m_link(nullptr), m_numGpu(params.find<int>("num_gpu", -1)),
    m_numSwitches(params.find<int>("num_nvswitches", -1)), m_switchIndex(params.find<int>("switch_index", 0)),
    m_nodeId(params.find<int>("node_id", 0))
{
    m_out.init("@t:NVSwitchCollective::@p():@l ", params.find<uint32_t>("verbose",0), 0, Output::STDOUT);

    m_link = loadUserSubComponent<SimpleNetwork>("networkIF", ComponentInfo::SHARE_NONE, 1);
    if (!m_link) {
        Params p;
        p.insert("link_bw", "400GB/s");
        p.insert("input_buf_size", "64kB");
        p.insert("output_buf_size", "64kB");
        p.insert("port_name", "rtr");
        m_link = loadAnonymousSubComponent<SimpleNetwork>("merlin.linkcontrol", "networkIF", 0,
            ComponentInfo::SHARE_PORTS | ComponentInfo::INSERT_STATS, p, 1);
    }

    m_link->setNotifyOnReceive(new SimpleNetwork::Handler2<NVSwitchCollectiveEndpoint,&NVSwitchCollectiveEndpoint::handleRecv>(this));
    m_link->setNotifyOnSend(new SimpleNetwork::Handler2<NVSwitchCollectiveEndpoint,&NVSwitchCollectiveEndpoint::handleSend>(this));
}

NVSwitchCollectiveEndpoint::~NVSwitchCollectiveEndpoint() { delete m_link; }
void NVSwitchCollectiveEndpoint::init(unsigned int phase) { m_link->init(phase); }
void NVSwitchCollectiveEndpoint::setup() { m_link->setup(); }
void NVSwitchCollectiveEndpoint::finish() { m_link->finish(); }

bool NVSwitchCollectiveEndpoint::handleSend(int vn) { (void)vn; trySend(); return true; }

void NVSwitchCollectiveEndpoint::trySend() {
    while (!m_pending.empty()) {
        auto* req = m_pending.front();
        if (!m_link->spaceToSend(req->vn, req->size_in_bits)) break;
        m_pending.pop_front();
        m_link->send(req, req->vn);
    }
}

void NVSwitchCollectiveEndpoint::completeSegment(const Key& key, const State& st) {
    for (int g = 0; g < m_numGpu; ++g) {
        auto* req = new SimpleNetwork::Request(g, m_nodeId, (sizeof(SharpHdr) + st.segBytes) * 8, true, true);
        req->vn = 0;

        SharpHdr hdr { kMagic, 2, key.group, key.cid, key.seg, st.numSeg, st.segBytes, static_cast<uint32_t>(m_switchIndex) };
        auto* ff = new FireflyNetworkEvent();
        ff->bufAppend(&hdr, sizeof(hdr));
        std::vector<uint8_t> zeros(st.segBytes, 0);
        ff->bufAppend(zeros.data(), zeros.size());
        ff->setHdr();
        ff->setTail();
        req->givePayload(ff);
        m_pending.push_back(req);
    }
    m_out.debug(CALL_INFO,1,0,"complete group=%u cid=%" PRIu64 " seg=%" PRIu64 " count=%u\n", key.group, key.cid, key.seg, st.count);
    trySend();
}

bool NVSwitchCollectiveEndpoint::handleRecv(int vn) {
    while (auto* req = m_link->recv(vn)) {
        Event* payload = req->takePayload();
        auto* ff = static_cast<FireflyNetworkEvent*>(payload);
        if (!ff || ff->bufSize() < sizeof(SharpHdr)) {
            delete ff; delete req; continue;
        }

        SharpHdr hdr;
        memcpy(&hdr, ff->bufPtr(), sizeof(hdr));
        if (hdr.magic != kMagic || hdr.kind != 1 || hdr.srcGpu >= (uint32_t)m_numGpu) {
            delete ff; delete req; continue;
        }

        Key key { hdr.group, hdr.collectiveId, hdr.segId };
        auto& st = m_state[key];
        if (st.seen.empty()) st.seen.assign(m_numGpu, 0);
        st.segBytes = hdr.segmentBytes;
        st.numSeg = hdr.numSeg;
        if (!st.seen[hdr.srcGpu]) { st.seen[hdr.srcGpu] = 1; st.count++; }

        if (st.count == (uint32_t)m_numGpu) {
            State done = st;
            m_state.erase(key);
            completeSegment(key, done);
        }

        delete ff;
        delete req;
    }
    return true;
}
