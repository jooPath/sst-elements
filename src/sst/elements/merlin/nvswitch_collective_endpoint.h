#ifndef COMPONENTS_MERLIN_NVSWITCH_COLLECTIVE_ENDPOINT_H
#define COMPONENTS_MERLIN_NVSWITCH_COLLECTIVE_ENDPOINT_H

#include <unordered_map>
#include <deque>
#include <vector>

#include <sst/core/component.h>
#include <sst/core/interfaces/simpleNetwork.h>

namespace SST { namespace Firefly { class FireflyNetworkEvent; } }

namespace SST { namespace Merlin {

class NVSwitchCollectiveEndpoint : public SST::Component {
public:
    SST_ELI_REGISTER_COMPONENT(
        NVSwitchCollectiveEndpoint,
        "merlin",
        "nvswitch_collective_endpoint",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "In-switch collective endpoint",
        COMPONENT_CATEGORY_NETWORK)

    SST_ELI_DOCUMENT_PARAMS(
        {"num_gpu", "Number of GPUs", ""},
        {"num_nvswitches", "Number of switches", ""},
        {"switch_index", "Local switch index", "0"},
        {"node_id", "Node id used as source in replies", "0"},
        {"verbose", "Verbose", "0"}
    )

    SST_ELI_DOCUMENT_PORTS(
        {"rtr", "Port to local router", {"merlin.linkcontrol"}}
    )

    NVSwitchCollectiveEndpoint(ComponentId_t id, Params& params);
    ~NVSwitchCollectiveEndpoint() override;

    void init(unsigned int phase) override;
    void setup() override;
    void finish() override;

private:
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

    struct Key {
        uint16_t group; uint64_t cid; uint64_t seg;
        bool operator==(const Key& o) const { return group==o.group && cid==o.cid && seg==o.seg; }
    };
    struct KeyHash { size_t operator()(const Key& k) const { return std::hash<uint64_t>{}(((uint64_t)k.group<<48) ^ (k.cid<<1) ^ k.seg); } };
    struct State { std::vector<uint8_t> seen; uint32_t count=0; uint32_t segBytes=0; uint64_t numSeg=0; };

    bool handleRecv(int vn);
    bool handleSend(int vn);
    void trySend();
    void completeSegment(const Key& key, const State& st);

    Interfaces::SimpleNetwork* m_link;
    Output m_out;
    int m_numGpu, m_numSwitches, m_switchIndex, m_nodeId;

    std::unordered_map<Key, State, KeyHash> m_state;
    std::deque<Interfaces::SimpleNetwork::Request*> m_pending;
};

}}

#endif
