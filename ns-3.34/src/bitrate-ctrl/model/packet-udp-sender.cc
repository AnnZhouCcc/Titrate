#include "packet-udp-sender.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE("PacketUdpSender");

LossEstimator::LossEstimator (Time window)
: m_window {window} {
  m_sendList.clear ();
  m_rtxList.clear ();
}

LossEstimator::~LossEstimator () {

}

void LossEstimator::SendUpdate (uint16_t num, Time now) {
  m_sendList.push_back (std::make_pair (num, now));
}

void LossEstimator::RtxUpdate (uint16_t num, Time now) {
  m_rtxList.push_back (std::make_pair (num, now));
}

double_t LossEstimator::GetLoss (Time now) {
  /* First clean up the history */
  while (!m_sendList.empty () && m_sendList.front ().second < now - m_window) {
    m_sendList.pop_front ();
  }
  while (!m_rtxList.empty () && m_rtxList.front ().second < now - m_window) {
    m_rtxList.pop_front ();
  }
  if (m_sendList.empty ()) {
    return m_rtxList.empty () ? 0 : 1;
  }
  else {
    double_t rtxSum = 0;
    double_t sendSum = 0;
    for (auto &rtx : m_rtxList) {
      rtxSum += rtx.first;
    }
    for (auto &send : m_sendList) {
      sendSum += send.first;
    }
    return rtxSum / sendSum;
  }
}

TypeId PacketUdpSender::GetTypeId() {
    static TypeId tid = TypeId ("ns3::PacketUdpSender")
        .SetParent<Object> ()
        .SetGroupName ("bitrate-ctrl")
        .AddAttribute ("RtxPolicy", "The policy for retransmission",
            StringValue ("dupack"),
            MakeStringAccessor (&PacketUdpSender::m_rtxPolicy),
            MakeStringChecker ())
        .AddAttribute ("MeasureWindow", "The window of number of frames for loss measurement",
            UintegerValue (2),
            MakeUintegerAccessor (&PacketUdpSender::m_measureWindow),
            MakeUintegerChecker<uint16_t> ())
    ;
    return tid;
};

PacketUdpSender::PacketUdpSender (void)
: PacketSender ()
, m_netStat {NULL}
, m_queue {}
, m_send_wnd {}
, m_send_wnd_size {10000}
, m_goodput_wnd {}
, m_goodput_wnd_size {50000} // 32 ms
, goodput_pkts_inwnd {0}
, total_pkts_inwnd {0}
, m_goodput_ratio {1.}
, m_ackGroupId {0}
, m_groupstart_TxTime {0}
, m_group_size {0}
, m_prev_id {0}
, m_prev_RxTime {0}
, m_prev_groupend_id {0}
, m_prev_groupend_RxTime {0}
, m_prev_group_size {0}
, m_prev_pkts_in_frame{0}
, m_curr_pkts_in_frame{0}
, m_prev_frame_TxTime{0}
, m_curr_frame_TxTime{0}
, m_prev_frame_RxTime{0}
, m_curr_frame_RxTime{0}
, inter_arrival{0}
, inter_departure{0}
, inter_delay_var{0}
, inter_group_size{0}
, m_firstFeedback {true}
, m_controller {NULL}
, m_pacing {false}
, m_pacing_interval {MilliSeconds(0)}
, m_pacingTimer {Timer::CANCEL_ON_DESTROY}
, m_eventSend {}
, m_netGlobalId {0}
, m_dataGlobalId {0}
, m_last_acked_global_id {0}
, m_delayDdl {Seconds (1.)}
, m_finished_frame_cnt {0}
, m_timeout_frame_cnt {0}
, m_exclude_ten_finished_frame_cnt {0}
{
    NS_LOG_INFO ("[Sender] Delay DDL is: " << m_delayDdl.GetMilliSeconds() << " ms");
    // init frame index
    m_nextGroupId = 0;
    m_nextBatchId = 0;
    this->fps = fps;
    this->send_group_cnt = 0;


    this->init_data_pkt_count = 0;
    this->other_pkt_count = 0;
    this->init_data_pkt_size = 0;
    this->other_pkt_size = 0;
    this->exclude_head_init_data_pkt_count = 0;
    this->exclude_head_other_pkt_count = 0;
    this->exclude_head_init_data_pkt_size = 0;
    this->exclude_head_other_pkt_size = 0;

    m_fecPolicy = CreateObject<RtxOnlyPolicy> ();
        
    this->check_rtx_interval = MicroSeconds(1e3); // check for retransmission every 1ms

    double defaultRtt = 50;
    double defaultBw  = 2;
    SetNetworkStatistics (MilliSeconds(defaultRtt), defaultBw, 0, 0);
};

void PacketUdpSender::SetParams (
    GameServer * gameServer, void (GameServer::*EncodeFunc)(uint32_t encode_bitrate),
    std::string fecPolicy, Ipv4Address srcIP, uint16_t srcPort, Ipv4Address destIP, uint16_t destPort, Ipv4Header::DscpType dscp, 
    uint8_t fps, Time delay_ddl, uint32_t bitrate, uint16_t interval, Ptr<OutputStreamWrapper> bwTrStream
) {
    PacketSender::SetParams (gameServer, EncodeFunc, MilliSeconds(40), srcIP, srcPort, destIP, destPort, dscp);
    m_interval = interval;
    m_send_time_limit = MilliSeconds (interval);
    this->bitrate = bitrate;
    m_delayDdl = delay_ddl;
    m_lossEstimator = Create<LossEstimator> (MilliSeconds (m_measureWindow * m_interval));
    m_bwTrStream = bwTrStream;
}

PacketUdpSender::~PacketUdpSender () {};

void PacketUdpSender::SetNetworkStatistics (
    Time defaultRtt, double_t bw, double_t loss, double_t delay
) {
    if (m_netStat == NULL)
        m_netStat = Create<FECPolicy::NetStat>();
    // initialize netstat
    m_netStat->curRtt           = defaultRtt;
    m_netStat->srtt             = defaultRtt;
    m_netStat->minRtt           = defaultRtt;
    m_netStat->rttSd            = Time (0);
    m_netStat->curBw            = bw;
    m_netStat->curLossRate      = loss;
    m_netStat->oneWayDispersion = MicroSeconds ((uint64_t) (delay * 1e3));
    // randomly generate loss seq
    m_netStat->loss_seq.clear();
    int next_seq = 0;
    for (uint16_t i = 0;i < bw * 1e6 / 8 * 0.1 / 1300/* packet count in 100ms */;i++) {
        bool lost = bool(std::rand() % 2);
        if (lost) {
            if(next_seq <= 0) {
                next_seq --;
            } else {
                m_netStat->loss_seq.push_back(next_seq);
                next_seq = 0;
            }
        } else {    // not lost
            if (next_seq >= 0) {
                next_seq ++;
            } else {
                m_netStat->loss_seq.push_back(next_seq);
                next_seq = 0;
            }
        }
    }
};

void PacketUdpSender::UpdateRTT (Time rtt) {
    if (m_netStat->srtt == Time (0))
        m_netStat->srtt = rtt;
    // EWMA formulas are implemented as suggested in
    // Jacobson/Karels paper appendix A.2
    
    double m_alpha = 0.125, m_beta = 0.25;
    // SRTT <- (1 - alpha) * SRTT + alpha *  R'
    Time rttErr (rtt - m_netStat->srtt);
    double gErr = rttErr.ToDouble (Time::S) * m_alpha;
    m_netStat->srtt += Time::FromDouble (gErr, Time::S);

    // RTTVAR <- (1 - beta) * RTTVAR + beta * |SRTT - R'|
    Time rttDifference = Abs (rttErr) - m_netStat->rttSd;
    m_netStat->rttSd += rttDifference * m_beta;

    m_netStat->curRtt = rtt;
    m_netStat->minRtt = Min (m_netStat->minRtt, rtt);
}

void PacketUdpSender::SetNetworkStatisticsBytrace(uint16_t rtt /* in ms */, 
                                double_t bw/* in Mbps */,
                                double_t loss_rate)
{
    UpdateRTT (MilliSeconds (rtt));
    m_netStat->curBw = bw;
    m_netStat->curLossRate = loss_rate;
    if (m_controller)
        m_controller->UpdateLossRate (uint8_t (loss_rate * 256));
};

void PacketUdpSender::StartApplication(Ptr<Node> node) {
    if (m_socket == NULL) {
        m_socket = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());

        InetSocketAddress local = InetSocketAddress(m_srcIP, m_srcPort);
        InetSocketAddress remote = InetSocketAddress(m_destIP, m_destPort);
        local.SetTos (m_dscp << 2);
        remote.SetTos (m_dscp << 2);
        m_socket->Bind(local);
        m_socket->Connect(remote);
        m_socket->SetRecvCallback(MakeCallback(&PacketUdpSender::OnSocketRecv_sender,this));
    }
    PacketSender::StartApplication(node);

    m_pacingTimer.SetFunction(&PacketUdpSender::SockSendPacket,this);
    m_pacingTimer.SetDelay(m_pacing_interval);
    
    m_lastAckTime = Simulator::Now ();
    if(this->trace_set){
        this->UpdateNetstateByTrace();
    }
    this->check_rtx_event = Simulator::Schedule(this->check_rtx_interval, &PacketUdpSender::CheckRetransmission, this);
};

void PacketUdpSender::StopRunning(){
    GetBandwidthLossRate();
    this->check_rtx_event.Cancel();
    PacketSender::StopRunning();
    m_socket->Close();
}

void PacketUdpSender::SetController (std::string cca) {
    if (cca == "Gcc") {
        m_controller = std::make_shared<rmcat::GccController> ();
        m_ccEnabled = true;
    }
    else if (cca == "Nada") {
        m_controller = std::make_shared<rmcat::NadaController> ();
        m_ccEnabled = true;
    }
    else if (cca == "Fixed") {
        m_ccEnabled = false;
    }
    else {
        NS_ABORT_MSG ("Congestion control algorithm " + cca + " not implemented.");
    }
}

uint32_t PacketUdpSender::GetNextGroupId() { return m_nextGroupId ++; };
uint32_t PacketUdpSender::GetNextBatchId() { return m_nextBatchId ++; };

TypeId PacketUdpSender::UnFECedPackets::GetTypeId() {
    static TypeId tid = TypeId ("ns3::PacketUdpSender::UnFECedPackets")
        .SetParent<Object> ()
        .SetGroupName("bitrate-ctrl")
        .AddConstructor<PacketUdpSender::UnFECedPackets> ()
    ;
    return tid;
};

PacketUdpSender::UnFECedPackets::UnFECedPackets()
: param {}
, next_pkt_id_in_batch {0}
, next_pkt_id_in_group {0}
, pkts {}
{};

PacketUdpSender::UnFECedPackets::~UnFECedPackets() {};

void PacketUdpSender::UnFECedPackets::SetUnFECedPackets(
    FECPolicy::FECParam param,
    uint16_t next_pkt_id_in_batch, uint16_t next_pkt_id_in_group,
    std::vector<Ptr<DataPacket>> pkts
) {
    this->param = param;
    this->next_pkt_id_in_batch = next_pkt_id_in_batch;
    this->next_pkt_id_in_group = next_pkt_id_in_group;
    this->pkts = pkts;
};

void PacketUdpSender::CreateFrame(uint32_t frame_id, uint16_t pkt_id, uint16_t data_pkt_max_payload, uint16_t data_pkt_num, uint32_t data_size){
    std::deque<Ptr<DataPacket>> data_pkt_queue;

    // create packets
    for(uint32_t data_ptr = 0; data_ptr < data_size; data_ptr += data_pkt_max_payload) {
        Ptr<DataPacket> data_pkt = Create<DataPacket>(frame_id, 0, data_pkt_num, pkt_id);
        data_pkt->SetEncodeTime(Simulator::Now());
        data_pkt->SetPayload(nullptr, MIN(data_pkt_max_payload, data_size - data_ptr));
        data_pkt_queue.push_back(data_pkt);
        pkt_id = pkt_id + 1;
    }
    // record the number of data packets in a single frame
    m_frameDataPktCnt[frame_id] = data_pkt_num;

    NS_ASSERT(data_pkt_queue.size() > 0);
    /* End of 1. Create data packets of a frame */

    SendPackets (
        data_pkt_queue,
        m_delayDdl - data_pkt_queue.size () * m_netStat->oneWayDispersion,
        frame_id,
        false
    );
}

void PacketUdpSender::SendPackets (std::deque<Ptr<DataPacket>> pkts, Time ddlLeft, uint32_t frameId, bool isRtx) {

  m_ccaQuotaPkt -= pkts.size ();

  std::vector<Ptr<VideoPacket>> pktToSendList;
  std::vector<Ptr<VideoPacket>> tmpList;
  FECPolicy::FECParam fecParam;

  if (m_frameDataPktCnt.find (frameId) == m_frameDataPktCnt.end()) {
    NS_ASSERT_MSG(false, "No frame size info");
  }
  uint8_t frameSize = m_frameDataPktCnt[frameId];
  // Not retransmission packets:
  // Group the packets as fec_param's optimal
  if (!isRtx) {
    // Get a FEC parameter in advance to divide packets into groups
    // Default
    m_frameIdToGroupId[frameId].clear ();
    fecParam = GetFECParam (pkts.size(), m_bitrate >> 10, ddlLeft, false, isRtx, frameSize);
    NS_LOG_FUNCTION ("fecParam " << Simulator::Now ().GetMilliSeconds () << 
      " loss " << m_netStat->curLossRate <<
      " frameSize " << (int) frameSize <<
      " ddlLeft " << ddlLeft <<
      " rtt " << m_netStat->srtt.GetMilliSeconds () <<
      " dispersion " << m_netStat->oneWayDispersion.GetMilliSeconds () <<
      " blockSize " << fecParam.fec_group_size <<
      " bitRate " << (m_bitrate >> 10) <<
      " fecRate " << fecParam.fec_rate);

    // divide packets into groups
    while (pkts.size() >= fecParam.fec_group_size) {
      tmpList.clear();

      // get fec_param.group_size data packets from pkts
      for (uint16_t i = 0; i < fecParam.fec_group_size; i++) {
        tmpList.push_back (pkts.front());
        pkts.pop_front ();
      }

      // group them into a pkt_batch
      CreateFirstPacketBatch (tmpList, fecParam);

      // insert them into sending queue
      pktToSendList.insert (pktToSendList.end (), tmpList.begin(), tmpList.end());
    }
  }
  if (!pkts.empty ()) {
  /* Retransmission packets and tail packets:
     Group the remaininng packets as a single group */
    tmpList.clear();

    // not pacing
    // send out all packets
    // get all data packets left from data_pkt_queue
    fecParam = GetFECParam (pkts.size(), m_bitrate >> 10, ddlLeft, true, isRtx, frameSize);
    while (!pkts.empty ()) {
      tmpList.push_back (pkts.front());
      pkts.pop_front ();
    }
    // group them into a pkt_batch!
    if (isRtx) 
      CreateRTXPacketBatch (tmpList, fecParam); /* rtx packets */
    else 
      CreateFirstPacketBatch (tmpList, fecParam);   /* remaining tail packets that cannot form a full fec group */
    pktToSendList.insert (pktToSendList.end (), tmpList.begin(), tmpList.end());
  }
  
  NS_ASSERT (!pktToSendList.empty ());

  // Set enqueue time
  for (auto pkt : pktToSendList)
    pkt->SetEnqueueTime (Simulator::Now());
  // Send out packets
  if (isRtx) 
    SendRtx (pktToSendList);
  else 
    SendFrame (pktToSendList);
  // store data packets in case of retransmissions
  StorePackets (pktToSendList);
  m_lossEstimator->SendUpdate (pktToSendList.size (), Simulator::Now ());
}

void PacketUdpSender::StorePackets (std::vector<Ptr<VideoPacket>> pkts) {
  // store data packets in case of retransmission
  for (auto pkt : pkts) {
    if (pkt->GetPacketType() != PacketType::DATA_PKT)
      continue;
    Ptr<DataPacket> dataPkt = DynamicCast<DataPacket, VideoPacket> (pkt);
    Ptr<GroupPacketInfo> info = Create<GroupPacketInfo> (pkt->GetGroupId (), pkt->GetPktIdGroup (), 
      dataPkt->GetDataGlobalId (), dataPkt->GetGlobalId ());
    info->m_sendTime = Simulator::Now ();
    m_dataPktHistoryKey.push_back (info);
    m_dataPktHistory[info->m_groupId][info->m_pktIdInGroup] = dataPkt;
  }
};

Time PacketUdpSender::GetDispersion (Ptr<DataPacket> pkt) {
  uint32_t batchSize = pkt->GetBatchDataNum () + pkt->GetBatchFECNum ();
  return std::min (batchSize * m_netStat->oneWayDispersion + MicroSeconds (500), MilliSeconds (m_frameInterval));
}

bool PacketUdpSender::IsRtxTimeout (Ptr<DataPacket> pkt, Time rto) {
  Time now = Simulator::Now ();
  Time enqueueTime = pkt->GetEnqueueTime ();
  Time lastSendTime = pkt->GetSendTime ();
  // Decide if the packet needs to be retransmitted
  if (rto == Time (0)) {
    /* rto = max (avg + 4 * stdev, 2 * avg) 
       basically following the pto plan from 
       RFC 8985 - The RACK-TLP Loss Detection Algorithm for TCP
       https://datatracker.ietf.org/doc/rfc8985/
    */
    rto = Max (m_netStat->srtt + 4 * m_netStat->rttSd, 2 * m_netStat->srtt);
  }
  rto += GetDispersion (pkt) + MicroSeconds (500);
  return (now > enqueueTime && (now - lastSendTime > rto));
};


bool PacketUdpSender::MissesDdl (Ptr<DataPacket> pkt) {
  Time now = Simulator::Now ();
  Time encodeTime = pkt->GetEncodeTime ();
  /* Decide if it's gonna miss ddl. Could be more strict */
  return (now - encodeTime + m_netStat->minRtt / 2 > m_delayDdl);
};

void PacketUdpSender::CreatePacketBatch(
  std::vector<Ptr<VideoPacket>>& pkt_batch,
  FECPolicy::FECParam fec_param,
  bool new_group, uint32_t group_id, bool is_rtx) {

  // data packets
  std::vector<Ptr<DataPacket>> data_pkts;
  for(auto pkt : pkt_batch)
    data_pkts.push_back(DynamicCast<DataPacket, VideoPacket> (pkt));

  // FEC paramters
  double_t fec_rate =fec_param.fec_rate;

  // new_group cannot be true when new_batch is false
  NS_ASSERT(data_pkts.size() > 0);

  // if force_create_fec -> create FEC packets even though data packet num is less than fec_param
  uint16_t batch_data_num = data_pkts.size();
  /* since fec_rate is calculated by fec_count when tx_count == 0, use round to avoid float error. */
  uint16_t max_fec_num = UINT16_MAX;
  // *m_debugStream->GetStream () << "[Server] At " << Simulator::Now ().GetMilliSeconds () << 
  //   " quota is " << m_ccaQuotaPkt << " beforeFec " << round(batch_data_num * fec_rate) << std::endl;
  if (m_ccEnabled) {
    max_fec_num = MAX (1, m_ccaQuotaPkt);
  }
  uint16_t batch_fec_num = MIN ((uint16_t) round(batch_data_num * fec_rate), max_fec_num);
  m_ccaQuotaPkt -= batch_fec_num;

  uint16_t group_data_num, group_fec_num;
  uint8_t tx_count = data_pkts.front()->GetTXCount();

  uint32_t batch_id = GetNextBatchId();
  uint16_t pkt_id_in_batch = 0, pkt_id_in_group = 0;
  uint32_t frameId = data_pkts.front ()->GetFrameId ();
  if (new_group) {
    group_id = GetNextGroupId();
    pkt_id_in_group = 0;
    group_data_num = batch_data_num;
    group_fec_num = batch_fec_num;
    if (std::find (m_frameIdToGroupId[frameId].begin (), m_frameIdToGroupId[frameId].end (), group_id) == m_frameIdToGroupId[frameId].end ()) 
      m_frameIdToGroupId[frameId].push_back (group_id);
  } else {
    group_data_num = data_pkts.front()->GetGroupDataNum();
    group_fec_num = data_pkts.front()->GetBatchFECNum();
  }
  // DEBUG("[Group INFO] gid " << group_id << ", group data num: " << group_data_num << ", group fec num: " << group_fec_num << ", batch id: " << batch_id << ", batch data num: " << batch_data_num << ", batch fec num: " << batch_fec_num);

  // to save some time
  pkt_batch.reserve(batch_data_num + batch_fec_num);

  // Assign batch & group ids for data packets
  for(auto data_pkt : data_pkts) {
    NS_ASSERT(data_pkt->GetPacketType() == PacketType::DATA_PKT);

    // Set batch info
    data_pkt->SetFECBatch(batch_id, batch_data_num, batch_fec_num, pkt_id_in_batch ++);

    // Set group info
    if (!new_group && data_pkt->GetGroupInfoSetFlag()) {
      // packets whose group info have already been set
      NS_ASSERT(data_pkt->GetGroupId() == group_id);
      continue;
    }
    // other packets
    data_pkt->SetFECGroup(group_id, group_data_num, group_fec_num, pkt_id_in_group ++);
  }

  // Generate FEC packets
  // create FEC packets and push into packet_group{
  for(uint16_t i = 0;i < batch_fec_num;i++) {
    Ptr<FECPacket> fec_pkt = Create<FECPacket> (tx_count, data_pkts);
    fec_pkt->SetFECBatch(batch_id, batch_data_num, batch_fec_num, pkt_id_in_batch ++);
    if(!is_rtx)
      fec_pkt->SetFECGroup(
        group_id, group_data_num, group_fec_num, pkt_id_in_group ++
      );
    else
      fec_pkt->SetFECGroup(
        group_id, group_data_num, group_fec_num, VideoPacket::RTX_FEC_GROUP_ID
      );
    fec_pkt->SetEncodeTime(Simulator::Now());
    pkt_batch.push_back(fec_pkt);
  }
  // DEBUG(pkt_batch.size() << ", "<< fec_param.fec_group_size << ", "<< batch_id << pkt_id_in_batch << group_id << pkt_id_in_group);
  return ;
};


void PacketUdpSender::CreateFirstPacketBatch(
  std::vector<Ptr<VideoPacket>>& pkt_batch, FECPolicy::FECParam fec_param) {
  NS_LOG_FUNCTION(pkt_batch.size());

  NS_ASSERT(pkt_batch.size() > 0);

  CreatePacketBatch(
    pkt_batch, fec_param, true /* new_group */, 0, false /* not rtx */
  );

  send_group_cnt ++;
};

void PacketUdpSender::CreateRTXPacketBatch(
  std::vector<Ptr<VideoPacket>>& pkt_batch, FECPolicy::FECParam fec_param) {
  NS_LOG_FUNCTION(pkt_batch.size());

  NS_ASSERT(pkt_batch.size() > 0);
  NS_ASSERT(pkt_batch.size() == fec_param.fec_group_size);

  uint32_t group_id = pkt_batch.front()->GetGroupId();

  CreatePacketBatch(
    pkt_batch, fec_param, false /* new_group */, group_id, true /* is rtx */
  );
};

FECPolicy::FECParam PacketUdpSender::GetFECParam (
    uint16_t maxGroupSize, uint32_t bitrate,
    Time ddlLeft, bool fixGroupSize,
    bool isRtx, uint8_t frameSize
  ) {
  m_netStat->curLossRate = m_lossEstimator->GetLoss (Simulator::Now ());
  auto fecParam = m_fecPolicy->GetFECParam (m_netStat, bitrate, 
    m_delayDdl.GetMilliSeconds (), (uint16_t) floor (ddlLeft.GetMicroSeconds () / 1e3), 
    isRtx, frameSize, maxGroupSize, fixGroupSize);
  m_curFecParam = fecParam;
  return fecParam;
};


void PacketUdpSender::RetransmitGroup (uint32_t groupId) {
  // cannot find the packets to send
  NS_ASSERT (m_dataPktHistory.find (groupId) != m_dataPktHistory.end ());

  int txCnt = -1;
  Time encodeTime = Time (0);
  Time now = Simulator::Now ();

  std::deque<Ptr<DataPacket>> dataPktRtxQueue;

  auto groupDataPkt = m_dataPktHistory[groupId];
  // find all packets that belong to the same group and retransmit them
  for (const auto& [pktId, dataPkt] : groupDataPkt) {
    if (txCnt == -1) 
      txCnt = dataPkt->GetTXCount () + 1;
    if (encodeTime == Time (0)) 
      encodeTime = dataPkt->GetEncodeTime ();

    dataPkt->SetTXCount (txCnt);
    dataPkt->SetEnqueueTime (now);
    dataPkt->ClearFECBatch ();
    dataPktRtxQueue.push_back (dataPkt);
  }
  for (auto it = m_dataPktHistoryKey.begin (); it != m_dataPktHistoryKey.end ();) {
    Ptr<GroupPacketInfo> info = *it;
    if (info->m_groupId == groupId)
      it = m_dataPktHistoryKey.erase (it);
    else
      it ++;
  }
  groupDataPkt.clear ();

  if (!dataPktRtxQueue.empty ()) {
    m_lossEstimator->RtxUpdate (dataPktRtxQueue.size (), Simulator::Now ());
    // all packets in the same group belong to the same frame
    uint32_t frameId = dataPktRtxQueue.front ()->GetFrameId ();
    SendPackets (dataPktRtxQueue, m_delayDdl - (now - encodeTime), frameId, true);
  }
}

void PacketUdpSender::CheckRetransmission () {
  Time now = Simulator::Now ();
  this->check_rtx_event = Simulator::Schedule (this->check_rtx_interval, 
    &PacketUdpSender::CheckRetransmission, this);

  bool isFront = true;
  /* 1) check for packets that will definitely miss ddl */
  for (auto it = m_dataPktHistoryKey.begin (); it != m_dataPktHistoryKey.end ();) {
    Ptr<GroupPacketInfo> info = (*it);
    if (info->m_state != GroupPacketInfo::PacketState::RCVD_PREV_DATA) {
      // if we cannot find it in m_dataPktHistory, and it's not a fake hole (data rcvd)
      if (m_dataPktHistory.find (info->m_groupId) == m_dataPktHistory.end ()) {
        it = m_dataPktHistoryKey.erase (it);
        continue;
      }
      auto groupDataPkt = &m_dataPktHistory[info->m_groupId];
      if (groupDataPkt->find (info->m_pktIdInGroup) == groupDataPkt->end ()) {
        it = m_dataPktHistoryKey.erase (it);
        continue;
      }
    }

    // we can find it in m_dataPktHistory, check if it's timed out
    if (isFront && (info->m_state == GroupPacketInfo::PacketState::RCVD_PREV_DATA || 
        MissesDdl (m_dataPktHistory[info->m_groupId][info->m_pktIdInGroup]))) {
      /* only remove pkts from begin ()! otherwise will create holes 
         remove it in m_dataPktHistory */
      m_dataPktHistory[info->m_groupId].erase (info->m_pktIdInGroup);
      if (m_dataPktHistory[info->m_groupId].empty ())
        m_dataPktHistory.erase (info->m_groupId);
      it = m_dataPktHistoryKey.erase (it);
    } else {
      // it's not a FIFO queue -- rtx packets are put to the end
      // we need to check if packets behind the first non-timeout packet will timeout
      isFront = false;
      it ++;
    }
  }

  /* 2) check for packets that exceeds rtx timer, needs to be retransmitted */
  std::unordered_set<uint32_t> rtxGroupId;

  /* Check if there are delayed rtx that is exactly the time to retransmit */
  for (auto it = m_delayedRtxGroup.begin (); it != m_delayedRtxGroup.end ();) {
    uint32_t groupId = (*it).first;
    Time rtxTime = (*it).second;
    if (rtxTime < now) {
      /* if there are still packets in that group, retransmit them,
         otherwise, just erase the group id since packets must have been received. */
      if (m_dataPktHistory.find (groupId) != m_dataPktHistory.end ())
        rtxGroupId.insert ((*it).first);
      it = m_delayedRtxGroup.erase (it);
    }
    else
      it ++;
  }

  uint16_t lastDataGlobalId = m_curRxHighestDataGlobalId;  /* for dup-ack check */
  if (m_dataPktHistoryKey.empty ()) {
    return;
  }

  bool isLoop = true;
  bool hasHole = false;    /* whether we have found the first rtx packet or not */
  for (auto it = m_dataPktHistoryKey.end () - 1; isLoop && !m_dataPktHistoryKey.empty (); ) {
    bool shouldRtx = false;
    Ptr<DataPacket> pkt;
    Ptr<GroupPacketInfo> info = (*it);
    if (it == m_dataPktHistoryKey.begin ()) {
      isLoop = false;
    }
    /* this packet has actually been received, thus no longer exists in m_dataPktHistory 
       this must be checked before the intialization of pkt = m_dataPktHistory, 
       otherwise there will be nullptr inside. */
    if (info->m_state == GroupPacketInfo::PacketState::RCVD_PREV_DATA) {
      if (m_netStat->srtt > 2 * m_lastRtt && m_fecPolicy->GetFecName () == "HairpinPolicy") {
        m_delayedRtxGroup[info->m_groupId] = now + m_lastRtt;
      }
      goto continueLoop;
    }
    
    pkt = m_dataPktHistory[info->m_groupId][info->m_pktIdInGroup];

    /* this packet is too early to retransmit */
    if (now - pkt->GetEncodeTime () < m_netStat->minRtt)
      goto continueLoop;

    /* this group has just been retransmitted */
    if (rtxGroupId.find (pkt->GetGroupId ()) != rtxGroupId.end ())
      goto continueLoop;
    NS_ASSERT (pkt->GetDataGlobalId () == info->m_dataGlobalId);

    /* If the FEC policy is RTX, we use the dup-ack retransmission policy
      we do not count on the PTO policy. The rtx for fec packets is buggy, do not use it */
    if (!hasHole) {
      // *m_debugStream->GetStream () << "[HoleCheck] m_dataGlobalId " << info->m_dataGlobalId << 
      //   " lastDataGlobalId " << lastDataGlobalId <<
      //   " m_isRecovery " << m_isRecovery << " globalId " << info->m_globalId << 
      //   " m_curContRxHighestGlobalId " << m_curContRxHighestGlobalId << std::endl;
      if (Uint16Less (int (info->m_dataGlobalId) + 1, int (lastDataGlobalId))) {
        /* holes in data packets */
        hasHole = true;
      } else if (m_isRecovery && Uint16Less (info->m_globalId, m_curContRxHighestGlobalId)) {
        /* dataGlobalId is continuous, but fec packets might be lost */
        hasHole = true;
      }
    }

    if (hasHole) {
      /* If we detect a packet loss, be patient till dispersion */
      m_isRecovery = false; /* already found a hole, continue to find the next hole */
      if (m_delayedRtxGroup.find (pkt->GetGroupId ()) == m_delayedRtxGroup.end ())
        m_delayedRtxGroup[pkt->GetGroupId ()] = now + GetDispersion (pkt);
      else
        m_delayedRtxGroup[pkt->GetGroupId ()] = Min (m_delayedRtxGroup[pkt->GetGroupId ()], now + GetDispersion (pkt));
    }
    
    if (m_rtxPolicy == "pto") {
      /* For PTO, we can find it in m_dataPktHistory, check if it's timed out
         if this packet has not arrived at client
         do not retransmit it and all the packets behind 
         Specifically, PTO first goes through dupack, and then pto. */
      shouldRtx = IsRtxTimeout (pkt, Time (0));
    } else {
      shouldRtx = IsRtxTimeout (pkt, Seconds (1));   /* TCP RTO mechanism */
    }

    if (shouldRtx) {
      /* group all packets that are of the same group into a batch 
         and send out, but we cannot retransmit the whole group here 
         since we have no idea whether other packets in the same group
         need retransmitting or not (the iteration order) */
      uint32_t groupId = pkt->GetGroupId ();
      rtxGroupId.insert (groupId);
    }

continueLoop:
    it --;
    lastDataGlobalId = info->m_dataGlobalId;
  }

  /* Retransmit all the lost packets found in this round 
     We can only retransmit in the end because we're now iterating 
     from the newest to the oldest */
  for (uint32_t groupId : rtxGroupId) {
    if (m_delayedRtxGroup.find (groupId) != m_delayedRtxGroup.end ()) {
      /* if this group is also waiting to be retransmitted, erase it
         since we are now retransmitting that group. */
      m_delayedRtxGroup.erase (groupId);
    }
    RetransmitGroup (groupId);
  }
  
  m_lastRtt = m_netStat->srtt;
};

/* Remove packet history records when we receive an ACK packet */
void PacketUdpSender::RcvACKPacket (Ptr<AckPacket> ackPkt) {
  std::vector<Ptr<GroupPacketInfo>> pktInfos = ackPkt->GetAckedPktInfos ();

  for (Ptr<GroupPacketInfo> pktInfo : pktInfos) {
   if (!m_isRecovery) {
      m_curContRxHighestGlobalId = pktInfo->m_globalId;
      if (Uint16Less (int (m_curRxHighestGlobalId) + 1, pktInfo->m_globalId)) {
        m_isRecovery = true;
      }
    }
    m_curRxHighestGlobalId = pktInfo->m_globalId;
    // find the packet in m_dataPktHistory
    if (m_dataPktHistory.find (pktInfo->m_groupId) != m_dataPktHistory.end ()) {
      auto groupDataPkt = &m_dataPktHistory[pktInfo->m_groupId];
      if (groupDataPkt->find (pktInfo->m_pktIdInGroup) != groupDataPkt->end ()) {
        // erase pkt_id_in_frame
        groupDataPkt->erase (pktInfo->m_pktIdInGroup);
      }
      // erase frame_id if neccesary
      if (m_dataPktHistory[pktInfo->m_groupId].empty ()) {
        m_dataPktHistory.erase (pktInfo->m_groupId);
      }
    }
    for (auto it = m_dataPktHistoryKey.begin (); it != m_dataPktHistoryKey.end ();) {
      Ptr<GroupPacketInfo> senderInfo = (*it);
      if (senderInfo->m_groupId == pktInfo->m_groupId && senderInfo->m_pktIdInGroup == pktInfo->m_pktIdInGroup) {
        if (senderInfo->m_globalId == pktInfo->m_globalId) {
          it = m_dataPktHistoryKey.erase (it);
          if (Uint16Less (m_curRxHighestDataGlobalId, senderInfo->m_dataGlobalId))
            m_curRxHighestDataGlobalId = senderInfo->m_dataGlobalId;
        } else
          senderInfo->m_state = GroupPacketInfo::PacketState::RCVD_PREV_DATA;
        break;
      } else {
        it ++;
      }
    }
  }
};

void PacketUdpSender::RcvFrameAckPacket (Ptr<FrameAckPacket> frameAckPkt) {
  uint32_t frameId = frameAckPkt->GetFrameId ();
  for (auto groupId : m_frameIdToGroupId[frameId]) {
    if (m_dataPktHistory.find (groupId) != m_dataPktHistory.end ()) {
      m_dataPktHistory.erase (groupId);
    }
    for (auto info : m_dataPktHistoryKey) {
      if (info->m_groupId == groupId) {
        /* we are not sure if this info is in the front of the queue: 
           blindly remove the info could lead to the incorrect judgement 
           of previous data packet */
        info->m_state = GroupPacketInfo::PacketState::RCVD_PREV_DATA;
      }
    }
  }
  m_frameIdToGroupId.erase (frameId);
};

void PacketUdpSender::SendFrame (std::vector<Ptr<VideoPacket>> packets)
{
    UpdateGoodputRatio();
    Ptr<PacketFrame> newFrame = Create<PacketFrame>(packets,false);
    newFrame->Frame_encode_time_ = packets[0]->GetEncodeTime();
    m_queue.push_back(newFrame);
    Calculate_pacing_rate();
    if (m_pacing) {
        if(m_pacingTimer.IsExpired()){
            m_pacingTimer.Schedule();
        }
        else {
            m_pacingTimer.Cancel();
            m_pacingTimer.Schedule();
        }
    }
    else {
        SockSendPacket ();
    }
};

void PacketUdpSender::SendRtx (std::vector<Ptr<VideoPacket>> packets)
{
    NS_LOG_FUNCTION("At time " << Simulator::Now().GetMilliSeconds() << ", " << packets.size() << " RTX packets are enqueued");
    Ptr<PacketFrame> newFrame = Create<PacketFrame>(packets,true);
    newFrame->Frame_encode_time_ = packets[0]->GetEncodeTime();
    m_queue.insert(m_queue.begin(),newFrame);
    this->Calculate_pacing_rate();
    if(m_pacing){
        if(m_pacingTimer.IsExpired()){
           m_pacingTimer.Schedule();
        }
    }
    else {
        SockSendPacket ();
    }
};

void PacketUdpSender::Calculate_pacing_rate()
{
    Time time_now = Simulator::Now();
    uint32_t num_packets_left = 0;
    for(uint32_t i=0;i<this->num_frame_in_queue();i++) {
        if(m_queue[i]->retransmission) {
            continue;
        }
        num_packets_left = num_packets_left + m_queue[i]->Frame_size_in_packet();
        if(num_packets_left < 1) {
            NS_LOG_ERROR("Number of packet should not be zero.");
        }
        Time time_before_ddl_left = m_send_time_limit + m_queue[i]->Frame_encode_time_ - time_now;
        Time interval_max = time_before_ddl_left / num_packets_left;
        if(time_before_ddl_left <= Time(0)) {
            // TODO: DDL miss appears certain, need to require new IDR Frame from GameServer?
            NS_LOG_ERROR("DDL miss appears certain.");
        }
        else {
            if(interval_max < m_pacing_interval) {
                m_pacing_interval = interval_max;
                m_pacingTimer.SetDelay(interval_max);
            }
        }
    }
};

void PacketUdpSender::OnSocketRecv_sender(Ptr<Socket> socket)
{
    Ptr<Packet> pkt = socket->Recv();
    Ptr<NetworkPacket> packet = NetworkPacket::ToInstance(pkt);
    auto pkt_type = packet->GetPacketType();
    Time now = Simulator::Now();

    if (pkt_type == ACK_PKT) {
        Ptr<AckPacket> ack_pkt = DynamicCast<AckPacket, NetworkPacket> (packet);

        // Update RTT and inter-packet delay
        uint16_t pkt_id = ack_pkt->GetLastPktId();
        NS_LOG_FUNCTION ("[Sender] At " << Simulator::Now().GetMilliSeconds() << " ms rcvd ACK for packet " << pkt_id);
        for (auto it = m_dataPktHistoryKey.begin (); it != m_dataPktHistoryKey.end (); it++) {
            Ptr<GroupPacketInfo> curPkt = (*it);
            if (curPkt->m_globalId == pkt_id) {
                // RTT
                Time rtt = now - curPkt->m_sendTime;
                if (!this->trace_set) 
                    UpdateRTT(rtt);
            
                // inter-packet delay
                if (it != m_dataPktHistoryKey.begin () && (*(it-1))->m_globalId == pkt_id - 1) {
                    Ptr<GroupPacketInfo> lastPkt = *(it-1);
                    if (lastPkt->m_sendTime == curPkt->m_sendTime && m_lastAckTime != Time (0)) {
                        Time inter_pkt_delay = now - m_lastAckTime;
                        if (m_netStat->rt_dispersion == Time (0))
                            m_netStat->rt_dispersion = inter_pkt_delay;
                        else {
                            m_netStat->rt_dispersion += 0.2 * (inter_pkt_delay - m_netStat->rt_dispersion);
                        }
                    }
                }
                m_lastAckTime = now;
                break;
            }
        }
        
        RcvACKPacket (ack_pkt);
        return;
    }

    if (pkt_type == FRAME_ACK_PKT) {
        NS_LOG_FUNCTION ("Frame ACK packet received!");
        Ptr<FrameAckPacket> frame_ack_pkt = DynamicCast<FrameAckPacket, NetworkPacket> (packet);
        RcvFrameAckPacket (frame_ack_pkt);
        uint32_t frame_id = frame_ack_pkt->GetFrameId();
        Time frame_encode_time = frame_ack_pkt->GetFrameEncodeTime();
        if(now - frame_encode_time <= m_delayDdl + MilliSeconds(1)) {
            m_finished_frame_cnt ++;
            NS_LOG_FUNCTION ("[Sender] At " << Simulator::Now().GetMilliSeconds() << " Frame ID: " << frame_id << " rcvs within ddls");
        } else {
            NS_LOG_FUNCTION ("[Sender Timeout Frame] At " << Simulator::Now().GetMilliSeconds() << " Frame ID: " << frame_id << " misses ddl, delay: " << (now - frame_encode_time).GetMilliSeconds() << " ms\n");
            m_timeout_frame_cnt ++;
        }
        return;
    }

    // netstate packet
    NS_ASSERT_MSG(pkt_type == NETSTATE_PKT, "Sender should receive FRAME_ACK_PKT, ACK_PKT or NETSTATE_PKT");
    Ptr<NetStatePacket> netstate_pkt = DynamicCast<NetStatePacket, NetworkPacket> (packet);
    auto states = netstate_pkt->GetNetStates();
    if(!this->trace_set){
        m_netStat->curBw = ((double_t)states->throughput_kbps) / 1000.;
        m_netStat->curLossRate = states->loss_rate;
        if(m_controller)
            m_controller->UpdateLossRate(uint8_t (states->loss_rate * 256));
    }
    m_netStat->loss_seq = states->loss_seq;
    m_netStat->oneWayDispersion = MicroSeconds(states->fec_group_delay_us);

    NS_LOG_FUNCTION ("[Sender] At " << Simulator::Now().GetMilliSeconds() << " ms  bw = " << m_netStat->curBw << " Loss = " << m_netStat->curLossRate);

    if (m_ccEnabled) {
        uint64_t now_us = Simulator::Now().GetMicroSeconds();

        for (auto recvtime_item : states->recvtime_hist){
            uint16_t id = recvtime_item->pkt_id;
            uint64_t RxTime = recvtime_item->rt_us;
            uint64_t TxTime = m_controller->GetPacketTxTimestamp(id);
            // NS_ASSERT_MSG((RxTime <= now_us), "Receiving event and feedback event should be time-ordered.");
            if (m_firstFeedback) {
                m_prev_id = id;
                m_prev_RxTime = RxTime;
                m_ackGroupId = 0;
                m_group_size = m_controller->GetPacketSize(id);
                m_groupstart_TxTime = TxTime;
                m_firstFeedback = false;
                m_curr_pkts_in_frame = 1;
                continue;
            }
            
            if (Uint64Less (m_groupstart_TxTime, TxTime) || m_groupstart_TxTime == TxTime) {
                if ((TxTime - m_groupstart_TxTime) > 10000) {
                    if (TxTime - m_groupstart_TxTime > 20 * m_interval * 1000) {
                        NS_LOG_ERROR ("Huge gap between frame? group id: " << m_ackGroupId
                          << ", group start tx: " << m_groupstart_TxTime
                          << ", tx: " << TxTime);
                    }
                    // Switching to another burst (named as group)
                    // update inter arrival and inter departure
                    if (m_ackGroupId > 0) {
                        NS_ASSERT_MSG (m_prev_pkts_in_frame > 0, "Consecutive frame must have pkts!");
                        inter_arrival = m_curr_frame_RxTime / m_curr_pkts_in_frame - m_prev_frame_RxTime / m_prev_pkts_in_frame;
                        inter_departure = m_curr_frame_TxTime / m_curr_pkts_in_frame - m_prev_frame_TxTime / m_prev_pkts_in_frame;
                        inter_delay_var = inter_arrival - inter_departure;
                        inter_group_size = m_group_size - m_prev_group_size;
                        m_controller->processFeedback(now_us, id, RxTime, inter_arrival, inter_departure, inter_delay_var, inter_group_size, m_prev_RxTime);
                    }

                    // update group information
                    m_controller->PrunTransitHistory(m_prev_groupend_id);
                    m_prev_group_size = m_group_size;
                    m_prev_groupend_id = m_prev_id;
                    m_prev_groupend_RxTime = m_prev_RxTime;
                    m_ackGroupId += 1;
                    m_group_size = 0;
                    m_groupstart_TxTime = TxTime;
                    m_prev_frame_TxTime = m_curr_frame_TxTime;
                    m_prev_frame_RxTime = m_curr_frame_RxTime;
                    m_prev_pkts_in_frame = m_curr_pkts_in_frame;
                    m_curr_frame_TxTime = 0;
                    m_curr_frame_RxTime = 0;
                    m_curr_pkts_in_frame = 0;
                }

                m_curr_pkts_in_frame += 1;
                m_curr_frame_TxTime += TxTime;
                m_curr_frame_RxTime += RxTime;       

                m_group_size += m_controller->GetPacketSize(id);
                m_prev_id = id;
                m_prev_RxTime = RxTime;
            }
            else {
                NS_LOG_ERROR ("Out-of-order frame? group id: " << m_ackGroupId
                  << ", group start tx: " << m_groupstart_TxTime
                  << ", tx: " << TxTime);
            }
        }
    }
};

Ptr<FECPolicy::NetStat> PacketUdpSender::GetNetworkStatistics() { return m_netStat; };

double_t PacketUdpSender::GetBandwidthLossRate() {
    double_t bandwidth_loss_rate_count, bandwidth_loss_rate_size;
    if(this->init_data_pkt_count + this->other_pkt_count == 0)
        return 0;
    bandwidth_loss_rate_count =
        ((double_t) this->other_pkt_count) /
        (this->init_data_pkt_count);
    bandwidth_loss_rate_size =
        ((double_t) this->other_pkt_size) /
        (this->init_data_pkt_size);
    NS_LOG_ERROR("[Sender] Initial data packets: " << this->init_data_pkt_count << ", other packets:" << this->other_pkt_count);
    NS_LOG_ERROR("[Sender] Bandwidth loss rate: " << bandwidth_loss_rate_count * 100 << "% (count), " << bandwidth_loss_rate_size * 100 << "% (size)");

    if(this->exclude_head_init_data_pkt_count > 0) {
        double_t exclude_head_bandwidth_loss_rate_count, exclude_head_bandwidth_loss_rate_size;
        exclude_head_bandwidth_loss_rate_count =
            ((double_t) this->exclude_head_other_pkt_count) /
            (this->exclude_head_init_data_pkt_count);
        exclude_head_bandwidth_loss_rate_size =
            ((double_t) this->exclude_head_other_pkt_size) /
            (this->exclude_head_init_data_pkt_size);
        NS_LOG_ERROR("[Sender] [Result] Initial data packets: " << this->exclude_head_init_data_pkt_count << ", other packets:" << this->exclude_head_other_pkt_count);
        NS_LOG_ERROR("[Sender] [Result] Bandwidth loss rate: " << exclude_head_bandwidth_loss_rate_count * 100 << "% (count), " << exclude_head_bandwidth_loss_rate_size * 100 << "% (size)");
    }
    NS_LOG_ERROR("[Sender] Played frames: " << m_finished_frame_cnt << ", timeout frames: " << m_timeout_frame_cnt);
    NS_LOG_ERROR("[Sender] [Result] Played frames: " << m_exclude_ten_finished_frame_cnt);

    return bandwidth_loss_rate_count;
};

void PacketUdpSender::UpdateSendingRate(){
    if (m_ccEnabled) {
        m_bitrate = 0.85 * m_controller->getSendBps();
        *m_bwTrStream->GetStream () << Simulator::Now ().GetMilliSeconds () << 
            " FlowId " << m_srcPort << 
            " Bitrate(bps) " << m_bitrate << 
            " Rtt(ms) " << (uint32_t) GetNetworkStatistics()->srtt.GetMilliSeconds() << std::endl;
    }
};

double PacketUdpSender::GetGoodputRatio() {
    return m_goodput_ratio;
};

void PacketUdpSender::UpdateGoodputRatio() {
    m_goodput_ratio = 1.; // only for confucius, not compatible with hairpin
};

void PacketUdpSender::UpdateNetstateByTrace()
{
    // Load trace file
    std::ifstream trace_file;
    trace_file.open(this->trace_filename);

    // Initialize storing variables
    std::string trace_line;
    std::vector<std::string> trace_data;
    std::vector<std::string> bw_value;
    std::vector<std::string> rtt_value;
    uint16_t rtt;
    double_t bw;
    double_t lr;

    uint64_t past_time = 0;

    // Set netstate for every tracefile line
    while (std::getline(trace_file,trace_line))
    {   
        rtt_value.clear();
        bw_value.clear();
        trace_data.clear();
        SplitString(trace_line, trace_data," ");
        SplitString(trace_data[0], bw_value, "Mbps");
        SplitString(trace_data[1], rtt_value, "ms");
        rtt = (uint16_t) std::atof(rtt_value[0].c_str());
        bw = std::atof(bw_value[0].c_str());
        lr = std::atof(trace_data[2].c_str());
        m_settrace_event = Simulator::Schedule(
            MilliSeconds(rtt + past_time),&PacketUdpSender::SetNetworkStatisticsBytrace, 
            this, rtt, bw, lr);
        past_time += m_interval;
    }
    
};

void PacketUdpSender::SetTrace(std::string tracefile)
{
    this->trace_set = true;
    this->trace_filename = tracefile;
};

void PacketUdpSender::SetTimeLimit (const Time& limit)
{
    m_send_time_limit = limit;
};

void PacketUdpSender::Pause()
{
    if (m_pacing) {
        m_pacingTimer.Cancel();
    }
    else {
        Simulator::Cancel(m_eventSend);
    }
};

void PacketUdpSender::Resume()
{
    if (m_pacing) {
        m_pacingTimer.Schedule();
    }
    else {
        m_eventSend = Simulator::ScheduleNow(&PacketUdpSender::SockSendPacket,this);
    }
};

void PacketUdpSender::SockSendPacket ()
{
    Time time_now = Simulator::Now();
    uint64_t NowUs = time_now.GetMicroSeconds();
    if(this->num_frame_in_queue() > 0){
        std::vector<Ptr<VideoPacket>>* current_frame = &m_queue[0]->packets_in_Frame;
        std::vector<Ptr<VideoPacket>>::iterator firstPkt = current_frame->begin();
        Ptr<VideoPacket> netPktToSend = *firstPkt;
        PacketType pktType = netPktToSend->GetPacketType ();
        netPktToSend->SetSendTime (time_now);
        netPktToSend->SetGlobalId (m_netGlobalId);
        if (pktType == PacketType::DATA_PKT) {
            Ptr<DataPacket> dataPkt = DynamicCast<DataPacket, VideoPacket> (netPktToSend);
            dataPkt->SetDataGlobalId (m_dataGlobalId);
            m_dataGlobalId = (m_dataGlobalId + 1) % 65536;
        }
        bool is_goodput = (pktType == PacketType::DATA_PKT)
                            && (netPktToSend->GetTXCount() == 0);

        Ptr<Packet> pktToSend = netPktToSend->ToNetPacket ();
        uint16_t pkt_size = pktToSend->GetSize();
        if (m_ccEnabled) {
            // handle pkt information to cc controller
            m_controller->processSendPacket (NowUs, m_netGlobalId, pktToSend->GetSize());
        }

        // statistics
        if (is_goodput) {
            this->init_data_pkt_count ++;
            this->init_data_pkt_size += pkt_size;
            this->goodput_pkts_inwnd += pkt_size;
        } else {
            this->other_pkt_count ++;
            this->other_pkt_size += pkt_size;
        }
        this->total_pkts_inwnd += pkt_size;

        DEBUG("[Sender] At " << Simulator::Now().GetMilliSeconds() << " Send packet " << netPktToSend->GetGlobalId() << ", Group id: " << netPktToSend->GetGroupId());
        m_socket->Send(pktToSend);

        current_frame->erase (firstPkt);

        m_netGlobalId = (m_netGlobalId + 1) % 65536;

        if(current_frame->empty()){
            m_queue.erase(m_queue.begin());
            this->Calculate_pacing_rate();
        }
        if(m_pacing){
            m_pacingTimer.Schedule();
        }
        else {
            SockSendPacket ();
        }

    }
};

uint32_t PacketUdpSender::num_frame_in_queue()
{
    return m_queue.size();
};

}; // namespace ns3