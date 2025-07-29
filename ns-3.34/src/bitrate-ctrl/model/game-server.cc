#include "game-server.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("GameServer");

TypeId GameServer::GetTypeId() {
    static TypeId tid = TypeId ("ns3::GameServer")
        .SetParent<Application> ()
        .SetGroupName("bitrate-ctrl")
        .AddConstructor<GameServer>()
    ;
    return tid;
};

GameServer::GameServer()
: m_sender      {NULL}
, m_encoder     {NULL}
, kMinEncodeBps {(uint32_t) 100E3}
, kMaxEncodeBps {(uint32_t) 100E6}
, m_frameId      {0}
, fps           {0}
, bitrate       {0}
, interval      {0}
, m_srcIP       {}
, m_srcPort     {0}
, m_destIP      {}
, m_destPort    {0}
, m_cca         {"Fixed"}
, m_proto       {"Udp"}
{};

GameServer::~GameServer() {};

void GameServer::Setup (Ipv4Address srcIP, uint16_t srcPort, Ipv4Address destIP, uint16_t destPort,
    Time delayDdl, uint16_t interval, std::string fecPolicy,
    bool tcpEnabled, Ipv4Header::DscpType dscp, 
    Ptr<OutputStreamWrapper> appTrStream, Ptr<OutputStreamWrapper> bwTrStream) 
{
    // std::cout << "[GameServer] Server ip: " << srcIP << ", server port: " << srcPort << ", Client ip: " << destIP <<  ", dstPort: " << destPort << '\n';
    m_srcIP = srcIP;
    m_srcPort = srcPort;
    m_destIP = destIP;
    m_destPort = destPort;
    // init modules
    m_encoder = Create<DumbVideoEncoder> (1000 / interval, bitrate, this, &GameServer::SendFrame);
    m_appTrStream = appTrStream;
    if (tcpEnabled) {
        Ptr<PacketTcpSender> tcpSender = CreateObject<PacketTcpSender> ();
        tcpSender->SetParams (this, &GameServer::UpdateBitrate, interval, delayDdl, 
            srcIP, srcPort, destIP, destPort, dscp, kMinEncodeBps, kMaxEncodeBps, bwTrStream); 
        m_sender = tcpSender;
    } else {
        Ptr<PacketUdpSender> udpSender = CreateObject<PacketUdpSender> ();
        udpSender->SetParams (this, &GameServer::UpdateBitrate, fecPolicy, srcIP, srcPort, 
            destIP, destPort, dscp, fps, delayDdl, kMinEncodeBps / 1000, interval, bwTrStream);
        m_sender = udpSender;
    }
    m_sendFrameCnt = 0;
};

void GameServer::SetController(std::string cca) {
    m_cca = cca;
}

void GameServer::DoDispose() {};

void GameServer::StartApplication() {
    m_sender->StartApplication(GetNode());
    m_sender->SetController(m_cca);
    m_encoder->StartEncoding();
};

void GameServer::StopApplication() {
    NS_LOG_ERROR("\n\n[Server] Stopping GameServer...");
    m_sender->StopRunning();
    m_encoder->StopEncoding();
};

uint32_t GameServer::GetNextFrameId () { return m_frameId ++; };


void GameServer::SendFrame(uint8_t * buffer, uint32_t data_size) {
    if(data_size == 0)
        return;
    
    m_sendFrameCnt ++;

    /* 1. Create data packets of a frame */
    // calculate the num of data packets needed
    uint32_t frame_id = GetNextFrameId();
    uint16_t pkt_id = 0;
    uint16_t data_pkt_max_payload = DataPacket::GetMaxPayloadSize();
    uint16_t data_pkt_num = data_size / data_pkt_max_payload
         + (data_size % data_pkt_max_payload != 0);    /* ceiling division */
    *m_appTrStream->GetStream () << Simulator::Now().GetMilliSeconds() << 
        " FlowId " << m_srcPort <<
        " Send frame " << frame_id << std::endl;
    m_sender->CreateFrame(frame_id, pkt_id, data_pkt_max_payload, data_pkt_num, data_size);
}

void GameServer::UpdateBitrate(uint32_t encodeRateBps){
    encodeRateBps = std::max(encodeRateBps, kMinEncodeBps);
    encodeRateBps = std::min(encodeRateBps, kMaxEncodeBps);
    m_curBitrateBps = encodeRateBps;
    m_encoder->SetBitrate(encodeRateBps >> 10);
};

uint32_t GameServer::GetBitrateBps () { return m_curBitrateBps; }

}; // namespace ns3