#ifndef PACKET_UDP_SENDER_H
#define PACKET_UDP_SENDER_H

#include "packet-sender.h"
#include "ns3/fec-policy.h"
#include "ns3/other-policy.h"

#include "ns3/sender-based-controller.h"
#include "ns3/gcc-controller.h"
#include "ns3/nada-controller.h"

#include "ns3/core-module.h"
#include "ns3/socket.h"
#include "ns3/application.h"
#include "ns3/simulator.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/nstime.h"
#include "ns3/timer.h"
#include <vector>
#include <queue>
#include <memory>

namespace ns3 {

class GameServer;

class LossEstimator : public Object {
public:
    LossEstimator (Time window);
    ~LossEstimator ();
    static TypeId GetTypeId (void);
private:
    std::deque<std::pair<uint16_t, Time>> m_sendList;
    std::deque<std::pair<uint16_t, Time>> m_rtxList;
    Time m_window;
public:
    void SendUpdate (uint16_t num, Time now);
    void RtxUpdate (uint16_t num, Time now);
    double_t GetLoss (Time now);
};

class PacketUdpSender  : public PacketSender{
public:
    static TypeId GetTypeId (void);
    PacketUdpSender (void);
    void SetParams (GameServer * m_gameServer, void (GameServer::*)(uint32_t), std::string fecPolicy, Ipv4Address srcIP, 
        uint16_t srcPort, Ipv4Address destIP, uint16_t destPort, Ipv4Header::DscpType dscp, 
        uint8_t fps, Time delay_ddl, uint32_t bitrate, uint16_t interval, Ptr<OutputStreamWrapper> bwTrStream);
    ~PacketUdpSender();

    void StartApplication(Ptr<Node> node);
    void StopRunning();
    void SetController(std::string cca);
    
    /**
     * \brief Send packets of a Frame to network, called by GameServer
     * \param packets data and FEC packets of a Frame
     */
    void SendFrame(std::vector<Ptr<VideoPacket>>);

    void CreateFrame(uint32_t, uint16_t, uint16_t, uint16_t, uint32_t);

    /**
     * \brief Send retransmission packets
     * \param packets data and FEC packets to be sent immediately
     */
    void SendRtx(std::vector<Ptr<VideoPacket>>);

    /**
     * \brief Caluculate pacing rate and set m_pacing_interval
     */
    void Calculate_pacing_rate();


    void OnSocketRecv_sender(Ptr<Socket>);

    void SetNetworkStatistics(
        Time default_rtt, double_t bw/* in Mbps */,
        double_t loss, double_t group_delay /* in ms */
    );

    void SetNetworkStatisticsBytrace(uint16_t rtt /* in ms */, 
                                    double_t bw/* in Mbps */,
                                    double_t loss_rate);

    Ptr<FECPolicy::NetStat> GetNetworkStatistics();

    double_t GetBandwidthLossRate();

    void UpdateSendingRate(); // apply cc algorithm to calculate the sending bitrate

    double GetGoodputRatio();

    void UpdateGoodputRatio();

    void UpdateRTT(Time rtt);

    void UpdateNetstateByTrace();

    void SetTrace(std::string tracefile);

    std::deque<int64_t> m_time_history;
    
    std::deque<uint32_t> m_tx_history;

protected:
    void SetTimeLimit(const Time& limit);
    void Pause();
    void Resume();

private:
    /**
     * \brief Send a packet to the network
     */
    void SockSendPacket ();

    /**
     * \brief Burstly send packets to the network
     */
    void SendPacket_burst(std::vector<Ptr<VideoPacket>>);

    /**
     * \brief calculate the number of frames in the queue
     * \return the number of frames in the queue
     */
    uint32_t num_frame_in_queue();

private:
    Ptr<FECPolicy> m_fecPolicy;          /* FEC Policy */
    std::string m_rtxPolicy;          /* FEC Policy */
    Ptr<Socket> m_socket; /* UDP socket to send our packets */

    Ptr<FECPolicy::NetStat> m_netStat; /* stats used for FEC para calculation */

    std::vector<Ptr<PacketFrame>> m_queue; /* Queue storing packets by frames */

    //std::vector<Ptr<VideoPacket>> pkts_sent;

    std::unordered_map<uint16_t, Ptr<SentPacketInfo>> pktsHistory; /* Packet history for all packets */
    std::deque<uint16_t> m_send_wnd; // Last ID in sender sliding window
    uint16_t m_send_wnd_size;

    std::deque<uint16_t> m_goodput_wnd;
    uint32_t m_goodput_wnd_size; // in us
    uint64_t goodput_pkts_inwnd;
    uint64_t total_pkts_inwnd;
    float m_goodput_ratio;

    uint32_t m_ackGroupId;
    uint64_t m_groupstart_TxTime;
    int m_group_size;
    uint16_t m_prev_id;
    uint64_t m_prev_RxTime;
    uint16_t m_prev_groupend_id;
    uint64_t m_prev_groupend_RxTime;
    int m_prev_group_size;

    uint16_t m_prev_pkts_in_frame;
    uint16_t m_curr_pkts_in_frame;
    uint64_t m_prev_frame_TxTime; // in us
    uint64_t m_curr_frame_TxTime;
    uint64_t m_prev_frame_RxTime;
    uint64_t m_curr_frame_RxTime;

    uint16_t m_interval;

    int64_t inter_arrival;
    uint64_t inter_departure;
    int64_t  inter_delay_var;
    int      inter_group_size;
    int64_t tx_threshold;

    bool m_firstFeedback;

    std::shared_ptr<rmcat::SenderBasedController> m_controller;
    uint32_t (GameServer::*m_getBitrateBps)(void);

    /* pacing-related variables */
    bool m_pacing; // whether to turn on pacing

    Time m_pacing_interval; /* the time interval before calling the next SendPacket() */

    Timer m_pacingTimer;

    Time m_send_time_limit;

    EventId m_eventSend;

    EventId m_settrace_event;

    uint16_t m_netGlobalId;
    uint16_t m_dataGlobalId;

    uint16_t m_last_acked_global_id;

    uint64_t m_finished_frame_cnt;
    uint64_t m_timeout_frame_cnt;
    uint64_t m_exclude_ten_finished_frame_cnt;

    /* statistics for bandwidth loss rate */
    uint64_t init_data_pkt_count, other_pkt_count;
    uint64_t init_data_pkt_size, other_pkt_size;
    // do not record the first 10 frames
    std::set<uint32_t> first_ten_frame_group_id;
    uint64_t exclude_head_init_data_pkt_count, exclude_head_other_pkt_count;
    uint64_t exclude_head_init_data_pkt_size, exclude_head_other_pkt_size;

    /* statistics setup by trace */
    bool trace_set;
    std::string trace_filename;

    Ptr<OutputStreamWrapper> m_bwTrStream;

    /*gameserver*/
    class UnFECedPackets : public Object {
    public:
        FECPolicy::FECParam param;
        uint16_t next_pkt_id_in_batch;
        uint16_t next_pkt_id_in_group;
        std::vector<Ptr<DataPacket>> pkts;

        static TypeId GetTypeId (void);
        UnFECedPackets();
        ~UnFECedPackets();
        void SetUnFECedPackets(
            FECPolicy::FECParam param,
            uint16_t next_pkt_id_in_batch, uint16_t next_pkt_id_in_group,
            std::vector<Ptr<DataPacket>> pkts
        );
    };
    
    std::deque<Ptr<GroupPacketInfo>> m_dataPktHistoryKey;     /* in time order */

    /* (GroupId, pkt_id_group) -> Packet */
    std::unordered_map<
        uint32_t, std::map<uint16_t, Ptr<DataPacket>>> m_dataPktHistory;    /* Packets sent in the past ddl, temporary */
    std::unordered_map<
        uint32_t, std::deque<uint32_t>> m_frameIdToGroupId; /* frame_id -> group_id */
    /* Record how many data packets there are in a frame */
    /* frame id -> data packet count */
    std::unordered_map<uint32_t, uint8_t> m_frameDataPktCnt;
    uint16_t m_curRxHighestDataGlobalId;
    uint16_t m_curRxHighestGlobalId;
    uint16_t m_curContRxHighestGlobalId;
    bool m_isRecovery;
    std::unordered_map<uint32_t, Time> m_delayedRtxGroup;
    Time m_lastRtt;
    Time m_lastAckTime;

    uint32_t m_nextGroupId;  /* accumulated */
    uint32_t m_nextBatchId;  /* for simplicity, batch_id is also globally incremental */
    uint8_t fps;        /* video fps */
    uint32_t bitrate;   /* in Kbps */
    uint16_t m_frameInterval;  /* ms, pass the info to packet sender */
    Time m_delayDdl;     /* delay ddl */
    bool pacing_flag;   /* flag for pacing */

    Ptr<LossEstimator> m_lossEstimator;
    uint16_t m_measureWindow;

    int m_ccaQuotaPkt;

    FECPolicy::FECParam m_curFecParam; /* record the latest FEC parameters for encoding bitrate convertion */


    // statistics
    uint64_t send_group_cnt;
    uint64_t send_frame_cnt;

    /**
     * \brief Initialize a UDP socket
     *
     */
    void InitSocket();

    uint32_t GetNextGroupId();
    uint32_t GetNextBatchId();
    uint32_t GetNextFrameId();

    /**
     * \brief Create a packet batch from pkt_batch
     *
     * \param pkt_batch Data packets for creating the batch, store the whole batch afterwards
     * \param fec_param FEC parameters
     * \param new_group flag to create a new group
     * \param group_id if new_group==false, use group_id instead of creating a new id
     * \param is_rtx
     */
    void CreatePacketBatch(
        std::vector<Ptr<VideoPacket>>& pkt_batch,
        FECPolicy::FECParam fec_param,
        bool new_group, uint32_t group_id, bool is_rtx
    );

    void CreateFirstPacketBatch(
    std::vector<Ptr<VideoPacket>>& pkt_batch, FECPolicy::FECParam fec_param);
    void CreateRTXPacketBatch(
    std::vector<Ptr<VideoPacket>>& pkt_batch, FECPolicy::FECParam fec_param);

    /**
     * \brief For incomplete group/batch, complete it with new data packets
     *
     * \param new_data_pkts new data packets for creating the batch, store the whole batch afterwards
     * \param fec_pkts Store FEC packets
     */
    void CompletePacketBatch(
        std::vector<Ptr<VideoPacket>>& new_data_pkts, std::vector<Ptr<VideoPacket>>& fec_pkts
    );

    /**
     * \brief Determine whether the packet will definitely exceed delay_ddl
     *
     * \param pkt
     *
     * \return true it will exceed delay_ddl
     * \return false it probably will not
     */
    bool MissesDdl(Ptr<DataPacket> packet);

    Time GetDispersion (Ptr<DataPacket> pkt);

    bool IsRtxTimeout (Ptr<DataPacket> packet, Time rto);


    /**
     * \brief Store data packets for retransmission
     *
     * \param pkts
     */
    void StorePackets(std::vector<Ptr<VideoPacket>> pkts);

    void SendPackets(std::deque<Ptr<DataPacket>> pkts, Time ddl_left, uint32_t frame_id, bool is_rtx);

    void RetransmitGroup(uint32_t);

    /**
     * \brief Check for packets to be retransmitted regularly
     *
     */
    void CheckRetransmission();

    FECPolicy::FECParam GetFECParam(
        uint16_t max_group_size, 
        uint32_t bitrate,
        Time ddl_left, bool fix_group_size,
        bool isRtx, uint8_t frameSize
    );

public:
    /**
     * \brief A frame of encoded data is provided by encoder. Called every frame.
     *
     * \param buffer Pointer to the start of encoded data
     * \param size Length of the data in bytes
     */
    void SendFrame(uint8_t * buffer, uint32_t size);

    /**
     * \brief Process ACK packet
     *
     * \param pkt contains info of the packets that have been received
     */
    void RcvACKPacket(Ptr<AckPacket> pkt);

    void RcvFrameAckPacket (Ptr<FrameAckPacket> frameAckPkt);
    /**
     * \brief For packet_sender to get the UDP socket
     *
     * \return Ptr<Socket> a UDP socket
     */
    Ptr<Socket> GetSocket();

    Time check_rtx_interval;
    EventId check_rtx_event; /* Timer for retransmisstion */
};  // class PacketUdpSender


};  // namespace ns3

#endif  /* PACKET_UDP_SENDER_H */