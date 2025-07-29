#include "game-client.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("GameClient");

TypeId GameClient::GetTypeId() {
    static TypeId tid = TypeId ("ns3::GameClient")
        .SetParent<Application> ()
        .SetGroupName("bitrate-ctrl")
        .AddConstructor<GameClient>()
    ;
    return tid;
};

GameClient::GameClient()
: receiver{NULL}
, decoder{NULL}
, fps{0}
, rtt{MilliSeconds(20)}
, m_peerIP{}
, m_peerPort{0}
, m_localPort{0}
, m_receiver_window{32000}
{};

/*
GameClient::GameClient(uint8_t fps, Time delay_ddl) {
    this->InitSocket();
    this->receiver = Create<PacketReceiver> (this, &GameClient::ReceivePacket, this->m_socket);
    this->decoder = Create<VideoDecoder> ();
    this->fps = fps;
    this->delay_ddl = delay_ddl;
};*/

GameClient::~GameClient() {

};

void GameClient::Setup (Ipv4Address srcIP, uint16_t srcPort, uint16_t destPort, uint16_t interval, 
    Time delayDdl, bool tcpEnabled, Ptr<OutputStreamWrapper> appTrStream) {
    m_peerIP = srcIP;
    m_peerPort = srcPort;
    m_localPort = destPort;
    this->decoder = Create<VideoDecoder> (delayDdl, 1000 / interval, this, destPort, appTrStream, &GameClient::ReplyFrameACK);
    this->delay_ddl = delayDdl;
    this->interval = interval;
    if (tcpEnabled) {
        this->receiver = Create<PacketTcpReceiver> (this, this->decoder, this->interval, this->m_localPort);
    }
    else {
        this->receiver = Create<PacketUdpReceiver> (m_receiver_window, this->interval, this->m_peerIP, this->m_peerPort, this->m_localPort);
    }
};

void GameClient::DoDispose() {

};

void GameClient::StartApplication(void) {
    std::cout<<"receiver start"<<std::endl;
    this->receiver->StartApplication(this, this->decoder, this->delay_ddl, GetNode());
};

void GameClient::StopApplication(void) {
    NS_LOG_ERROR("\n[Client] Stopping GameClient...");
    this->receiver->StopRunning();
};

void GameClient::ReplyFrameACK(uint32_t frame_id, Time frame_encode_time) {
    Ptr<FrameAckPacket> pkt = Create<FrameAckPacket>(frame_id, frame_encode_time);
    if(!this->m_cc_create){
        this->receiver->SendPacket(pkt);
    }
};

}; // namespace ns3