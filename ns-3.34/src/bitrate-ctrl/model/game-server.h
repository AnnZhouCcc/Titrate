#ifndef GAME_SERVER_H
#define GAME_SERVER_H

#include "packet-sender.h"
#include "packet-tcp-sender.h"
#include "packet-udp-sender.h"
#include "video-encoder.h"
#include "ns3/fec-policy.h"
#include "ns3/application.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/simulator.h"
#include "ns3/assert.h"
#include "ns3/socket.h"
#include <vector>
#include <deque>
#include <unordered_map>

namespace ns3 {

class PacketSender;

enum CC_ALG { NOT_USE_CC, GCC, NADA, SCREAM, Copa, ABC, Bbr, Cubic};

class GameServer : public Application {

/* Override Application */
public:
    static TypeId GetTypeId (void);
    /**
     * \brief Construct a new GameServer object
     *
     * \param fps Default video output config: frames per second
     * \param delay_ddl
     * \param bitrate in Kbps
     * \param pacing_flag
     */
    GameServer();
    //GameServer(uint8_t, Time, uint32_t, bool);
    ~GameServer();
    void Setup(
        Ipv4Address srcIP, uint16_t srcPort, Ipv4Address destIP, uint16_t destPort,
        Time delayDdl, uint16_t interval, std::string fecPolicy, 
        bool tcpEnabled, Ipv4Header::DscpType dscp, Ptr<OutputStreamWrapper> appTrStream, 
        Ptr<OutputStreamWrapper> bwTrStream
    );
    void SetController(std::string cca);
protected:
    void DoDispose();
private:
    void StartApplication();
    void StopApplication();

/* Packet sending logic */
private:
    Ptr<PacketSender> m_sender;       /* Pacing, socket operations */
    Ptr<VideoEncoder> m_encoder;      /* Provides encoded video data */
    Ptr<FECPolicy> m_policy;          /* FEC Policy */

    uint32_t kMinEncodeBps;
    uint32_t kMaxEncodeBps;

    uint32_t m_frameId;  /* accumulated */
    uint8_t fps;        /* video fps */
    uint32_t bitrate;   /* in Kbps */
    uint16_t interval;  /* ms, pass the info to packet sender */
    bool pacing_flag;   /* flag for pacing */

    Time check_rtx_interval;
    EventId check_rtx_event; /* Timer for retransmisstion */

    Ipv4Address m_srcIP;
    uint16_t m_srcPort;
    Ipv4Address m_destIP;
    uint16_t m_destPort;

    /* Congestion Control-related variables */
    std::string m_cca;
    std::string m_proto;
    
    Time m_cc_interval;

    FECPolicy::FECParam m_curr_fecparam; /* record the latest FEC parameters for encoding bitrate convertion */

    float m_goodput_ratio; /* (data pkt number / all pkts sent) in a time window */

    Ptr<OutputStreamWrapper> m_appTrStream;

    uint32_t m_curBitrateBps;

    uint32_t m_sendFrameCnt;

    uint32_t GetNextFrameId ();

public:
    /**
     * \brief A frame of encoded data is provided by encoder. Called every frame.
     *
     * \param buffer Pointer to the start of encoded data
     * \param size Length of the data in bytes
     */
    void SendFrame(uint8_t * buffer, uint32_t size);

    /**
     * \brief For packet_sender to get the UDP socket
     *
     * \return Ptr<Socket> a UDP socket
     */
    Ptr<Socket> GetSocket();

    /**
     * @brief Update sending bitrate in CC controller and report to video encoder.
     *
     */
    void UpdateBitrate(uint32_t);

    uint32_t GetBitrateBps ();

    /**
     * @brief Print group info
     */
    void OutputStatistics();

};  // class GameServer

};  // namespace ns3

#endif  /* GAME_SERVER_H */