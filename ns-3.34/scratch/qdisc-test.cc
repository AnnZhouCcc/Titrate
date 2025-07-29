/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 ResiliNets, ITTC, University of Kansas
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Truc Anh N Nguyen <trucanh524@gmail.com>
 * Modified by:   Pasquale Imputato <p.imputato@gmail.com>
 *
 */

/*
 * This is a basic example that compares CoDel and PfifoFast queues using a simple, single-flow topology:
 *
 * source -------------------------- router ------------------------ sink
 *          100 Mb/s, 0.1 ms       pfifofast       10 Mb/s, 50ms
 *                                 or codel        bottleneck
 *
 * The source generates traffic across the network using BulkSendApplication.
 * The default TCP version in ns-3, TcpNewReno, is used as the transport-layer protocol.
 * Packets transmitted during a simulation run are captured into a .pcap file, and
 * congestion window values are also traced.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
// #include <boost/filesystem.hpp>
// #include <boost/algorithm/string/classification.hpp> // Include boost::for is_any_of
// #include <boost/algorithm/string/split.hpp> // Include for boost::split

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include "ns3/ipv4-header.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/bitrate-ctrl-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("QdiscTest");

/* experiment settings */
std::vector <std::pair<std::string, uint32_t> > appSettings;
double_t dwrrPrioRatio = 2.;
std::string codelTarget = "5ms";
std::string tbfRate = "10Mbps";
uint32_t tbfBurst = 50000;

uint32_t diffServ = 0;

std::string ccaMap[16] = {"Gcc", "Nada", "", "Fixed", 
  "Cubic", "Bbr", "Copa", "Yeah", 
  "Illinois", "Vegas", "Htcp", "Bic", 
  "LinuxReno", "Scalable", "", ""};

std::string autoDecayingFunc = "Linear";
double_t autoDecayingCoef = 1.;

double statIntervalSec = 0.001;
std::vector<uint32_t> flowHash;

uint32_t Ipv4Hash (Ipv4Address src, Ipv4Address dest, uint8_t prot, uint16_t srcPort, uint16_t destPort, uint32_t perturbation = 0) {
  /* serialize the 5-tuple and the perturbation in buf */
  uint8_t buf[17];
  src.Serialize (buf);
  dest.Serialize (buf + 4);
  buf[8] = prot;
  buf[9] = (srcPort >> 8) & 0xff;
  buf[10] = srcPort & 0xff;
  buf[11] = (destPort >> 8) & 0xff;
  buf[12] = destPort & 0xff;
  buf[13] = (perturbation >> 24) & 0xff;
  buf[14] = (perturbation >> 16) & 0xff;
  buf[15] = (perturbation >> 8) & 0xff;
  buf[16] = perturbation & 0xff;

  // Linux calculates jhash2 (jenkins hash), we calculate murmur3 because it is
  // already available in ns-3
  uint32_t hash = Hash32 ((char*) buf, 17);
  return hash;
}

static void DbeTracer (Ptr<OutputStreamWrapper> stream, Ptr<const QueueDiscItem> item, const char* reason) {
  *stream->GetStream () << Simulator::Now ().GetSeconds () << " [DropBeforeEnqueue] " << reason << " ";
  item->Print (*stream->GetStream ());
  *stream->GetStream () << std::endl;
}

static void DadTracer (Ptr<OutputStreamWrapper> stream, Ptr<const QueueDiscItem> item, const char* reason) {
  *stream->GetStream () << Simulator::Now ().GetSeconds () << " [DropAfterDequeue] " << reason << " ";
  item->Print (*stream->GetStream ());
  *stream->GetStream () << std::endl;
}

void TraceQdiscDrop (Ptr<QueueDisc> qdisc, Ptr<OutputStreamWrapper> stream) {
  qdisc->TraceConnectWithoutContext ("DropBeforeEnqueue", MakeBoundCallback (&DbeTracer, stream));
  qdisc->TraceConnectWithoutContext ("DropAfterDequeue", MakeBoundCallback (&DadTracer, stream));
}

QueueDiscContainer InstallQdisc (std::string qdisc, 
                                 uint32_t queueDiscSize,
                                 NetDeviceContainer devicesBottleneckLink) {
  TrafficControlHelper tchBottleneck;
  if (qdisc == "PfifoFast" || qdisc == "StrictPriority" || 
      qdisc == "Hhf" || qdisc == "Choke" || qdisc == "Sfb" || qdisc == "Fifo") {
    tchBottleneck.SetRootQueueDisc ("ns3::" + qdisc + "QueueDisc", 
                                    "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"));
  } else if (qdisc == "FqCoDel") {
    tchBottleneck.SetRootQueueDisc ("ns3::FqCoDelQueueDisc",
                                    "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"), 
                                    "Target", StringValue (codelTarget));
  } else if (qdisc == "CoDel") {
    tchBottleneck.SetRootQueueDisc ("ns3::CoDelQueueDisc", 
                                    "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
                                    "Target", StringValue (codelTarget));
  } else if (qdisc == "Red") {
    tchBottleneck.SetRootQueueDisc ("ns3::RedQueueDisc", 
                                    "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
                                    "MinTh", DoubleValue (queueDiscSize / 12.),
                                    "MaxTh", DoubleValue (queueDiscSize / 4.));
  } else if (qdisc == "Dwrr") {
    tchBottleneck.SetRootQueueDisc ("ns3::DwrrQueueDisc",
                                    "NumClass", UintegerValue (2),
                                    "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
                                    "PrioRatio", DoubleValue (dwrrPrioRatio));
  } else if (qdisc == "Auto") {
    Config::SetDefault ("ns3::AutoFlow::DecayingFunction", StringValue (autoDecayingFunc));
    Config::SetDefault ("ns3::AutoFlow::DecayingCoef", DoubleValue (autoDecayingCoef));
    tchBottleneck.SetRootQueueDisc ("ns3::AutoQueueDisc",
                                    "NumClass", UintegerValue (3),
                                    "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
                                    "PrioRatio", DoubleValue (dwrrPrioRatio));
  } else if (qdisc == "Tbf") {
    tchBottleneck.SetRootQueueDisc ("ns3::TbfQueueDisc",
                                    "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
                                    "Rate", DataRateValue (DataRate (tbfRate)),
                                    "Burst", UintegerValue (tbfBurst));

  } else if (qdisc == "DualQ") {
    tchBottleneck.SetRootQueueDisc ("ns3::DualQCoupledPi2QueueDisc",
                                    "QueueLimit", UintegerValue (queueDiscSize * 1500));
  }
  else {
    NS_ABORT_MSG ("Invalid queue disc type " + qdisc);
  }
  return tchBottleneck.Install (devicesBottleneckLink.Get (0));
}

TypeId GetCca (std::string ccaType) {
  return TypeId::LookupByName ("ns3::Tcp" + ccaType);
}

void StatFct (ApplicationContainer sourceApps, 
              uint32_t portBase,
              uint32_t flowNum,
              Ptr<OutputStreamWrapper> stream) {
  bool notCompleted = false;
  for (uint32_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
    Ptr<BulkSendApplication> appBulkSend = StaticCast<BulkSendApplication, Application> (sourceApps.Get(flowIndex));
    uint32_t totBytes = appBulkSend->GetTotalBytes ();
    Ptr<TcpSocketBase> tcpSocket = StaticCast<TcpSocketBase, Socket> (appBulkSend->GetSocket ());
    TcpSocket::TcpStates_t tcpSockState = tcpSocket->GetSockState ();
    if (tcpSockState < TcpSocket::TcpStates_t::CLOSING) {
      notCompleted = true;
      if (tcpSockState >= TcpSocket::TcpStates_t::ESTABLISHED) {
        *stream->GetStream () << Simulator::Now ().GetMilliSeconds () << 
          " FlowId " << flowIndex + portBase << 
          " TotalBytes " << totBytes << 
          " SocketState " << tcpSocket->GetSockState () <<
          " TcpCongState " << tcpSocket->GetCongState () << std::endl;
      }
    }
  }
  if (notCompleted) {
    Simulator::Schedule (Seconds (statIntervalSec), &StatFct, sourceApps, portBase, flowNum, stream);
  }
}

void StatQdisc (Ptr<QueueDisc> q, std::string qdisc, Ptr<OutputStreamWrapper> stream) {
  *stream->GetStream () << Simulator::Now ().GetMilliSeconds ();
  if (qdisc == "PfifoFast" || qdisc == "StrictPriority" || qdisc == "DualQ" || qdisc == "CoDel" || 
      qdisc == "Hhf" || qdisc == "Choke" || qdisc == "Sfb" || qdisc == "Red") {
    *stream->GetStream () << " " << q->GetNInternalQueues ();
    for (size_t i = 0; i < q->GetNInternalQueues (); i++) {
      Ptr<QueueDisc::InternalQueue> child = q->GetInternalQueue (i);
      *stream->GetStream () << " [ " << i << ", " << child->GetNPackets () << " ]";
    }
  }
  else if (qdisc == "FqCoDel") {
    Ptr<FqCoDelQueueDisc> qCodel = StaticCast <FqCoDelQueueDisc, QueueDisc> (q);
    std::list<Ptr<FqCoDelFlow> > flowList = qCodel->GetOldFlows ();
    *stream->GetStream () << " " << flowList.size ();

    UintegerValue qCodelMaxFlow;
    qCodel->GetAttribute ("Flows", qCodelMaxFlow);
    for (auto hash = flowHash.begin(); hash != flowHash.end(); hash++) {
      std::list <Ptr<FqCoDelFlow> >::iterator it;
      for (it = flowList.begin (); it != flowList.end (); it++) {
        if (*hash % qCodelMaxFlow.Get () == (*it)->GetIndex ()) {
          Ptr<CoDelQueueDisc> qCodelIn = StaticCast <CoDelQueueDisc, QueueDisc> ((*it)->GetQueueDisc ());
          *stream->GetStream () << " [ " << (*it)->GetIndex () << ", " << qCodelIn->GetNPackets () << " ]";
          break;
        }
      }
      if (it == flowList.end()) {
        *stream->GetStream () << " [ " << *hash % qCodelMaxFlow.Get () << ", 0 ]";
      }
    }
  }
  else if (qdisc == "Dwrr" || qdisc == "Tbf") {
    *stream->GetStream () << " " << q->GetNQueueDiscClasses ();
    for (size_t i = 0; i < q->GetNQueueDiscClasses (); i++) {
      Ptr<QueueDiscClass> child = q->GetQueueDiscClass (i);
      *stream->GetStream () << " [ " << i << ", " << child->GetQueueDisc ()->GetNPackets () << " ]";
    }
  }
  else if (qdisc == "Auto") {
    Ptr<AutoQueueDisc> qAuto = StaticCast <AutoQueueDisc, QueueDisc> (q);
    *stream->GetStream () << " " << qAuto->GetNumClasses ();
    for (size_t i = 0; i <= qAuto->GetNumClasses (); i++) {
      *stream->GetStream () << " [ " << i << ", " << qAuto->GetClassNPackets (i) << " ]";
    }
  }
  *stream->GetStream () << '\n';
  Simulator::Schedule(Seconds(statIntervalSec), &StatQdisc, q, qdisc, stream);
}

void InstallApp (Ptr<Node> source,
                 Ptr<Node> sink,
                 Ipv4InterfaceContainer sinkInterface,
                 std::string ccaType,
                 std::string appTrFileName,
                 std::string bwTrFileName,
                 std::string fctTrFileName,
                 float stopTime) {
  Ipv4Address sourceAddr = source->GetObject<Ipv4> ()->GetAddress (1, 0).GetLocal ();
  Ipv4Address sinkAddr = sink->GetObject<Ipv4> ()->GetAddress (1, 0).GetLocal ();
  AsciiTraceHelper ascii;
  uint32_t portBase = 10000;
  uint32_t ccaConf;
  std::stringstream ss;
  ss << std::hex << ccaType;
  ss >> ccaConf;

  Ptr<OutputStreamWrapper> appTrStream = ascii.CreateFileStream (appTrFileName.c_str ());
  Ptr<OutputStreamWrapper> bwTrStream = ascii.CreateFileStream (bwTrFileName.c_str ());

  for (uint16_t appIndex = 0; appIndex < appSettings.size (); appIndex++) {
    Time startTime = Seconds (1 + appIndex * 13);
    ApplicationContainer sourceApps, sinkApps;
    std::string appType = appSettings[appIndex].first;
    uint32_t appConf = appSettings[appIndex].second;
    if (appType == "")
      continue;
    uint32_t flowNum = appConf % 1000;
    uint32_t ccaOption = uint32_t ((ccaConf >> (appIndex * 4)) & 0xf);
    double_t labelRatio = uint32_t (diffServ / std::pow (10, appIndex)) % 10 / 9.;
    uint32_t dscpFlowNum = uint32_t (flowNum * labelRatio);
    bool tcpEnabled = ccaOption & 0xFC;

    if (appType == "Video" || appType == "Ctrl") {
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        uint16_t port = portBase + flowIndex;
        Ipv4Header::DscpType dscp = flowIndex < dscpFlowNum ? Ipv4Header::DSCP_CS2 : Ipv4Header::DscpDefault;
        if (tcpEnabled) {
          flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 6, port, port));
        }
        else {
          flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 17, port, port));
        }
        Ptr<GameServer> sendApp = CreateObject<GameServer> ();
        Ptr<GameClient> recvApp = CreateObject<GameClient> ();
        source->AddApplication (sendApp);
        sink->AddApplication (recvApp);
        Ipv4Address sendIp = source->GetObject<Ipv4> ()->GetAddress (1, 0).GetLocal ();
        Ipv4Address recvIp = sink->GetObject<Ipv4> ()->GetAddress (1, 0).GetLocal ();

        uint32_t delayDdlMs = 10000;
        uint32_t interval = 20;
        std::string fecPolicy = "rtx";

        sendApp->Setup (
          sendIp, port, recvIp, port,
          MicroSeconds (delayDdlMs * 1000), interval,
          fecPolicy, tcpEnabled, dscp, appTrStream, bwTrStream
        );
        recvApp->Setup (
          sendIp, port, port, interval, 
          MicroSeconds (delayDdlMs * 1000), tcpEnabled, appTrStream
        );

        sendApp->SetController (ccaMap[ccaOption]);
        sourceApps.Add (sendApp);
        sinkApps.Add (recvApp);
      }
    }
    else if (appType == "Ftp" || appType == "Web") {
      std::vector<uint32_t> ftpMaxBytes;
      if (flowNum == 0) {
        uint32_t webTraceId = appConf / 1000;
        std::vector<std::string> sizes;
        std::ifstream webIo ("webtraces/" + std::to_string (webTraceId) + ".log");
        std::stringstream buffer;
        buffer << webIo.rdbuf ();
        std::string tmpStr = buffer.str ();
        // boost::split (sizes, tmpStr, boost::is_any_of(", "), boost::token_compress_on);
        sizes.push_back(tmpStr);
        webIo.close ();
        for (auto flowSize : sizes) {
          ftpMaxBytes.push_back (std::stoi (flowSize));
        }
        flowNum = ftpMaxBytes.size ();
        dscpFlowNum = uint32_t (flowNum * labelRatio);
      } 
      else {
        ftpMaxBytes.resize (flowNum);
        std::fill (ftpMaxBytes.begin (), ftpMaxBytes.end (), (appConf / 1000) << 10);
      }
      NS_ASSERT (tcpEnabled); /* these apps have to be tcp */
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        uint16_t port = portBase + flowIndex;
        InetSocketAddress localAddress (Ipv4Address::GetAny (), port);
        Ipv4Header::DscpType dscp = flowIndex < dscpFlowNum ? Ipv4Header::DSCP_CS2 : Ipv4Header::DscpDefault;
        localAddress.SetTos (dscp << 2);
        InetSocketAddress remoteAddress (sinkAddr, port);
        remoteAddress.SetTos (dscp << 2);
        BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
        ftp.SetAttribute ("Local", AddressValue (localAddress));
        ftp.SetAttribute ("Remote", AddressValue (remoteAddress));
        ftp.SetAttribute ("MaxBytes", UintegerValue (ftpMaxBytes[flowIndex]));
        ftp.SetAttribute ("TcpCongestionOps", TypeIdValue (GetCca (ccaMap[ccaOption])));
        sourceApps.Add (ftp.Install (source));

        PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));

        flowHash.push_back (Ipv4Hash (sourceAddr, sinkAddr, 6, port, port));
      }
      Simulator::Schedule (Seconds (statIntervalSec) + startTime, &StatFct, sourceApps, portBase, flowNum, 
        ascii.CreateFileStream (fctTrFileName.c_str ()));
    }
    else if (appType == "OnOff") {
      uint32_t cbrRateMbps = (appConf / 1000) % 1000;
      uint32_t burstIntervalMs = appConf / 1000000;
      NS_ASSERT (!tcpEnabled);  /* these apps have to be udp */
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        uint16_t port = portBase + flowIndex;
        InetSocketAddress localAddress (Ipv4Address::GetAny (), port);
        Ipv4Header::DscpType dscp = flowIndex < dscpFlowNum ? Ipv4Header::DSCP_CS2 : Ipv4Header::DscpDefault;
        localAddress.SetTos (dscp << 2);
        InetSocketAddress remoteAddress (sinkAddr, port);
        remoteAddress.SetTos (dscp << 2);
        OnOffHelper onOff ("ns3::UdpSocketFactory", Address (remoteAddress));
        onOff.SetAttribute ("Local", AddressValue (localAddress));
        onOff.SetAttribute ("Remote", AddressValue (remoteAddress));
        if (burstIntervalMs > 0) {
          onOff.SetAttribute ("DataRate", DataRateValue ((cbrRateMbps << 20) * burstIntervalMs));
          onOff.SetAttribute ("PacketSize", UintegerValue (1472));
          onOff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.001]"));
          onOff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=" + std::to_string (double (burstIntervalMs - 1) / 1000.) + "]"));
        }
        else {
          onOff.SetConstantRate (DataRate (cbrRateMbps << 20), 1472);
        }
        sourceApps.Add (onOff.Install (source));

        PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));
        flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 17, port, port));
      }
    }
    else {
      NS_ABORT_MSG ("Unknown application type: " << appType);
    }
    sourceApps.Start (startTime);
    sourceApps.Stop (Seconds (stopTime - 2));
    sinkApps.Start (startTime);
    sinkApps.Stop (Seconds (stopTime - 1));
    portBase += 10000;
  }
}

std::string traceLine;
std::vector<std::string> traceData;
std::vector<std::string> bwValue;
std::vector<std::string> rttValue;
double timestamp;
std::string bandwidth;
double nextTimestamp;
std::string nextBandwidth;
std::vector<std::string> nextTraceData;

const double   TOPO_MIN_BW         = 1;      // in Mbps: 1 Mbps

Time oldDelay, newDelay;
Time remainInterval;
std::streampos pos = std::ios::beg;
Time minSetInterval = MicroSeconds (13);
Time maxAllowedDiff = MicroSeconds (11);
uint16_t BW_CHANGE = 0b1;
uint16_t DELAY_CHANGE = 0b10;
uint16_t LOSS_CHANGE = 0b100;

const double   DEFAULT_ERROR_RATE  = 0.00;

void BandwidthTrace (Ptr<Node> node0, 
                     Ptr<Node> node1, 
                     std::string trace, 
                     uint16_t change, 
                     int interval,
                     bool readNewLine) {
  Ptr<PointToPointNetDevice> n0SndDev = StaticCast<PointToPointNetDevice,NetDevice> (node0->GetDevice (1));
  Ptr<PointToPointNetDevice> n1RcvDev = StaticCast<PointToPointNetDevice,NetDevice> (node1->GetDevice (1));
  Ptr<PointToPointNetDevice> n1SndDev = StaticCast<PointToPointNetDevice,NetDevice> (node1->GetDevice (2));
  std::ifstream traceFile;
  int newBwBps;
  double newErrorRate = DEFAULT_ERROR_RATE;
  NS_ASSERT (maxAllowedDiff <= minSetInterval);

  bool bwChange = (change & BW_CHANGE) == BW_CHANGE;
  bool delayChange = (change & DELAY_CHANGE) == DELAY_CHANGE;
  bool lossChange = (change & LOSS_CHANGE) == LOSS_CHANGE;

  if (readNewLine) {
    std::string traceLine;
    std::vector<std::string> traceData;
    std::vector<std::string> bwValue;
    std::vector<std::string> rttValue;

    traceLine.clear ();
    traceFile.open (trace);
    traceFile.seekg (pos);
    std::getline (traceFile, traceLine);
    if (traceFile.eof ())
      return;
    pos = traceFile.tellg ();
    if (traceLine.find (' ') == std::string::npos) {
      traceFile.close ();
      NS_FATAL_ERROR ("Trace file format error " + trace);
      return;
    }
    traceData.clear ();
    SplitString (traceLine, traceData," ");

    if (bwChange) {
      bwValue.clear ();
      SplitString (traceData[0], bwValue, "Mbps");
      newBwBps = std::stod (bwValue[0]) * 1000000 * 15. / 13.;
      n1SndDev->SetAttribute ("DataRate", DataRateValue (DataRate (newBwBps)));
    }

    if (delayChange) {
      rttValue.clear ();
      SplitString (traceData[1], rttValue, "ms");
      /* Set delay of n0-n1 as rtt/2 - 1, the delay of n1-n2 is 1ms */ 
      newDelay = MilliSeconds (std::stod (rttValue[0]) / 2. - 1);
    }

    if (lossChange) {
      newErrorRate = std::stod (traceData[2]);
      ObjectFactory factoryErrModel;
      factoryErrModel.SetTypeId ("ns3::RateErrorModel");
      factoryErrModel.Set ("ErrorUnit", EnumValue (RateErrorModel::ERROR_UNIT_PACKET),
                           "ErrorRate", DoubleValue (newErrorRate));
      Ptr<ErrorModel> em = factoryErrModel.Create<ErrorModel> ();
      n1RcvDev->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    }
    
    NS_LOG_INFO (Simulator::Now ().GetMilliSeconds () << 
      " delay " << newDelay << " bw " << newBwBps << "bps errorRate " << newErrorRate);
    remainInterval = MilliSeconds (interval);
  }

  if (delayChange) {
    /* Set propagation delay, smoothsize to decrease 0.3ms every 0.33ms to avoid out of order
      These values are calculated based on 30Mbps and 1500B MTU --> 0.4ms / pkt */
    Ptr<Channel> channel = n0SndDev->GetChannel ();
    bool smoothDecrease = newDelay < oldDelay - maxAllowedDiff ? 1 : 0;
    oldDelay = smoothDecrease ? (oldDelay - maxAllowedDiff) : newDelay;
    channel->SetAttribute ("Delay", TimeValue (oldDelay));
    readNewLine = smoothDecrease && remainInterval > minSetInterval;
    remainInterval -= minSetInterval;
  }
  Simulator::Schedule (readNewLine ? remainInterval : minSetInterval, &BandwidthTrace, node0, node1, trace, 
    change, interval, readNewLine);
}


void ResetQueueSize (Ptr<QueueDisc> q, uint32_t newQueueSize) {
  q->SetMaxSize (QueueSize (QueueSizeUnit::PACKETS, newQueueSize));
}

int main (int argc, char *argv[]) {
  std::string dstBandwidth = "1000Mbps";
  std::string dstDelay = "0.1ms";
  std::string midBandwidth = "10Mbps";
  std::string midDelay = "20ms";
  std::string srcBandwidth = "1000Mbps";
  std::string srcDelay = "0.1ms";

  uint32_t queueDiscSize = 1000;

  std::string trace = "";
  std::string queueDiscType = "FqCoDel";       //PfifoFast or CoDel
  std::string ccaType = "0";
  float startTime = 0.1f;
  float simDuration = 300;         //in seconds

  int traceInterval = 200;  // in milliseconds

  bool isPcapEnabled = false;
  bool logging = false;
  std::string dir = "logs/";

  std::string appAType = "";
  std::string appBType = "";
  std::string appCType = "";
  std::string appDType = "";
  std::string appEType = "";
  uint32_t appAConf = 0;
  uint32_t appBConf = 0;
  uint32_t appCConf = 0;
  uint32_t appDConf = 0;
  uint32_t appEConf = 0;

  uint32_t resetQueueDisc = 0;

  uint32_t hop = 2;

  double_t sockBufDutyRatio = 0.9f;

  CommandLine cmd (__FILE__);
  cmd.AddValue ("dstBandwidth", "Bottleneck bandwidth", dstBandwidth);
  cmd.AddValue ("dstDelay", "Bottleneck delay", dstDelay);
  cmd.AddValue ("srcBandwidth", "Access link bandwidth", srcBandwidth);
  cmd.AddValue ("srcDelay", "Access link delay", srcDelay);
  cmd.AddValue ("midBandwidth", "Access link bandwidth", midBandwidth);
  cmd.AddValue ("midDelay", "Access link delay", midDelay);
  cmd.AddValue ("qdiscSize", "The size of the queue discipline, in the unit of packet", queueDiscSize);
  cmd.AddValue ("trace", "Trace file to simulate", trace);
  cmd.AddValue ("queueDiscType", "Bottleneck queue disc type: PfifoFast, CoDel", queueDiscType);
  cmd.AddValue ("ccaType", "Congestion control algorithm used by the sender", ccaType);
  cmd.AddValue ("diffServ", "Using DSCP markings for DiffServ or not.", diffServ);
  cmd.AddValue ("simDuration", "Simulation duration in seconds", simDuration);
  cmd.AddValue ("isPcapEnabled", "Flag to enable/disable pcap", isPcapEnabled);
  cmd.AddValue ("logging", "Flag to enable/disable logging", logging);
  cmd.AddValue ("logdir", "The base output directory for all results", dir);

  cmd.AddValue ("sockBufDutyRatio", "The duty ratio of the socket buffer", sockBufDutyRatio);

  cmd.AddValue ("appAType", "Application A type", appAType);
  cmd.AddValue ("appAConf", "Application A configuration", appAConf);
  cmd.AddValue ("appBType", "Application A type", appBType);
  cmd.AddValue ("appBConf", "Application A configuration", appBConf);
  cmd.AddValue ("appCType", "Application A type", appCType);
  cmd.AddValue ("appCConf", "Application A configuration", appCConf);
  cmd.AddValue ("appDType", "Application A type", appDType);
  cmd.AddValue ("appDConf", "Application A configuration", appDConf);
  cmd.AddValue ("appEType", "Application A type", appEType);
  cmd.AddValue ("appEConf", "Application A configuration", appEConf);

  cmd.AddValue ("autoDecayingFunc", "the function of decaying in AutoQueueDisc", autoDecayingFunc);
  cmd.AddValue ("autoDecayingCoef", "the coefficient of decaying in AutoQueueDisc", autoDecayingCoef);
  cmd.AddValue ("dwrrPrioRatio", "DWRR ratio between different classes", dwrrPrioRatio);
  cmd.AddValue ("codelTarget", "Target value of dropping packets in CoDel", codelTarget);
  cmd.AddValue ("tbfRate", "", tbfRate);
  cmd.AddValue ("tbfBurst", "", tbfBurst);

  cmd.AddValue ("resetQueueDisc", "Reset queue disc; only in cca-cut exps", resetQueueDisc);

  cmd.AddValue ("hop", "Number of hops in the line topology; only in different btlbw exps", hop);

  cmd.Parse (argc, argv);

  std::string conf = ccaType;

  // parse application settings
  if (appAType != "") {
    conf += "_" + appAType + std::to_string(appAConf);
    appSettings.push_back(std::make_pair(appAType, appAConf));
  }
  else {
    appSettings.push_back(std::make_pair("", 0));
  }

  if (appBType != "") {
    conf += "_" + appBType + std::to_string(appBConf);
    appSettings.push_back(std::make_pair(appBType, appBConf));
  }
  else {
    appSettings.push_back(std::make_pair("", 0));
  }

  if (appCType != "") {
    conf += "_" + appCType + std::to_string(appCConf);
    appSettings.push_back(std::make_pair(appCType, appCConf));
  }
  else {
    appSettings.push_back(std::make_pair("", 0));
  }
  
  if (appDType != "") {
    conf += "_" + appDType + std::to_string(appDConf);
    appSettings.push_back(std::make_pair(appDType, appDConf));
  }
  else {
    appSettings.push_back(std::make_pair("", 0));
  }
  
  if (appEType != "") {
    conf += "_" + appEType + std::to_string(appEConf);
    appSettings.push_back(std::make_pair(appEType, appEConf));
  }
  else {
    appSettings.push_back(std::make_pair("", 0));
  }

  if (hop == 3) {
    conf += "_" + srcBandwidth + "_" + midBandwidth + "_" + dstBandwidth;
  }
  

  /* qdisc settings */
  conf += "/" + queueDiscType + "_" + std::to_string(diffServ) + "_";
  if (queueDiscType == "Dwrr") {
    conf += std::to_string (dwrrPrioRatio).substr (0, 4);
  } else if (queueDiscType == "Auto") {
    conf += std::to_string (dwrrPrioRatio).substr (0, 4) + "_" + 
      autoDecayingFunc + std::to_string (autoDecayingCoef).substr (0, 4);
  } else if (queueDiscType == "FqCoDel" || queueDiscType == "CoDel") {
    conf += codelTarget + std::to_string (queueDiscSize);
  } else if (queueDiscType == "Tbf") {
    conf += tbfRate + std::to_string (tbfBurst);
  } else {
    conf += std::to_string (queueDiscSize);
  }

  if (trace != "") {
    std::string traceDir, traceName;
    std::stringstream tracePath (trace);
    std::getline (tracePath, traceDir, '/');
    std::getline (tracePath, traceName, '/');
    conf += "/" + traceName;
  }
  
  // boost::filesystem::create_directories (dir + conf);
  namespace fs = std::filesystem;
  fs::create_directories(dir + conf);
  std::string pcapFileName    = dir + conf + "/pcap";
  std::string pcapTrFileName  = dir + conf + "/pcap.tr";
  std::string appTrFileName   = dir + conf + "/app.tr";
  std::string cwndTrFileName  = dir + conf + "/cwnd.tr";
  std::string qdiscTrFileName = dir + conf + "/qdisc.tr";
  std::string bwTrFileName    = dir + conf + "/bw.tr"; 
  std::string fctTrFileName   = dir + conf + "/fct.tr"; 
  std::string dropTrFileName  = dir + conf + "/drop.tr"; 

  std::string tmp;
  std::ifstream traceFile;
  if (trace != "") {
    float n = 0.;
    traceFile.open (trace, std::ios::in);
    if (traceFile.fail ()) {
      NS_FATAL_ERROR ("Trace file fail to open! " + trace);
    } else {
      while (getline (traceFile, tmp)) {
        n += 1.;
      }
      traceFile.close();
      simDuration = std::min (simDuration, (float) (n * traceInterval / 1000. - 2));
      if (simDuration < 2) {
        NS_FATAL_ERROR ("Trace file too short! " + trace);
      }
    }
  }

  float stopTime = startTime + simDuration;

  if (logging) {
    LogComponentEnableAll (LOG_LEVEL_WARN);

    LogComponentEnable ("QdiscTest", LOG_LEVEL_INFO);
    LogComponentEnable ("AutoFlow", LOG_LEVEL_INFO);

    // LogComponentEnable ("PacketUdpSender", LOG_LEVEL_ALL);
    // LogComponentEnable ("GccController", LOG_LEVEL_ALL);
    // LogComponentEnable ("SenderBasedController", LOG_LEVEL_ALL);

    LogComponentEnable ("AutoQueueDisc", LOG_LEVEL_INFO);
    LogComponentEnable ("DwrrQueueDisc", LOG_LEVEL_INFO);
    // LogComponentEnable ("QueueDisc", LOG_LEVEL_INFO);
    // LogComponentEnable ("PfifoFastQueueDisc", LOG_LEVEL_ALL);
    // LogComponentEnable ("CoDelQueueDisc", LOG_LEVEL_ALL);
    // LogComponentEnable ("HhfQueueDisc", LOG_LEVEL_ALL);
    // LogComponentEnable ("ChokeQueueDisc", LOG_LEVEL_ALL);
    // LogComponentEnable ("SfbQueueDisc", LOG_LEVEL_ALL);

    LogComponentEnable ("TcpBbr", LOG_LEVEL_DEBUG);
  }

  // Enable checksum
  if (isPcapEnabled) {
    // GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  }

  // Devices queue configuration
  Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue (QueueSize ("1p")));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1448));
  Config::SetDefault ("ns3::BulkSendApplication::SendSize", UintegerValue (1448));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (5 << 20)); // if (rwnd > 5M)，retransmission (RTO) will accumulate
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (5 << 20)); // over 5M packets, causing packet metadata overflows.
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  Config::SetDefault ("ns3::TcpCopa::ModeSwitch", BooleanValue (false));
  Config::SetDefault ("ns3::PacketTcpSender::SockBufDutyRatio", DoubleValue (sockBufDutyRatio));
  // Create gateway, source, and sink
  NodeContainer nodes;
  nodes.Create (hop + 1);

  // Create and configure access link and bottleneck link
  PointToPointHelper srcLink;
  srcLink.SetDeviceAttribute ("DataRate", StringValue (srcBandwidth));
  srcLink.SetChannelAttribute ("Delay", StringValue (srcDelay));

  PointToPointHelper midLink;
  midLink.SetDeviceAttribute ("DataRate", StringValue (midBandwidth));
  midLink.SetChannelAttribute ("Delay", StringValue (midDelay));
  
  PointToPointHelper dstLink;
  dstLink.SetDeviceAttribute ("DataRate", StringValue (dstBandwidth));
  dstLink.SetChannelAttribute ("Delay", StringValue (dstDelay));

  InternetStackHelper stack;
  stack.InstallAll ();

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");

  // Configure the source and sink net devices
  // and the channels between the source/sink and the gateway
  NetDeviceContainer devicesSrcLink, devicesDstLink, devicesMidLink;

  devicesSrcLink = srcLink.Install (nodes.Get (0), nodes.Get (1));
  InstallQdisc ("Fifo", queueDiscSize, devicesSrcLink);
  address.NewNetwork ();
  Ipv4InterfaceContainer interfaces = address.Assign (devicesSrcLink);

  devicesMidLink = midLink.Install (nodes.Get (1), nodes.Get (2));
  QueueDiscContainer qdiscs = InstallQdisc (queueDiscType, queueDiscSize, devicesMidLink);

  /* reset queue disc after bandwidth reduction, only in cca-cut exps */
  if (resetQueueDisc) {
    uint32_t cutRatio = resetQueueDisc % 100;
    uint32_t cutTime = resetQueueDisc / 100;
    
    if (cutRatio > 5) {
      uint32_t newQueueSize = queueDiscSize * 5. / cutRatio;
      Simulator::Schedule (MilliSeconds (cutTime * traceInterval + 1000), &ResetQueueSize, qdiscs.Get (0), newQueueSize);
    }
  }

  address.NewNetwork ();
  interfaces = address.Assign (devicesMidLink);

  /* Only for experiments where Confucius is not on the bottleneck. */
  if (hop == 3) {
    devicesDstLink = dstLink.Install (nodes.Get (2), nodes.Get (3));
    InstallQdisc ("Fifo", queueDiscSize, devicesDstLink);
    address.NewNetwork ();
    interfaces = address.Assign (devicesDstLink);
  }

  Ipv4InterfaceContainer sinkInterface;
  sinkInterface.Add (interfaces.Get (1));

  NS_LOG_WARN ("Initialize Global Routing.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  InstallApp (nodes.Get(0), nodes.Get(hop), sinkInterface, ccaType,
    appTrFileName, bwTrFileName, fctTrFileName, stopTime);

  if (trace != "") {
    BandwidthTrace (nodes.Get(0), nodes.Get(1), trace, 
      BW_CHANGE & ~DELAY_CHANGE & ~LOSS_CHANGE, 
      traceInterval, true);
  }

  AsciiTraceHelper ascii;
  Packet::EnablePrinting ();
  Simulator::Schedule (MicroSeconds (10), &TraceQdiscDrop, qdiscs.Get (0), ascii.CreateFileStream (dropTrFileName));
  Simulator::Schedule (Seconds(statIntervalSec + 1), &StatQdisc, qdiscs.Get (0), queueDiscType, ascii.CreateFileStream (qdiscTrFileName));

  if (isPcapEnabled) {
    // srcLink.EnablePcap (pcapFileName, nodes, true);
    // AsciiTraceHelper ascii;
    // srcLink.EnableAsciiAll (ascii.CreateFileStream (pcapTrFileName));
  }

  Simulator::Stop (Seconds (stopTime));
  Simulator::Run ();

  Simulator::Destroy ();
  return 0;
}
