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
// #include <boost/filesystem.hpp>
// #include <boost/algorithm/string/classification.hpp> // Include boost::for is_any_of
// #include <boost/algorithm/string/split.hpp> // Include for boost::split
#include <filesystem>
#include <random>
#include <cstddef>
#include <algorithm>
#include <regex>

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
#include "ns3/flow-monitor-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("QdiscTest");

/*Buffer Management Algorithms*/
# define DT 101
# define FAB 102
# define CS 103
# define IB 104
# define ABM 110
# define MY 111
# define LLM_DT_COT_0 100000
# define LLM_DT_COT_1 100001
# define LLM_DT_COT_2 100002
# define LLM_DT_BRDG_0 100010
# define LLM_DT_BRDG_1 100011
# define LLM_DT_BRDG_2 100012
# define LLM_TTR_COT_0 100100
# define LLM_TTR_COT_1 100101
# define LLM_TTR_COT_2 100102

/*Queue Scheme*/
# define FQ 201
# define CCA 202
# define RTT 203
# define SQ 204

bool light_logging = true;
bool long_goodput_logging = false;
bool fct_logging = false;
bool torstats_sinkonly = true;

/* experiment settings */
double_t dwrrPrioRatio = 2.;
std::string codelTarget = "5ms";
std::string codelInterval = "100ms";
std::string pieTarget = "20ms";
std::string tbfRate = "10Mbps";
uint32_t tbfBurst = 50000;

// uint32_t diffServ = 0;

// std::vector < std::vector < std::vector<std::string> > > appSettingsSink1, appSettingsSink2;
std::vector<std::vector < std::vector < std::vector<std::string> > >> appSettingsArray;

std::string ccaMap[16] = {"Gcc", "Nada", "", "Fixed", 
  "Cubic", "Bbr", "Copa", "Yeah", 
  "Illinois", "Vegas", "Htcp", "Bic", 
  "LinuxReno", "Scalable", "", ""};

std::string autoDecayingFunc = "Linear";
double_t autoDecayingCoef = 1.;

double statIntervalSec = 0.001; // 0.001;
double fctStatIntervalSec = 0.000001; // 0.001;
double gptStatIntervalSec = 0.1;
std::vector<uint32_t> flowHash;

Ptr<SharedMemoryBuffer> sharedMemory;
QueueDiscContainer bottleneckQueueDiscsCollection;
QueueDiscContainer outputQueueDiscsCollection;
// Ptr<UtilityWarehouse> utilityWarehouse;

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

// QueueDiscContainer InstallQdisc (std::string qdisc, 
//                                  uint32_t queueDiscSize,
//                                  NetDeviceContainer devicesBottleneckLink) {
//   TrafficControlHelper tchBottleneck;
//   if (qdisc == "PfifoFast" || qdisc == "StrictPriority" || 
//       qdisc == "Hhf" || qdisc == "Choke" || qdisc == "Sfb" || qdisc == "Fifo") {
//     tchBottleneck.SetRootQueueDisc ("ns3::" + qdisc + "QueueDisc", 
//                                     "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"));
//   } else if (qdisc == "FqCoDel") {
//     tchBottleneck.SetRootQueueDisc ("ns3::FqCoDelQueueDisc",
//                                     "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"), 
//                                     "Target", StringValue (codelTarget));
//   } else if (qdisc == "CoDel") {
//     tchBottleneck.SetRootQueueDisc ("ns3::CoDelQueueDisc", 
//                                     "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
//                                     "Target", StringValue (codelTarget));
//   } else if (qdisc == "Red") {
//     tchBottleneck.SetRootQueueDisc ("ns3::RedQueueDisc", 
//                                     "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
//                                     "MinTh", DoubleValue (queueDiscSize / 12.),
//                                     "MaxTh", DoubleValue (queueDiscSize / 4.));
//   } else if (qdisc == "Dwrr") {
//     tchBottleneck.SetRootQueueDisc ("ns3::DwrrQueueDisc",
//                                     "NumClass", UintegerValue (2),
//                                     "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
//                                     "PrioRatio", DoubleValue (dwrrPrioRatio));
//   } else if (qdisc == "Auto") {
//     Config::SetDefault ("ns3::AutoFlow::DecayingFunction", StringValue (autoDecayingFunc));
//     Config::SetDefault ("ns3::AutoFlow::DecayingCoef", DoubleValue (autoDecayingCoef));
//     tchBottleneck.SetRootQueueDisc ("ns3::AutoQueueDisc",
//                                     "NumClass", UintegerValue (3),
//                                     "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
//                                     "PrioRatio", DoubleValue (dwrrPrioRatio));
//   } else if (qdisc == "Tbf") {
//     tchBottleneck.SetRootQueueDisc ("ns3::TbfQueueDisc",
//                                     "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"),
//                                     "Rate", DataRateValue (DataRate (tbfRate)),
//                                     "Burst", UintegerValue (tbfBurst));

//   } else if (qdisc == "DualQ") {
//     tchBottleneck.SetRootQueueDisc ("ns3::DualQCoupledPi2QueueDisc",
//                                     "QueueLimit", UintegerValue (queueDiscSize * 1500));
//   }
//   else {
//     NS_ABORT_MSG ("Invalid queue disc type " + qdisc);
//   }
//   return tchBottleneck.Install (devicesBottleneckLink.Get (0));
// }

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
      // if (tcpSockState >= TcpSocket::TcpStates_t::ESTABLISHED) {
      *stream->GetStream () << Simulator::Now ().GetMicroSeconds () << 
        " FlowId " << flowIndex + portBase << 
        " TotalBytes " << totBytes << 
        " SocketState " << tcpSocket->GetSockState () <<
        " TcpCongState " << tcpSocket->GetCongState () << std::endl;
      // }
    }
  }
  if (notCompleted) {
    if (!light_logging) {
      Simulator::Schedule (Seconds (fctStatIntervalSec), &StatFct, sourceApps, portBase, flowNum, stream);
    } else {
      if (fct_logging) {
        Simulator::Schedule (Seconds (fctStatIntervalSec), &StatFct, sourceApps, portBase, flowNum, stream);
      }
    } 
  } 
}

std::map<uint32_t,std::vector<uint64_t>> flowsizemap;
std::map<uint32_t,std::vector<bool>> flowstartmap;
std::map<uint32_t,std::vector<bool>> flowendmap;

void StatFctStartEndOnly (ApplicationContainer sourceApps, 
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
      int64_t now = Simulator::Now ().GetMicroSeconds ();
      if (totBytes>0 && flowstartmap[portBase][flowIndex]) {
        flowstartmap[portBase][flowIndex]=false;
        *stream->GetStream () << now << " FlowId " << flowIndex + portBase << " TotalBytes " << totBytes << std::endl;
      }
      if (totBytes==flowsizemap[portBase][flowIndex] && flowendmap[portBase][flowIndex]) {
        flowendmap[portBase][flowIndex]=false;
        *stream->GetStream () << now << " FlowId " << flowIndex + portBase << " TotalBytes " << totBytes << std::endl;
      }

      // if (tcpSockState >= TcpSocket::TcpStates_t::ESTABLISHED) {
      // *stream->GetStream () << Simulator::Now ().GetMicroSeconds () << 
      //   " FlowId " << flowIndex + portBase << 
      //   " TotalBytes " << totBytes << 
      //   " SocketState " << tcpSocket->GetSockState () <<
      //   " TcpCongState " << tcpSocket->GetCongState () << std::endl;
      // }
    }
  }
  if (notCompleted) {
    if (!light_logging) {
      Simulator::Schedule (Seconds (fctStatIntervalSec), &StatFctStartEndOnly, sourceApps, portBase, flowNum, stream);
    } else {
      if (fct_logging) {
        Simulator::Schedule (Seconds (fctStatIntervalSec), &StatFctStartEndOnly, sourceApps, portBase, flowNum, stream);
      }
    } 
  } 
}

void StatGoodput (ApplicationContainer sourceApps, 
              ApplicationContainer sinkApps,
              uint32_t portBase,
              uint32_t flowNum,
              uint64_t prevTotalPacketsThr,
              Ptr<OutputStreamWrapper> stream) {
  bool notCompleted = false;
  uint64_t totalPacketsThr = 0;
  for (uint32_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
    Ptr<BulkSendApplication> appBulkSend = StaticCast<BulkSendApplication, Application> (sourceApps.Get(flowIndex));
    uint32_t totBytes = appBulkSend->GetTotalBytes ();
    Ptr<TcpSocketBase> tcpSocket = StaticCast<TcpSocketBase, Socket> (appBulkSend->GetSocket ());
    TcpSocket::TcpStates_t tcpSockState = tcpSocket->GetSockState ();
    if (tcpSockState < TcpSocket::TcpStates_t::CLOSING) {
      notCompleted = true;
      if (tcpSockState >= TcpSocket::TcpStates_t::ESTABLISHED) {
        totalPacketsThr = DynamicCast<PacketSink> (sinkApps.Get (flowIndex))->GetTotalRx ();
        *stream->GetStream () << Simulator::Now ().GetMilliSeconds () << 
          " FlowId " << flowIndex + portBase << 
          " TotalBytes " << totBytes << 
          " SocketState " << tcpSocket->GetSockState () <<
          " TcpCongState " << tcpSocket->GetCongState () << 
          " GoodputRxBytes " << totalPacketsThr << 
          " ThisGoodputRxBytes " << totalPacketsThr-prevTotalPacketsThr <<
          std::endl;
      }
    }
  }
  if (notCompleted) {
    if (!light_logging) {
      Simulator::Schedule (Seconds (gptStatIntervalSec), &StatGoodput, sourceApps, sinkApps, portBase, flowNum, totalPacketsThr, stream);
    } else {
      if (long_goodput_logging) {
        Simulator::Schedule (Seconds (gptStatIntervalSec), &StatGoodput, sourceApps, sinkApps, portBase, flowNum, totalPacketsThr, stream);
      }
    }
  }
}

std::map<uint32_t,std::vector<uint64_t>> gptflowsizemap;
std::map<uint32_t,std::vector<bool>> gptflowstartmap;
std::map<uint32_t,std::vector<bool>> gptflowendmap;

void StatGoodputStartEndOnly (ApplicationContainer sourceApps, 
              ApplicationContainer sinkApps,
              uint32_t portBase,
              uint32_t flowNum,
              uint64_t prevTotalPacketsThr,
              Ptr<OutputStreamWrapper> stream) {
  bool notCompleted = false;
  uint64_t totalPacketsThr = 0;
  for (uint32_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
    Ptr<BulkSendApplication> appBulkSend = StaticCast<BulkSendApplication, Application> (sourceApps.Get(flowIndex));
    uint32_t totBytes = appBulkSend->GetTotalBytes ();
    Ptr<TcpSocketBase> tcpSocket = StaticCast<TcpSocketBase, Socket> (appBulkSend->GetSocket ());
    TcpSocket::TcpStates_t tcpSockState = tcpSocket->GetSockState ();
    if (tcpSockState < TcpSocket::TcpStates_t::CLOSING) {
      notCompleted = true;
      if (tcpSockState >= TcpSocket::TcpStates_t::ESTABLISHED) {
        totalPacketsThr = DynamicCast<PacketSink> (sinkApps.Get (flowIndex))->GetTotalRx ();
        int64_t now = Simulator::Now ().GetMicroSeconds ();
        if (totalPacketsThr>0 && gptflowstartmap[portBase][flowIndex]) {
          gptflowstartmap[portBase][flowIndex]=false;
          *stream->GetStream () << now << " FlowId " << flowIndex + portBase << " GoodputRxBytes " << totalPacketsThr << std::endl;
        }
        if (totalPacketsThr==gptflowsizemap[portBase][flowIndex] && gptflowendmap[portBase][flowIndex]) {
          gptflowendmap[portBase][flowIndex]=false;
          *stream->GetStream () << now << " FlowId " << flowIndex + portBase << " GoodputRxBytes " << totalPacketsThr << std::endl;
        }
        // *stream->GetStream () << Simulator::Now ().GetMicroSeconds () << 
        //   " FlowId " << flowIndex + portBase << 
        //   " TotalBytes " << totBytes << 
        //   " SocketState " << tcpSocket->GetSockState () <<
        //   " TcpCongState " << tcpSocket->GetCongState () << 
        //   " GoodputRxBytes " << totalPacketsThr << 
        //   " ThisGoodputRxBytes " << totalPacketsThr-prevTotalPacketsThr <<
        //   std::endl;
      }
    }
  }
  if (notCompleted) {
    if (!light_logging) {
      Simulator::Schedule (Seconds (gptStatIntervalSec), &StatGoodputStartEndOnly, sourceApps, sinkApps, portBase, flowNum, totalPacketsThr, stream);
    } else {
      if (long_goodput_logging) {
        Simulator::Schedule (Seconds (gptStatIntervalSec), &StatGoodputStartEndOnly, sourceApps, sinkApps, portBase, flowNum, totalPacketsThr, stream);
      }
    }
  }
}

void StatQdisc (Ptr<QueueDisc> q, std::string qdisctype, Ptr<OutputStreamWrapper> stream) {
  // for (uint32_t port = 0; port<bottleneckQueueDiscsCollection.GetN(); port++) {
  //   Ptr<GenQueueDisc> q = DynamicCast<GenQueueDisc>(bottleneckQueueDiscsCollection.Get(port));
    *stream->GetStream () << Simulator::Now ().GetMilliSeconds ();
    // if (qdisctype == "PfifoFast" || qdisctype == "StrictPriority" || qdisctype == "DualQ" || qdisctype == "CoDel" || 
    //     qdisctype == "Hhf" || qdisctype == "Choke" || qdisctype == "Sfb" || qdisctype == "Red") {
    //   *stream->GetStream () << " " << q->GetNInternalQueues ();
    //   for (size_t i = 0; i < q->GetNInternalQueues (); i++) {
    //     Ptr<QueueDisc::InternalQueue> child = q->GetInternalQueue (i);
    //     *stream->GetStream () << " [ " << i << ", " << child->GetNPackets () << " ]";
    //   }
    // }
    // else if (qdisctype == "Dwrr" || qdisctype == "Tbf") {
      *stream->GetStream () << " " << q->GetNQueueDiscClasses ();
      for (size_t i = 0; i < q->GetNQueueDiscClasses (); i++) {
        Ptr<QueueDiscClass> child = q->GetQueueDiscClass (i);
        *stream->GetStream () << " [ " << i << ", " << child->GetQueueDisc ()->GetNPackets () << " ]";
      }
    // }
    // else if (qdisctype == "Auto") {
    //   Ptr<AutoQueueDisc> qAuto = StaticCast <AutoQueueDisc, QueueDisc> (q);
    //   *stream->GetStream () << " " << qAuto->GetNumClasses ();
    //   for (size_t i = 0; i <= qAuto->GetNumClasses (); i++) {
    //     *stream->GetStream () << " [ " << i << ", " << qAuto->GetClassNPackets (i) << " ]";
    //   }
    // }
    *stream->GetStream () << '\n';
    if (!light_logging) {
      Simulator::Schedule(Seconds(statIntervalSec), &StatQdisc, q, qdisctype, stream);
    }
  // }
}
// void StatQdisc (Ptr<QueueDisc> q, std::string qdisc, Ptr<OutputStreamWrapper> stream) {
//   *stream->GetStream () << Simulator::Now ().GetMilliSeconds ();
//   if (qdisc == "PfifoFast" || qdisc == "StrictPriority" || qdisc == "DualQ" || qdisc == "CoDel" || 
//       qdisc == "Hhf" || qdisc == "Choke" || qdisc == "Sfb" || qdisc == "Red") {
//     *stream->GetStream () << " " << q->GetNInternalQueues ();
//     for (size_t i = 0; i < q->GetNInternalQueues (); i++) {
//       Ptr<QueueDisc::InternalQueue> child = q->GetInternalQueue (i);
//       *stream->GetStream () << " [ " << i << ", " << child->GetNPackets () << " ]";
//     }
//   }
//   else if (qdisc == "FqCoDel") {
//     Ptr<FqCoDelQueueDisc> qCodel = StaticCast <FqCoDelQueueDisc, QueueDisc> (q);
//     std::list<Ptr<FqCoDelFlow> > flowList = qCodel->GetOldFlows ();
//     *stream->GetStream () << " " << flowList.size ();

//     UintegerValue qCodelMaxFlow;
//     qCodel->GetAttribute ("Flows", qCodelMaxFlow);
//     for (auto hash = flowHash.begin(); hash != flowHash.end(); hash++) {
//       std::list <Ptr<FqCoDelFlow> >::iterator it;
//       for (it = flowList.begin (); it != flowList.end (); it++) {
//         if (*hash % qCodelMaxFlow.Get () == (*it)->GetIndex ()) {
//           Ptr<CoDelQueueDisc> qCodelIn = StaticCast <CoDelQueueDisc, QueueDisc> ((*it)->GetQueueDisc ());
//           *stream->GetStream () << " [ " << (*it)->GetIndex () << ", " << qCodelIn->GetNPackets () << " ]";
//           break;
//         }
//       }
//       if (it == flowList.end()) {
//         *stream->GetStream () << " [ " << *hash % qCodelMaxFlow.Get () << ", 0 ]";
//       }
//     }
//   }
//   else if (qdisc == "Dwrr" || qdisc == "Tbf") {
//     *stream->GetStream () << " " << q->GetNQueueDiscClasses ();
//     for (size_t i = 0; i < q->GetNQueueDiscClasses (); i++) {
//       Ptr<QueueDiscClass> child = q->GetQueueDiscClass (i);
//       *stream->GetStream () << " [ " << i << ", " << child->GetQueueDisc ()->GetNPackets () << " ]";
//     }
//   }
//   else if (qdisc == "Auto") {
//     Ptr<AutoQueueDisc> qAuto = StaticCast <AutoQueueDisc, QueueDisc> (q);
//     *stream->GetStream () << " " << qAuto->GetNumClasses ();
//     for (size_t i = 0; i <= qAuto->GetNumClasses (); i++) {
//       *stream->GetStream () << " [ " << i << ", " << qAuto->GetClassNPackets (i) << " ]";
//     }
//   }
//   *stream->GetStream () << '\n';
//   Simulator::Schedule(Seconds(statIntervalSec), &StatQdisc, q, qdisc, stream);
// }

uint64_t flowIdGlobal = 0;
std::map<uint32_t, uint32_t> ccaQueueidMapping;
std::vector<double> rttBuckets;
// std::vector < float > sendersAsrcLinkRates, sendersBsrcLinkRates;
std::vector<std::vector<double>> sendersSrcLinkRatesArray;

void InstallApp (Ptr<Node> source,
                 Ptr<Node> sink,
                //  Ipv4InterfaceContainer sinkInterface,
                //  std::string ccaType,
                 std::string appTrFileName,
                 std::string bwTrFileName,
                 std::string fctTrFileName,
                 std::string gptTrFileName,
                 float stopTime,
                //  std::vector < std::vector<std::string> > appSettings,
                 uint32_t sinkID,
                 uint32_t senderID,
                 uint32_t portBase,
                 uint32_t randomSeed,
                 uint32_t mainRoomQueueScheme) {
  Ipv4Address sourceAddr = source->GetObject<Ipv4> ()->GetAddress (1, 0).GetLocal ();
  Ipv4Address sinkAddr = sink->GetObject<Ipv4> ()->GetAddress (1, 0).GetLocal ();
  AsciiTraceHelper ascii;
  std::vector < std::vector<std::string> > appSettings = appSettingsArray[sinkID][senderID];
  double srcDelay = sendersSrcLinkRatesArray[sinkID][senderID];
  // if (sinkID == 0) {
  //   appSettings = appSettingsSink1.at(senderID);
  //   srcDelay = sendersAsrcLinkRates[senderID];
  // } else if (sinkID == 1) {
  //   appSettings = appSettingsSink2.at(senderID);
  //   srcDelay = sendersBsrcLinkRates[senderID];
  // }
  double rtt = (srcDelay+1)*2;
  // uint32_t portBase = 10000;
  // uint32_t ccaConf;
  // std::stringstream ss;
  // ss << std::hex << ccaType;
  // ss >> ccaConf;

  // Ptr<OutputStreamWrapper> appTrStream = ascii.CreateFileStream (appTrFileName.c_str ());
  // Ptr<OutputStreamWrapper> bwTrStream = ascii.CreateFileStream (bwTrFileName.c_str ());

  std::default_random_engine re;
  for (uint16_t appIndex = 0; appIndex < appSettings.size (); appIndex++) {
    // Time startTime = Seconds (1 + appIndex * 13);
    Time startTime = Seconds(1);
    ApplicationContainer sourceApps, sinkApps;
    std::string appType = appSettings[appIndex].at(0);
    uint64_t appConf = 0;
    std::string appConfString = "";
    if (appType.compare("BurstV3")==0) {
      appConfString = appSettings[appIndex].at(1);
    } else {
      appConf = std::stoi(appSettings[appIndex].at(1));
    }
    float appStart = std::stof(appSettings[appIndex].at(2));
    uint32_t ccaOption = std::stoi(appSettings[appIndex].at(3));
    // uint32_t diffServ = std::stoi(appSettings[appIndex].at(4));
    uint32_t longBurstV3FlowPriority = std::stoi(appSettings[appIndex].at(4));
    uint32_t diffServ = 0;
    if (appType == "")
      continue;
    uint32_t flowNum = appConf % 1000;
    // uint32_t ccaOption = uint32_t ((ccaConf >> (appIndex * 4)) & 0xf);
    double_t labelRatio = uint32_t (diffServ / std::pow (10, appIndex)) % 10 / 9.;
    uint32_t dscpFlowNum = uint32_t (flowNum * labelRatio);
    bool tcpEnabled = ccaOption & 0xFC;

    if (appType == "Video" || appType == "Ctrl") {
      // for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
      //   uint16_t port = portBase + flowIndex;
      //   Ipv4Header::DscpType dscp = flowIndex < dscpFlowNum ? Ipv4Header::DSCP_CS2 : Ipv4Header::DscpDefault;
      //   if (tcpEnabled) {
      //     flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 6, port, port));
      //   }
      //   else {
      //     flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 17, port, port));
      //   }
      //   Ptr<GameServer> sendApp = CreateObject<GameServer> ();
      //   Ptr<GameClient> recvApp = CreateObject<GameClient> ();
      //   source->AddApplication (sendApp);
      //   sink->AddApplication (recvApp);
      //   Ipv4Address sendIp = source->GetObject<Ipv4> ()->GetAddress (1, 0).GetLocal ();
      //   Ipv4Address recvIp = sink->GetObject<Ipv4> ()->GetAddress (1, 0).GetLocal ();

      //   uint32_t delayDdlMs = 10000;
      //   uint32_t interval = 20;
      //   std::string fecPolicy = "rtx";

      //   sendApp->Setup (
      //     sendIp, port, recvIp, port,
      //     MicroSeconds (delayDdlMs * 1000), interval,
      //     fecPolicy, tcpEnabled, dscp, appTrStream, bwTrStream
      //   );
      //   recvApp->Setup (
      //     sendIp, port, port, interval, 
      //     MicroSeconds (delayDdlMs * 1000), tcpEnabled, appTrStream
      //   );

      //   sendApp->SetController (ccaMap[ccaOption]);
      //   sourceApps.Add (sendApp);
      //   sinkApps.Add (recvApp);
      // }
    }
    else if (appType == "Ftp" || appType == "Web") {
      std::vector<uint32_t> ftpMaxBytes;
      if (flowNum == 0) {
        uint32_t webTraceId = appConf / 1000;
        std::vector<std::string> sizes;
        std::ifstream webIo ("webtraces/" + std::to_string (webTraceId) + ".log");
        std::cout << "Using webtrace: webtraces/" << webTraceId << ".log" << std::endl;
        std::stringstream buffer;
        buffer << webIo.rdbuf ();
        std::string tmpStr = buffer.str ();
        // boost::split (sizes, tmpStr, boost::is_any_of(", "), boost::token_compress_on);
        // std::cout << "tmpStr: " << tmpStr << std::endl;
        std::istringstream iss(tmpStr);
        std::string token;
        while (iss >> token) {
            sizes.push_back(token);
        }
        webIo.close ();
        for (auto flowSize : sizes) {
          ftpMaxBytes.push_back (std::stoi (flowSize));
        }
        flowNum = ftpMaxBytes.size ();
        std::cout << "flowNum=" << flowNum << std::endl;
        dscpFlowNum = uint32_t (flowNum * labelRatio);
      } 
      else {
        ftpMaxBytes.resize (flowNum);
        std::fill (ftpMaxBytes.begin (), ftpMaxBytes.end (), (appConf / 1000) << 10);
      }
      NS_ASSERT (tcpEnabled); /* these apps have to be tcp */
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {

        // uint16_t flowPriority = 0;
        // if (mainRoomQueueScheme == FQ) {
        //   flowPriority = flowIndex+1;
        // } else if (mainRoomQueueScheme == CCA) {
        //   flowPriority = ccaQueueidMapping[ccaOption];
        // } else if (mainRoomQueueScheme == RTT) {
        //   for (uint32_t i=0; i<rttBuckets.size(); i++) {
        //     double rttRightEdge = rttBuckets[i];
        //     if (rtt < rttRightEdge) {
        //       flowPriority = i+1;
        //       break;
        //     }
        //   }
        //   if (flowPriority == 0) flowPriority = rttBuckets.size() + 1;
        // } else if (mainRoomQueueScheme == SQ) {
        //   flowPriority = 1;
        // }
        uint32_t flowPriority = longBurstV3FlowPriority;

        uint16_t port = portBase + flowIndex;
        InetSocketAddress localAddress (Ipv4Address::GetAny (), port);
        // Ipv4Header::DscpType dscp = flowIndex < dscpFlowNum ? Ipv4Header::DSCP_CS2 : Ipv4Header::DscpDefault;
        // localAddress.SetTos (dscp << 2);
        InetSocketAddress remoteAddress (sinkAddr, port);
        // remoteAddress.SetTos (dscp << 2);
        BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
        ftp.SetAttribute ("Local", AddressValue (localAddress));
        ftp.SetAttribute ("Remote", AddressValue (remoteAddress));
        ftp.SetAttribute ("MaxBytes", UintegerValue (ftpMaxBytes[flowIndex]));
        ftp.SetAttribute ("TcpCongestionOps", TypeIdValue (GetCca (ccaMap[ccaOption])));
        ftp.SetAttribute("FlowId", UintegerValue(flowIdGlobal++));
        ftp.SetAttribute("priority",UintegerValue(flowPriority));
        ftp.SetAttribute("priorityCustom",UintegerValue(flowPriority));
        ApplicationContainer appFtp = ftp.Install (source);
        appFtp.Start(startTime + Seconds(appStart));
        sourceApps.Add (appFtp);
        std::cout << "appIndex=" << appIndex << ", flowIndex=" << flowIndex << ", flowId=" << flowIdGlobal-1 << ", appStart=" << appStart << ", flowPriority=" << flowPriority << std::endl;

        PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));
        sinkApps.Get(0)->SetAttribute("flowId", UintegerValue(100+flowIdGlobal+flowNum)); // AnnC: maybe they dont have enough to queue
        sinkApps.Get(0)->SetAttribute("priority",UintegerValue(0));
        sinkApps.Get(0)->SetAttribute("priorityCustom",UintegerValue(0));

        flowHash.push_back (Ipv4Hash (sourceAddr, sinkAddr, 6, port, port));
      }

      std::vector<uint64_t> flowsizelist;
      std::vector<bool> flowstartlist;
      std::vector<bool> flowendlist;
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        flowsizelist.push_back(ftpMaxBytes[flowIndex]);
        flowstartlist.push_back(true);
        flowendlist.push_back(true);
      }
      flowsizemap.insert(std::make_pair(portBase,flowsizelist));
      flowstartmap.insert(std::make_pair(portBase,flowstartlist));
      flowendmap.insert(std::make_pair(portBase,flowendlist));

      std::vector<uint64_t> gptflowsizelist;
      std::vector<bool> gptflowstartlist;
      std::vector<bool> gptflowendlist;
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        gptflowsizelist.push_back(ftpMaxBytes[flowIndex]);
        gptflowstartlist.push_back(true);
        gptflowendlist.push_back(true);
      }
      gptflowsizemap.insert(std::make_pair(portBase,flowsizelist));
      gptflowstartmap.insert(std::make_pair(portBase,flowstartlist));
      gptflowendmap.insert(std::make_pair(portBase,flowendlist));
        
      if (!light_logging) {
        std::string fctTrFileNameFull = fctTrFileName + "_app" + std::to_string(appIndex) + ".tr";
        Simulator::Schedule (Seconds (fctStatIntervalSec+appStart) + startTime, &StatFctStartEndOnly, sourceApps, portBase, flowNum, 
          ascii.CreateFileStream (fctTrFileNameFull.c_str ()));

        std::string gptTrFileNameFull = gptTrFileName + "_app" + std::to_string(appIndex) + ".tr";
        Simulator::Schedule (Seconds (fctStatIntervalSec+appStart) + startTime, &StatGoodputStartEndOnly, sourceApps, sinkApps, portBase, flowNum, 0,
          ascii.CreateFileStream (gptTrFileNameFull.c_str ()));
      } else {
        if (long_goodput_logging) {
          std::string gptTrFileNameFull = gptTrFileName + "_app" + std::to_string(appIndex) + ".tr";
          Simulator::Schedule (Seconds (fctStatIntervalSec+appStart) + startTime, &StatGoodputStartEndOnly, sourceApps, sinkApps, portBase, flowNum, 0,
            ascii.CreateFileStream (gptTrFileNameFull.c_str ()));
        }

        if (fct_logging) {
          std::string fctTrFileNameFull = fctTrFileName + "_app" + std::to_string(appIndex) + ".tr";
          Simulator::Schedule (Seconds (fctStatIntervalSec+appStart) + startTime, &StatFctStartEndOnly, sourceApps, portBase, flowNum, 
            ascii.CreateFileStream (fctTrFileNameFull.c_str ()));
        }
      }
    }
    else if (appType == "BurstV3") {
      // NS_ASSERT(tcpEnabled);
      // uint32_t flowSizeInPackets = (appConf/1000000); // (appConf/1000000)%1000;
      // uint32_t startRangeMs = ((appConf / 1000) % 1000)*10;
      // flowNum = appConf % 1000;
      flowNum = std::stoi(appConfString.substr(appConfString.length()-3));
      uint32_t startRangeMs = std::stoi(appConfString.substr(appConfString.length()-6,3))*10;
      uint32_t flowSizeInPackets = std::stoi(appConfString.substr(0,appConfString.length()-6));
      uint64_t flowSize = flowSizeInPackets * 1500;
      std::cout << "startRangeMs = " << startRangeMs << ", flowSizeInPackets = " << flowSizeInPackets << std::endl;
      std::uniform_real_distribution<double> unif(0,startRangeMs);
      // std::default_random_engine re;
      re.seed(randomSeed);
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        
        // uint16_t flowPriority = 0;
        // if (mainRoomQueueScheme == FQ) {
        //   flowPriority = flowIndex+1;
        // } else if (mainRoomQueueScheme == CCA) {
        //   flowPriority = ccaQueueidMapping[ccaOption];
        // } else if (mainRoomQueueScheme == RTT) {
        //   for (uint32_t i=0; i<rttBuckets.size(); i++) {
        //     double rttRightEdge = rttBuckets[i];
        //     if (rtt < rttRightEdge) {
        //       flowPriority = i+1;
        //       break;
        //     }
        //   }
        //   if (flowPriority == 0) flowPriority = rttBuckets.size() + 1;
        // } else if (mainRoomQueueScheme == SQ) {
        //   flowPriority = 1;
        // }
        uint32_t flowPriority = longBurstV3FlowPriority;

        uint16_t port = portBase + flowIndex;
        std::cout << "port=" << port << std::endl;
        InetSocketAddress localAddress (Ipv4Address::GetAny (), port);
        // Ipv4Header::DscpType dscp = flowIndex < dscpFlowNum ? Ipv4Header::DSCP_CS2 : Ipv4Header::DscpDefault;
        // localAddress.SetTos (dscp << 2);
        InetSocketAddress remoteAddress (sinkAddr, port);
        // remoteAddress.SetTos (dscp << 2);
        BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
        ftp.SetAttribute ("Local", AddressValue (localAddress));
        ftp.SetAttribute ("Remote", AddressValue (remoteAddress));
        ftp.SetAttribute ("MaxBytes", UintegerValue (flowSize));
        ftp.SetAttribute ("TcpCongestionOps", TypeIdValue (GetCca (ccaMap[ccaOption])));
        ftp.SetAttribute("FlowId", UintegerValue(flowIdGlobal++));
        ftp.SetAttribute("priority",UintegerValue(flowPriority));
        ftp.SetAttribute("priorityCustom",UintegerValue(flowPriority));
        ftp.SetAttribute("InitialCwnd", UintegerValue (flowSizeInPackets));
        ApplicationContainer appFtp = ftp.Install (source);
        double randomStartMs = unif(re);
        appFtp.Start(startTime + Seconds(appStart + randomStartMs/1000.));
        // appFtp.Start(startTime + Seconds(3));
        sourceApps.Add (appFtp);
        std::cout << "appIndex=" << appIndex << ", flowIndex=" << flowIndex << ", flowId=" << flowIdGlobal-1 << ", appStart=" << appStart << ", randomStartMs=" << randomStartMs << ", flowPriority=" << flowPriority << std::endl;

        PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));
        sinkApps.Get(0)->SetAttribute("flowId", UintegerValue(flowIdGlobal++)); // AnnC: maybe they dont have enough to queue
        sinkApps.Get(0)->SetAttribute("priority",UintegerValue(0));
        sinkApps.Get(0)->SetAttribute("priorityCustom",UintegerValue(0));

        flowHash.push_back (Ipv4Hash (sourceAddr, sinkAddr, 6, port, port));
      }
      
      std::vector<uint64_t> flowsizelist;
      std::vector<bool> flowstartlist;
      std::vector<bool> flowendlist;
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        flowsizelist.push_back(flowSize);
        flowstartlist.push_back(true);
        flowendlist.push_back(true);
      }
      flowsizemap.insert(std::make_pair(portBase,flowsizelist));
      flowstartmap.insert(std::make_pair(portBase,flowstartlist));
      flowendmap.insert(std::make_pair(portBase,flowendlist));

      std::vector<uint64_t> gptflowsizelist;
      std::vector<bool> gptflowstartlist;
      std::vector<bool> gptflowendlist;
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        gptflowsizelist.push_back(flowSize);
        gptflowstartlist.push_back(true);
        gptflowendlist.push_back(true);
      }
      gptflowsizemap.insert(std::make_pair(portBase,flowsizelist));
      gptflowstartmap.insert(std::make_pair(portBase,flowstartlist));
      gptflowendmap.insert(std::make_pair(portBase,flowendlist));

      if (!light_logging) {
        std::string fctTrFileNameFull = fctTrFileName + "_app" + std::to_string(appIndex) + ".tr";
        Simulator::Schedule (Seconds (fctStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatFctStartEndOnly, sourceApps, portBase, flowNum, 
          ascii.CreateFileStream (fctTrFileNameFull.c_str ()));

        std::string gptTrFileNameFull = gptTrFileName + "_app" + std::to_string(appIndex) + ".tr";
        Simulator::Schedule (Seconds (fctStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatGoodputStartEndOnly, sourceApps, sinkApps, portBase, flowNum, 0,
          ascii.CreateFileStream (gptTrFileNameFull.c_str ()));
      } else {
        if (long_goodput_logging) {
          std::string gptTrFileNameFull = gptTrFileName + "_app" + std::to_string(appIndex) + ".tr";
          Simulator::Schedule (Seconds (fctStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatGoodputStartEndOnly, sourceApps, sinkApps, portBase, flowNum, 0,
            ascii.CreateFileStream (gptTrFileNameFull.c_str ()));
        }

        if (fct_logging) {
          std::string fctTrFileNameFull = fctTrFileName + "_app" + std::to_string(appIndex) + ".tr";
          Simulator::Schedule (Seconds (fctStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatFctStartEndOnly, sourceApps, portBase, flowNum,
            ascii.CreateFileStream (fctTrFileNameFull.c_str ()));
        }
      }
    }
    else if (appType == "Long") {
      // NS_ASSERT(tcpEnabled);
      // uint64_t flowSize = 1e9;
      uint64_t flowSize = 1e10;
      uint32_t startRangeMs = ((appConf / 1000) % 1000)*10;
      std::cout << "startRangeMs = " << startRangeMs << std::endl;
      std::uniform_real_distribution<double> unif(0,startRangeMs);
      // std::default_random_engine re;
      re.seed(randomSeed);
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        
        // uint16_t flowPriority = 0;
        // if (mainRoomQueueScheme == FQ) {
        //   flowPriority = flowIndex+1;
        // } else if (mainRoomQueueScheme == CCA) {
        //   flowPriority = ccaQueueidMapping[ccaOption];
        // } else if (mainRoomQueueScheme == RTT) {
        //   for (uint32_t i=0; i<rttBuckets.size(); i++) {
        //     double rttRightEdge = rttBuckets[i];
        //     if (rtt < rttRightEdge) {
        //       flowPriority = i+1;
        //       break;
        //     }
        //   }
        //   if (flowPriority == 0) flowPriority = rttBuckets.size() + 1;
        // } else if (mainRoomQueueScheme == SQ) {
        //   flowPriority = 1;
        // }
        uint32_t flowPriority = longBurstV3FlowPriority;
         
        uint16_t port = portBase + flowIndex;
        std::cout << "port=" << port << std::endl;
        InetSocketAddress localAddress (Ipv4Address::GetAny (), port);
        // Ipv4Header::DscpType dscp = flowIndex < dscpFlowNum ? Ipv4Header::DSCP_CS2 : Ipv4Header::DscpDefault;
        // localAddress.SetTos (dscp << 2);
        InetSocketAddress remoteAddress (sinkAddr, port);
        // remoteAddress.SetTos (dscp << 2);
        BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
        ftp.SetAttribute ("Local", AddressValue (localAddress));
        ftp.SetAttribute ("Remote", AddressValue (remoteAddress));
        ftp.SetAttribute ("MaxBytes", UintegerValue (flowSize));
        ftp.SetAttribute ("TcpCongestionOps", TypeIdValue (GetCca (ccaMap[ccaOption])));
        ftp.SetAttribute("FlowId", UintegerValue(flowIdGlobal++));
        ftp.SetAttribute("priority",UintegerValue(flowPriority));
        ftp.SetAttribute("priorityCustom",UintegerValue(flowPriority));
        ApplicationContainer appFtp = ftp.Install (source);
        double randomStartMs = unif(re);
        appFtp.Start(startTime + Seconds(appStart + randomStartMs/1000.));
        sourceApps.Add (appFtp);
        std::cout << "appIndex=" << appIndex << ", flowIndex=" << flowIndex << ", flowId=" << flowIdGlobal-1 << ", appStart=" << appStart << ", randomStartMs=" << randomStartMs << ", flowPriority=" << flowPriority << std::endl;

        PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));
        sinkApps.Get(0)->SetAttribute("flowId", UintegerValue(flowIdGlobal++)); // AnnC: maybe they dont have enough to queue
        sinkApps.Get(0)->SetAttribute("priority",UintegerValue(0));
        sinkApps.Get(0)->SetAttribute("priorityCustom",UintegerValue(0));

        flowHash.push_back (Ipv4Hash (sourceAddr, sinkAddr, 6, port, port));
      }
        
      if (!light_logging) {
        std::string fctTrFileNameFull = fctTrFileName + "_app" + std::to_string(appIndex) + ".tr";
        Simulator::Schedule (Seconds (fctStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatFct, sourceApps, portBase, flowNum, 
          ascii.CreateFileStream (fctTrFileNameFull.c_str ()));

        std::string gptTrFileNameFull = gptTrFileName + "_app" + std::to_string(appIndex) + ".tr";
        Simulator::Schedule (Seconds (gptStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatGoodput, sourceApps, sinkApps, portBase, flowNum, 0,
          ascii.CreateFileStream (gptTrFileNameFull.c_str ()));
      } else {
        if (long_goodput_logging) {
          std::string gptTrFileNameFull = gptTrFileName + "_app" + std::to_string(appIndex) + ".tr";
          Simulator::Schedule (Seconds (gptStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatGoodput, sourceApps, sinkApps, portBase, flowNum, 0,
            ascii.CreateFileStream (gptTrFileNameFull.c_str ()));
        }

        if (fct_logging) {
          std::string fctTrFileNameFull = fctTrFileName + "_app" + std::to_string(appIndex) + ".tr";
          // Simulator::Schedule (Seconds (fctStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatFct, sourceApps, portBase, flowNum, 
          //   ascii.CreateFileStream (fctTrFileNameFull.c_str ()));
        }
      }
    }
    else if (appType == "Sized") {
      // NS_ASSERT(tcpEnabled);
      uint32_t startRangeMs = ((appConf / 1000) % 1000)*10;
      uint32_t flowSizeExp = (appConf/1000000)%10;
      uint32_t flowSizeMult = (appConf/10000000)%100;
      uint64_t flowSize = flowSizeMult * std::pow(10, flowSizeExp);
      std::cout << "startRangeMs=" << startRangeMs << ", flowSizeExp=" << flowSizeExp << ", flowSizeMult=" << flowSizeMult << ", flowSize=" << flowSize << std::endl;
      std::uniform_real_distribution<double> unif(0,startRangeMs);
      // std::default_random_engine re;
      re.seed(randomSeed);
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        
        // uint16_t flowPriority = 0;
        // if (mainRoomQueueScheme == FQ) {
        //   flowPriority = flowIndex+1;
        // } else if (mainRoomQueueScheme == CCA) {
        //   flowPriority = ccaQueueidMapping[ccaOption];
        // } else if (mainRoomQueueScheme == RTT) {
        //   for (uint32_t i=0; i<rttBuckets.size(); i++) {
        //     double rttRightEdge = rttBuckets[i];
        //     if (rtt < rttRightEdge) {
        //       flowPriority = i+1;
        //       break;
        //     }
        //   }
        //   if (flowPriority == 0) flowPriority = rttBuckets.size() + 1;
        // } else if (mainRoomQueueScheme == SQ) {
        //   flowPriority = 1;
        // }
        uint32_t flowPriority = longBurstV3FlowPriority;
         
        uint16_t port = portBase + flowIndex;
        std::cout << "port=" << port << std::endl;
        InetSocketAddress localAddress (Ipv4Address::GetAny (), port);
        // Ipv4Header::DscpType dscp = flowIndex < dscpFlowNum ? Ipv4Header::DSCP_CS2 : Ipv4Header::DscpDefault;
        // localAddress.SetTos (dscp << 2);
        InetSocketAddress remoteAddress (sinkAddr, port);
        // remoteAddress.SetTos (dscp << 2);
        BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
        ftp.SetAttribute ("Local", AddressValue (localAddress));
        ftp.SetAttribute ("Remote", AddressValue (remoteAddress));
        ftp.SetAttribute ("MaxBytes", UintegerValue (flowSize));
        ftp.SetAttribute ("TcpCongestionOps", TypeIdValue (GetCca (ccaMap[ccaOption])));
        ftp.SetAttribute("FlowId", UintegerValue(flowIdGlobal++));
        ftp.SetAttribute("priority",UintegerValue(flowPriority));
        ftp.SetAttribute("priorityCustom",UintegerValue(flowPriority));
        ApplicationContainer appFtp = ftp.Install (source);
        double randomStartMs = unif(re);
        appFtp.Start(startTime + Seconds(appStart + randomStartMs/1000.));
        sourceApps.Add (appFtp);
        sourceApps.Start(startTime + Seconds(appStart + randomStartMs/1000.));
        std::cout << "appIndex=" << appIndex << ", flowIndex=" << flowIndex << ", flowId=" << flowIdGlobal-1 << ", appStart=" << appStart << ", randomStartMs=" << randomStartMs << ", flowPriority=" << flowPriority << std::endl;

        PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));
        sinkApps.Get(0)->SetAttribute("flowId", UintegerValue(100+flowIdGlobal+flowNum)); // AnnC: maybe they dont have enough to queue
        sinkApps.Get(0)->SetAttribute("priority",UintegerValue(0));
        sinkApps.Get(0)->SetAttribute("priorityCustom",UintegerValue(0));

        flowHash.push_back (Ipv4Hash (sourceAddr, sinkAddr, 6, port, port));
      }

      std::vector<uint64_t> flowsizelist;
      std::vector<bool> flowstartlist;
      std::vector<bool> flowendlist;
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        flowsizelist.push_back(flowSize);
        flowstartlist.push_back(true);
        flowendlist.push_back(true);
      }
      flowsizemap.insert(std::make_pair(portBase,flowsizelist));
      flowstartmap.insert(std::make_pair(portBase,flowstartlist));
      flowendmap.insert(std::make_pair(portBase,flowendlist));

      std::vector<uint64_t> gptflowsizelist;
      std::vector<bool> gptflowstartlist;
      std::vector<bool> gptflowendlist;
      for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
        gptflowsizelist.push_back(flowSize);
        gptflowstartlist.push_back(true);
        gptflowendlist.push_back(true);
      }
      gptflowsizemap.insert(std::make_pair(portBase,flowsizelist));
      gptflowstartmap.insert(std::make_pair(portBase,flowstartlist));
      gptflowendmap.insert(std::make_pair(portBase,flowendlist));
        
      if (!light_logging) {
        std::string fctTrFileNameFull = fctTrFileName + "_app" + std::to_string(appIndex) + ".tr";
        Simulator::Schedule (Seconds (fctStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatFctStartEndOnly, sourceApps, portBase, flowNum, 
          ascii.CreateFileStream (fctTrFileNameFull.c_str ()));

        std::string gptTrFileNameFull = gptTrFileName + "_app" + std::to_string(appIndex) + ".tr";
        Simulator::Schedule (Seconds (fctStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatGoodputStartEndOnly, sourceApps, sinkApps, portBase, flowNum, 0,
          ascii.CreateFileStream (gptTrFileNameFull.c_str ()));
      } else {
        if (long_goodput_logging) {
          std::string gptTrFileNameFull = gptTrFileName + "_app" + std::to_string(appIndex) + ".tr";
          Simulator::Schedule (Seconds (gptStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatGoodputStartEndOnly, sourceApps, sinkApps, portBase, flowNum, 0,
            ascii.CreateFileStream (gptTrFileNameFull.c_str ()));
        }

        if (fct_logging) {
          std::string fctTrFileNameFull = fctTrFileName + "_app" + std::to_string(appIndex) + ".tr";
          Simulator::Schedule (Seconds (fctStatIntervalSec+appStart+startRangeMs/1000.) + startTime, &StatFctStartEndOnly, sourceApps, portBase, flowNum, 
            ascii.CreateFileStream (fctTrFileNameFull.c_str ()));
        }
      }
    }
    else if (appType == "UDP") {
      // AnnC: cannot do udp at the moment. our probing asks for flowid
      // error message: "Attribute name=flowId does not exist for this object: tid=ns3::UdpSocketImpl"

      // NS_ASSERT (!tcpEnabled);  /* these apps have to be udp */
      // Time interPacketInterval = Seconds (0.05);
      // uint32_t maxPacketCount = 320;
      // for (uint16_t flowIndex = 0; flowIndex < flowNum; flowIndex++) {
      //   uint16_t port = portBase + flowIndex;

      //   InetSocketAddress localAddress (Ipv4Address::GetAny (), port);
      //   Ipv4Header::DscpType dscp = flowIndex < dscpFlowNum ? Ipv4Header::DSCP_CS2 : Ipv4Header::DscpDefault;
      //   localAddress.SetTos (dscp << 2);
      //   InetSocketAddress remoteAddress (sinkAddr, port);
      //   remoteAddress.SetTos (dscp << 2);

      //   UdpClientHelper client (Address (remoteAddress), port);
      //   // client.SetAttribute ("Local", AddressValue (localAddress));
      //   // client.SetAttribute ("Remote", AddressValue (remoteAddress));
      //   client.SetAttribute ("MaxPackets", UintegerValue (maxPacketCount));
      //   client.SetAttribute ("Interval", TimeValue (interPacketInterval));
      //   client.SetAttribute ("PacketSize", UintegerValue (1472));
      //   ApplicationContainer appUdp = client.Install (source);
      //   appUdp.Start(startTime + Seconds(appStart));
      //   sourceApps.Add (appUdp);

      //   PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", Address (localAddress));
      //   sinkApps.Add (sinkHelper.Install (sink));
      //   flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 17, port, port));
      // }
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
        ApplicationContainer appOnOff = onOff.Install (source);
        appOnOff.Start(startTime + Seconds(appStart));
        sourceApps.Add (appOnOff);

        PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));
        flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 17, port, port));
      }
    }
    else if (appType == "ConOnOff") {
      uint32_t cbrRateMbps = (appConf / 1000) % 100;
      uint32_t burstIntervalMs = appConf / 1000000;
      uint32_t numBursts = (appConf / 100000) % 10;
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
        ApplicationContainer appOnOff = onOff.Install (source);
        appOnOff.Start(startTime + Seconds(appStart));
        appOnOff.Stop(startTime + Seconds(appStart + (numBursts*burstIntervalMs)/1000.));
        sourceApps.Add (appOnOff);

        PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));
        flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 17, port, port));
      }
    }
    else if (appType == "Burst") {
      uint32_t cbrRateMbps = appConf / 10000000;
      uint32_t burstIntervalMs = 400;
      uint32_t burstDurationMs = (appConf / 1000) % 10000;
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
        onOff.SetAttribute ("DataRate", DataRateValue ((cbrRateMbps << 20) * burstIntervalMs));
        onOff.SetAttribute ("PacketSize", UintegerValue (1472));
        onOff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.001]"));
        onOff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=" + std::to_string (double (burstIntervalMs - 1) / 1000.) + "]"));
        
        ApplicationContainer appOnOff = onOff.Install (source);
        appOnOff.Start(startTime + Seconds(appStart));
        appOnOff.Stop(startTime + Seconds(appStart + burstDurationMs/1000.));
        sourceApps.Add (appOnOff);

        PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));
        flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 17, port, port));
      }
    }
    else if (appType == "BurstV2") {
      uint32_t cbrRateMbps = 1;
      uint32_t burstIntervalMs = 400;
      uint32_t burstDurationMs = ((appConf / 1000) % 1000) * 10;
      uint32_t burstStartRangeMs = (appConf / 1000000) * 10;
      std::cout << "burstDurationMs=" << burstDurationMs << ", burstStartRangeMs=" << burstStartRangeMs << std::endl;
      std::uniform_real_distribution<double> unif(0,burstStartRangeMs);
      // std::default_random_engine re;
      re.seed(randomSeed);
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
        onOff.SetAttribute ("DataRate", DataRateValue ((cbrRateMbps << 20) * burstIntervalMs));
        onOff.SetAttribute ("PacketSize", UintegerValue (1472));
        onOff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.001]"));
        onOff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=" + std::to_string (double (burstIntervalMs - 1) / 1000.) + "]"));
        
        ApplicationContainer appOnOff = onOff.Install (source);
        double randomStartMs = unif(re);
        appOnOff.Start(startTime + Seconds(appStart + randomStartMs/1000.));
        appOnOff.Stop(startTime + Seconds(appStart + randomStartMs/1000. + burstDurationMs/1000.));
        sourceApps.Add (appOnOff);

        PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", Address (localAddress));
        sinkApps.Add (sinkHelper.Install (sink));
        flowHash.push_back(Ipv4Hash(sourceAddr, sinkAddr, 17, port, port));
      }
    }
    else {
      NS_ABORT_MSG ("Unknown application type: " << appType);
    }

    if (appType != "Long" && appType != "OnOff" && appType != "ConOnOff" && appType != "Burst" && appType != "BurstV2" && appType != "BurstV3" && appType != "Web" && appType != "Sized" && appType != "UDP") {
      sourceApps.Start (startTime);
    }
    if (appType != "ConOnOff" && appType != "Burst" && appType != "BurstV2") {
      sourceApps.Stop (Seconds (stopTime - 2));
    }
    sinkApps.Start (startTime);
    sinkApps.Stop (Seconds (stopTime));
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
  if (!light_logging) {
    Simulator::Schedule (readNewLine ? remainInterval : minSetInterval, &BandwidthTrace, node0, node1, trace, 
      change, interval, readNewLine);
  }
}


// void ResetQueueSize (Ptr<QueueDisc> q, uint32_t newQueueSize) {
//   q->SetMaxSize (QueueSize (QueueSizeUnit::PACKETS, newQueueSize));
// }

double MbpsStringToGbpsDouble(std::string bandwidthMbps) {
  std::string bandwidthValue = bandwidthMbps.substr(0,bandwidthMbps.length()-4);
  double bandwidth = std::stold(bandwidthValue);
  return bandwidth/1000.0;
}

std::string toStringNoTrailingZeros(const std::string& str) {
    size_t dotPos = str.find_last_not_of('0') + 1;
    if (dotPos != std::string::npos && str[dotPos-1] == '.') {
        // Remove trailing dot as well
        dotPos--;
    }
    return str.substr(0, dotPos);
}

void InvokeToRStats(Ptr<OutputStreamWrapper> stream, uint32_t BufferSize, uint32_t nPrior, uint32_t bufferAlgorithm){
	double nanodelay = statIntervalSec*1e9;
  int64_t currentNanoSeconds = Simulator::Now().GetNanoSeconds();

	Ptr<SharedMemoryBuffer> pbuffer = sharedMemory;
	QueueDiscContainer pqueues = bottleneckQueueDiscsCollection;

	*stream->GetStream() << currentNanoSeconds << " " << double(BufferSize)/1e6 << " " << 100 * double(pbuffer->GetOccupiedBuffer())/BufferSize;
	for (uint32_t port = 0; port<pqueues.GetN(); port++) {
		Ptr<GenQueueDisc> genDisc = DynamicCast<GenQueueDisc>(pqueues.Get(port));
		double remaining = genDisc->GetRemainingBuffer();
		for (uint32_t priority = 0; priority<nPrior; priority++)  {
			uint32_t qSize = genDisc->GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes();
			// double th = genDisc->GetThroughputQueue(priority, nanodelay).first;
      // double sentBytes = genDisc->GetThroughputQueue(priority, nanodelay).second; 
      // auto [th, sentBytes] = genDisc->GetThroughputQueue(priority, nanodelay);
      std::vector<double> thvec = genDisc->GetThroughputQueue(priority, nanodelay);
      double th = thvec.at(0);
      double sentBytes = thvec.at(1);
			uint64_t droppedBytes = genDisc->GetDroppedBytes(priority);
			uint64_t maxSize = 0;
      if (bufferAlgorithm == DT) {
        maxSize = genDisc->GetAlpha(priority)*remaining;
      } else if (bufferAlgorithm == MY) {
        uint32_t proberId = sharedMemory->getProberId(port, priority);
        maxSize = sharedMemory->getCurrMaxSizeAllowed(proberId);
      } else {
        std::cout << "InvokeToRStats has not implemented for bufferAlgorithm " << bufferAlgorithm << std::endl;
      }
			*stream->GetStream() << " " << qSize << " " << th << " " << sentBytes << " " << droppedBytes << " " << maxSize;
		}
	}
	*stream->GetStream() << std::endl;

	Simulator::Schedule(Seconds(statIntervalSec), InvokeToRStats, stream, BufferSize, nPrior, bufferAlgorithm);
}

uint64_t codelDropAfterDequeue = 0;
void InvokeToRStatsSinkOnly(Ptr<OutputStreamWrapper> stream, uint32_t BufferSize, uint32_t nPrior, uint32_t bufferAlgorithm, uint32_t numSinks, std::string queueDiscType){
	double nanodelay = statIntervalSec*1e9;
  int64_t currentNanoSeconds = Simulator::Now().GetNanoSeconds();

	Ptr<SharedMemoryBuffer> pbuffer = sharedMemory;
	QueueDiscContainer pqueues = bottleneckQueueDiscsCollection;

  // if (queueDiscType.compare("CoDel")==0) {
  //   // AnnC: warning! only work for CoDel with a single queue and a single port
  //   assert(nPrior==2);
  //   assert(numSinks==1);
  //   uint32_t numpqueues = pqueues.GetN();
  //   uint32_t currQSize = DynamicCast<GenQueueDisc>(pqueues.Get(numpqueues-1))->GetQueueDiscClass(1)->GetQueueDisc()->GetNBytes();
  //   uint64_t totalCodelDropAfterDequeue = pbuffer->GetOccupiedBuffer()-currQSize;
  //   uint64_t thisCodelDropAfterDequeue = 0;
  //   if (totalCodelDropAfterDequeue > codelDropAfterDequeue) {
  //     thisCodelDropAfterDequeue = totalCodelDropAfterDequeue-codelDropAfterDequeue;
  //     codelDropAfterDequeue = totalCodelDropAfterDequeue;
  //   }
  //   *stream->GetStream() << currentNanoSeconds << " " << double(BufferSize)/1e6 << " " << 100 * double(currQSize)/BufferSize;
  //   for (uint32_t port = numpqueues-numSinks; port<numpqueues; port++) {
  //     Ptr<GenQueueDisc> genDisc = DynamicCast<GenQueueDisc>(pqueues.Get(port));
  //     double remaining = genDisc->GetRemainingBuffer();
  //     for (uint32_t priority = 0; priority<nPrior; priority++)  {
  //       uint32_t qSize = genDisc->GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes();
  //       // double th = genDisc->GetThroughputQueue(priority, nanodelay).first;
  //       // double sentBytes = genDisc->GetThroughputQueue(priority, nanodelay).second; 
  //       // auto [th, sentBytes] = genDisc->GetThroughputQueue(priority, nanodelay);
  //       std::vector<double> thvec = genDisc->GetThroughputQueue(priority, nanodelay);
  //       double th = thvec.at(0);
  //       double sentBytes = thvec.at(1);
  //       uint64_t droppedBytes = genDisc->GetDroppedBytes(priority);
  //       if (priority==1) droppedBytes += thisCodelDropAfterDequeue;
  //       uint64_t maxSize = 0;
  //       if (bufferAlgorithm == DT) {
  //         maxSize = genDisc->GetAlpha(priority)*remaining;
  //       } else if (bufferAlgorithm >= MY) {
  //         uint32_t proberId = sharedMemory->getProberId(port, priority);
  //         maxSize = sharedMemory->getCurrMaxSizeAllowed(proberId);
  //       } else {
  //         std::cout << "InvokeToRStatsSinkOnly has not implemented for bufferAlgorithm " << bufferAlgorithm << std::endl;
  //       }
  //       *stream->GetStream() << " " << qSize << " " << th << " " << sentBytes << " " << droppedBytes << " " << maxSize;
  //     }
  //   }
  //   *stream->GetStream() << std::endl;
  // }
  // else {
    *stream->GetStream() << currentNanoSeconds << " " << double(BufferSize)/1e6 << " " << 100 * double(pbuffer->GetOccupiedBuffer())/BufferSize;
    uint32_t numpqueues = pqueues.GetN();
    for (uint32_t port = numpqueues-numSinks; port<numpqueues; port++) {
      Ptr<GenQueueDisc> genDisc = DynamicCast<GenQueueDisc>(pqueues.Get(port));
      double remaining = genDisc->GetRemainingBuffer();
      for (uint32_t priority = 0; priority<nPrior; priority++)  {
        uint32_t qSize = genDisc->GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes();
        // double th = genDisc->GetThroughputQueue(priority, nanodelay).first;
        // double sentBytes = genDisc->GetThroughputQueue(priority, nanodelay).second; 
        // auto [th, sentBytes] = genDisc->GetThroughputQueue(priority, nanodelay);
        std::vector<double> thvec = genDisc->GetThroughputQueue(priority, nanodelay);
        double th = thvec.at(0);
        double sentBytes = thvec.at(1);
        uint64_t droppedBytes = genDisc->GetDroppedBytes(priority);
        uint64_t maxSize = 0;
        if (bufferAlgorithm == DT) {
          maxSize = genDisc->GetAlpha(priority)*remaining;
        } else if (bufferAlgorithm >= MY) {
          uint32_t proberId = sharedMemory->getProberId(port, priority);
          maxSize = sharedMemory->getCurrMaxSizeAllowed(proberId);
        } else {
          std::cout << "InvokeToRStatsSinkOnly has not implemented for bufferAlgorithm " << bufferAlgorithm << std::endl;
        }
        *stream->GetStream() << " " << qSize << " " << th << " " << sentBytes << " " << droppedBytes << " " << maxSize;
      }
    }
    *stream->GetStream() << std::endl;
  // }

	Simulator::Schedule(Seconds(statIntervalSec), InvokeToRStatsSinkOnly, stream, BufferSize, nPrior, bufferAlgorithm, numSinks, queueDiscType);
}

void DeltaBandwidth(Ptr<Node> node, float dbwSecond, std::string dbwMbps, uint32_t dbwTargetBW, uint32_t numSinks) {
  for (uint32_t sink=0; sink<numSinks; sink++) {
    Ptr<PointToPointNetDevice> nodeDevice = StaticCast<PointToPointNetDevice,NetDevice> (node->GetDevice(node->GetNDevices()-1-sink));
    nodeDevice->SetAttribute ("DataRate", StringValue (dbwMbps+"Mbps"));
    Ptr<GenQueueDisc> genDisc = DynamicCast<GenQueueDisc> (outputQueueDiscsCollection.Get(sink));
    genDisc->setPortBw(MbpsStringToGbpsDouble(dbwMbps+"Mbps"));
    genDisc->setTargetBw(dbwTargetBW);
  }
}

Ptr<OutputStreamWrapper> torStats;
AsciiTraceHelper torTraceHelper;

// double alpha_values[8]={1};

int main (int argc, char *argv[]) {
  uint32_t queueDiscSize = 1000;

  std::string trace = "";
  std::string queueDiscType = "Fifo";  // "FqCoDel";       //PfifoFast or CoDel
  // std::string ccaType = "0";
  float startTime = 0.1f;
  float simDuration = 300;         //in seconds

  int traceInterval = 200;  // in milliseconds

  bool isPcapEnabled = false;
  bool logging = false;
  std::string dir = "logs/";

  std::string appConfigFile = "../configurations/";
  std::string deltaBwConfigFile = "";
  std::string fixedVaryConfigFile = "";

  // std::string appAType = "";
  // std::string appBType = "";
  // std::string appCType = "";
  // std::string appDType = "";
  // std::string appEType = "";
  // uint32_t appAConf = 0;
  // uint32_t appBConf = 0;
  // uint32_t appCConf = 0;
  // uint32_t appDConf = 0;
  // uint32_t appEConf = 0;
  // uint32_t appASink = 0;
  // uint32_t appBSink = 0;
  // uint32_t appCSink = 0;
  // uint32_t appDSink = 0;
  // uint32_t appESink = 0;
  // float appAStart = 0;
  // float appBStart = 0;
  // float appCStart = 0;
  // float appDStart = 0;
  // float appEStart = 0;

  uint32_t resetQueueDisc = 0;

  uint32_t hop = 2;

  double_t sockBufDutyRatio = 0.9f;

  uint32_t bufferSize = 25000; // in bytes
  uint32_t bufferAlgorithm = MY;
  uint32_t burstReserve = 0;

  uint32_t startProbeBuffer = 1500;

  // uint16_t headRoomQueueScheme = FQ;
  uint16_t mainRoomQueueScheme = SQ;
  // uint32_t headRoomNumQueues = 0;
  uint32_t mainRoomNumQueues = 1;

    // alpha values
  // double sinkAalpha = 1;
  // double sinkBalpha = 1;
  // alpha_values[0] = 8; //for ACK packets
	// alpha_values[0] = sinkAalpha;
	// alpha_values[1] = sinkBalpha;

  uint32_t randomSeed = 1;

  uint32_t diffServ = 0;

  CommandLine cmd (__FILE__);
  cmd.AddValue ("qdiscSize", "The size of the queue discipline, in the unit of packet", queueDiscSize);
  cmd.AddValue ("trace", "Trace file to simulate", trace);
  cmd.AddValue ("queueDiscType", "Bottleneck queue disc type: PfifoFast, CoDel", queueDiscType);
  // cmd.AddValue ("ccaType", "Congestion control algorithm used by the sender", ccaType);
  // cmd.AddValue ("diffServ", "Using DSCP markings for DiffServ or not.", diffServ);
  cmd.AddValue ("simDuration", "Simulation duration in seconds", simDuration);
  cmd.AddValue ("isPcapEnabled", "Flag to enable/disable pcap", isPcapEnabled);
  cmd.AddValue ("logging", "Flag to enable/disable logging", logging);
  cmd.AddValue ("logdir", "The base output directory for all results", dir);

  cmd.AddValue ("sockBufDutyRatio", "The duty ratio of the socket buffer", sockBufDutyRatio);

  // cmd.AddValue ("appAType", "Application A type", appAType);
  // cmd.AddValue ("appAConf", "Application A configuration", appAConf);
  // cmd.AddValue ("appASink", "Application A sink", appASink);
  // cmd.AddValue ("appAStart", "Application A start time in second", appAStart);
  // cmd.AddValue ("appBType", "Application B type", appBType);
  // cmd.AddValue ("appBConf", "Application B configuration", appBConf);
  // cmd.AddValue ("appBSink", "Application B sink", appBSink);
  // cmd.AddValue ("appBStart", "Application B start time in second", appBStart);
  // cmd.AddValue ("appCType", "Application C type", appCType);
  // cmd.AddValue ("appCConf", "Application C configuration", appCConf);
  // cmd.AddValue ("appCSink", "Application C sink", appCSink);
  // cmd.AddValue ("appCStart", "Application C start time in second", appCStart);
  // cmd.AddValue ("appDType", "Application D type", appDType);
  // cmd.AddValue ("appDConf", "Application D configuration", appDConf);
  // cmd.AddValue ("appDSink", "Application D sink", appDSink);
  // cmd.AddValue ("appDStart", "Application D start time in second", appDStart);
  // cmd.AddValue ("appEType", "Application E type", appEType);
  // cmd.AddValue ("appEConf", "Application E configuration", appEConf);
  // cmd.AddValue ("appESink", "Application E sink", appESink);
  // cmd.AddValue ("appEStart", "Application E start time in second", appEStart);

  cmd.AddValue ("autoDecayingFunc", "the function of decaying in AutoQueueDisc", autoDecayingFunc);
  cmd.AddValue ("autoDecayingCoef", "the coefficient of decaying in AutoQueueDisc", autoDecayingCoef);
  cmd.AddValue ("dwrrPrioRatio", "DWRR ratio between different classes", dwrrPrioRatio);
  cmd.AddValue ("codelTarget", "Target value of dropping packets in CoDel", codelTarget);
  cmd.AddValue ("codelInterval", "Interval value of dropping packets in CoDel", codelInterval);
  cmd.AddValue ("pieTarget", "QueueDelayReference value in Pie", pieTarget);
  cmd.AddValue ("tbfRate", "", tbfRate);
  cmd.AddValue ("tbfBurst", "", tbfBurst);

  cmd.AddValue ("resetQueueDisc", "Reset queue disc; only in cca-cut exps", resetQueueDisc);

  // cmd.AddValue ("hop", "Number of hops in the line topology; only in different btlbw exps", hop);

  cmd.AddValue ("bufferSize", "Buffer size in bytes", bufferSize);
  cmd.AddValue ("bufferAlgorithm", "Buffer management algorithm", bufferAlgorithm);
  // cmd.AddValue ("sinkAalpha", "alpha value for sinkA (for DT 101)", sinkAalpha);
  // cmd.AddValue ("sinkBalpha", "alpha value for sinkB (for DT 101)", sinkBalpha);
  cmd.AddValue ("burstReserve", "The amount of buffer reserved for future burst (for MY 111)", burstReserve);

  cmd.AddValue ("startProbeBuffer", "The amount of buffer (in bytes) given when we first start probing; this is a temporary variable", startProbeBuffer);

  cmd.AddValue ("randomSeed", "random seed", randomSeed);

  cmd.AddValue ("appConfigFile", "A configuration file for app setup", appConfigFile);
  cmd.AddValue ("deltaBwConfigFile", "A configuration file for bandwidth change", deltaBwConfigFile);
  cmd.AddValue ("fixedVaryConfigFile", "A configuration file for threshold under fixed_vary pawmode", fixedVaryConfigFile);

  uint32_t nPrior = 2;
  // cmd.AddValue ("headRoomQueueScheme", "HeadRoom Queue Scheme", headRoomQueueScheme);
  cmd.AddValue ("mainRoomQueueScheme", "MainRoom Queue Scheme", mainRoomQueueScheme);
  cmd.AddValue ("nPrior", "The number of queues at each GenQueueDisc (need to be at least 2, since priority 0 is dedicated to short control packets)", nPrior);
  // cmd.AddValue ("headRoomNumQueues", "Number of queues in HeadRoom", headRoomNumQueues);
  cmd.AddValue ("mainRoomNumQueues", "Number of queues in MainRoom", mainRoomNumQueues);

  // uint32_t aSrcBw = 1000;
  // uint32_t aMidBw = 10;
  // uint32_t bSrcBw = 1000;
  // uint32_t bMidBw = 10;

  // // std::string aSrcDelay = "0.1";
  // std::string aMidDelay = "20";
  // // std::string bSrcDelay = "0.1";
  // std::string bMidDelay = "20";

  // // cmd.AddValue ("dstBandwidth", "Bottleneck bandwidth", dstBandwidth);
  // // cmd.AddValue ("dstDelay", "Bottleneck delay", dstDelay);
  // cmd.AddValue ("aSrcBw", "Access link bandwidth for sender A (in Mbps)", aSrcBw);
  // // cmd.AddValue ("aSrcDelay", "Access link delay for sender A (string in ms)", aSrcDelay);
  // cmd.AddValue ("aMidBw", "Access link bandwidth for sender A (in Mbps)", aMidBw);
  // cmd.AddValue ("aMidDelay", "Access link delay for sender A (string in ms)", aMidDelay);
  // cmd.AddValue ("bSrcBw", "Access link bandwidth for sender B (in Mbps)", bSrcBw);
  // // cmd.AddValue ("bSrcDelay", "Access link delay for sender B (string in ms)", bSrcDelay);
  // cmd.AddValue ("bMidBw", "Access link bandwidth for sender B (in Mbps)", bMidBw);
  // cmd.AddValue ("bMidDelay", "Access link delay for sender B (string in ms)", bMidDelay);

  uint32_t numSinks = 2;
  cmd.AddValue ("numSinks", "Number of sinks", numSinks);

  std::string alphaString = "";
  cmd.AddValue ("alphaString", "alpha values for each output port (for DT 101)", alphaString);

  std::string srcBwString = "";
  std::string midBwString = "";
  std::string midDelayString = "";
  cmd.AddValue ("srcBwString", "A string indicating srcBw for senders going to each output port, eg, '10_20_30'", srcBwString);
  cmd.AddValue ("midBwString", "A string indicating midBw for each output port, eg, '10_20_30'", midBwString);
  cmd.AddValue ("midDelayString", "A string indicating midDelay for each output port, eg, '10_20_30'", midDelayString);

  std::string rttTag = "0";
  cmd.AddValue ("rttTag", "RTT tag for fdir", rttTag);

  std::string rttBucketsString = "";
  cmd.AddValue ("rttBucketsString", "A string indicating how to divide RTT buckets, eg, '10,20,30'", rttBucketsString);

  uint16_t monitorlongms = 5000;
  double dropRateThreshold = 1;
  uint32_t targetBW = 0;
  cmd.AddValue ("monitorInterval", "Monitoring interval in ms", monitorlongms);
  cmd.AddValue ("dropRateThreshold", "Monitoring interval in ms", dropRateThreshold);
  cmd.AddValue ("targetBW", "Target bandwidth, according to monitoring interval", targetBW);

  // double adaptiveIncreaseParameter = 4;
  // double adaptiveDecreaseParameter = 2;
  std::string adaptiveIncreaseParameterString = "2";
  std::string adaptiveDecreaseParameterString = "4";
  cmd.AddValue ("adaptiveIncreaseParameterString", "Step when increase: +drop/adaptiveIncreaseParameter (for senders going to each output port)", adaptiveIncreaseParameterString);
  cmd.AddValue ("adaptiveDecreaseParameterString", "Step when cdcrease: -minbuffer/adaptiveDecreaseParameter (for senders going to each output port)", adaptiveDecreaseParameterString);

  uint32_t smoothQlenCollectionByUs = 1000;
  uint32_t smoothOutlierThresholdByMultiple = 100000;
  uint32_t smoothWindowByNumData = 500;
  cmd.AddValue ("smoothQlenCollection", "At what frequency we collect queue length data points (in us)", smoothQlenCollectionByUs);
  cmd.AddValue ("smoothWindow", "How many queue length data points in a window", smoothWindowByNumData);
  cmd.AddValue ("smoothOutlierThreshold", "How many times I need to be higher than mean to be considered an outlier", smoothOutlierThresholdByMultiple);

  std::string pawMode = "paw";
  cmd.AddValue ("pawMode", "paw,pa,aw,fixed", pawMode);

  uint16_t ParHistLen = 5;
  uint16_t ParRemoveStartLen = 10;
  uint16_t ParRemoveStartThres = 50;
  uint16_t ParExploreThres = 3;
  uint16_t ParSafeThres = 3;
  uint16_t ParConsecIncreaseThres = 3;
  uint16_t ParStepIncreaseCap = 5;
  uint16_t ParIncreaseRatio = 10;
  uint16_t ParConsecDecreaseThres = 3;
  uint16_t ParStepDecreaseCap = 5;
  uint16_t ParDecreaseRatio = 1;
  uint16_t ParMinQOutlier = 10;
  uint16_t ParMinQHold = 5;
  cmd.AddValue ("ParHistLen", "", ParHistLen);
  cmd.AddValue ("ParRemoveStartLen", "", ParRemoveStartLen);
  cmd.AddValue ("ParRemoveStartThres", "", ParRemoveStartThres);
  cmd.AddValue ("ParSafeThres", "", ParSafeThres);
  cmd.AddValue ("ParConsecIncreaseThres", "", ParConsecIncreaseThres);
  cmd.AddValue ("ParStepIncreaseCap", "", ParStepIncreaseCap);
  cmd.AddValue ("ParConsecDecreaseThres", "", ParConsecDecreaseThres);
  cmd.AddValue ("ParStepDecreaseCap", "", ParStepDecreaseCap);
  cmd.AddValue ("ParMinQOutlier", "", ParMinQOutlier);
  cmd.AddValue ("ParMinQHold", "", ParMinQHold);

  cmd.AddValue ("ParIncreaseRatio", "", ParIncreaseRatio);
  cmd.AddValue ("ParDecreaseRatio", "", ParDecreaseRatio);
  cmd.AddValue ("ParExploreThres", "", ParExploreThres);

  cmd.Parse (argc, argv);

  // uint32_t nPrior = headRoomNumQueues + mainRoomNumQueues + 1;
  // uint32_t nPrior = mainRoomNumQueues + 1;

  // Parse rttBucketsString
  if (rttBucketsString!="") {
    std::string delimiter = "_";
    size_t pos = 0;
    std::string token;
    while ((pos = rttBucketsString.find(delimiter)) != std::string::npos) {
        token = rttBucketsString.substr(0, pos);
        rttBuckets.push_back(std::stod(token));
        rttBucketsString.erase(0, pos + delimiter.length());
    }
    rttBuckets.push_back(std::stod(rttBucketsString));
  }
  std::cout << "RTT buckets: ";
  for (uint32_t i=0; i<rttBuckets.size(); i++) {
    std::cout << rttBuckets[i] << ", ";
  }
  std::cout << std::endl;
  std::cout << "targetBW: " << targetBW << std::endl;
  
  // std::cout << "***Debug: aSrcBw=" << aSrcBw << ", aMidBw=" << aMidBw << std::endl;

  // std::string dstBandwidth = "1000Mbps";
  // std::string dstDelay = "0.1ms";
  // AnnC: have to be after cmd.Parse()
  // std::string senderAsrcBandwidth = std::to_string(aSrcBw) + "Mbps";
  // // std::string senderAsrcDelay = aSrcDelay + "ms";
  // std::string senderAmidBandwidth = std::to_string(aMidBw) + "Mbps";
  // std::string senderAmidDelay = aMidDelay + "ms";
  // std::string senderBsrcBandwidth = std::to_string(bSrcBw) + "Mbps";
  // // std::string senderBsrcDelay = bSrcDelay + "ms";
  // std::string senderBmidBandwidth = std::to_string(bMidBw) + "Mbps";
  // std::string senderBmidDelay = bMidDelay + "ms";
  
  // std::string conf = ccaType;

  // parse ccaType
  // std::vector<std::string> ccaTypeVector;
  // std::string delimiter = "_";
  // size_t pos = 0;
  // std::string token;
  // while ((pos = ccaType.find(delimiter)) != std::string::npos) {
  //   token = ccaType.substr(0, pos);
  //   ccaTypeVector.push_back(token);
  //   ccaType.erase(0, pos + delimiter.length());
  // }
  // ccaTypeVector.push_back(ccaType);
  // std::cout << ccaTypeVector.size() <<  std::endl;
  // std::string ccaTypeSink1 = ccaTypeVector.at(0);
  // std::cout << "ccaTypeSink1=" << ccaTypeSink1 <<  std::endl;
  // std::string ccaTypeSink2;
  // if (ccaTypeVector.size() == 2) {
  //   ccaTypeSink2 = ccaTypeVector.at(1);
  //   std::cout << "ccaTypeSink2=" << ccaTypeSink2 <<  std::endl;
  // }

  // parse application settings
  // std::vector < std::vector<std::string> > appSettingsSink1, appSettingsSink2;
  // std::vector<std::string> tempAppAHolder,tempAppBHolder,tempAppCHolder,tempAppDHolder,tempAppEHolder;
  // if (appAType != "") {
  //   conf += "_" + std::to_string(appASink) + "_" + appAType + std::to_string(appAConf);
  //   tempAppAHolder.push_back(appAType);
  //   tempAppAHolder.push_back(std::to_string(appAConf));
  //   tempAppAHolder.push_back(std::to_string(appAStart));
  // }
  // else {
  //   tempAppAHolder.push_back("");
  //   tempAppAHolder.push_back(std::to_string(0));
  //   tempAppAHolder.push_back(std::to_string(0.));
  // }
  // if (appASink == 1) {
  //   appSettingsSink1.push_back(tempAppAHolder);
  // } else if (appASink == 2) {
  //   appSettingsSink2.push_back(tempAppAHolder);
  // }

  // if (appBType != "") {
  //   conf += "_" + std::to_string(appBSink) + "_" + appBType + std::to_string(appBConf);
  //   tempAppBHolder.push_back(appBType);
  //   tempAppBHolder.push_back(std::to_string(appBConf));
  //   tempAppBHolder.push_back(std::to_string(appBStart));
  // }
  // else {
  //   tempAppBHolder.push_back("");
  //   tempAppBHolder.push_back(std::to_string(0));
  //   tempAppBHolder.push_back(std::to_string(0.));
  // }
  // if (appBSink == 1) {
  //   appSettingsSink1.push_back(tempAppBHolder);
  // } else if (appBSink == 2) {
  //   appSettingsSink2.push_back(tempAppBHolder); 
  // }

  // if (appCType != "") {
  //   conf += "_" + std::to_string(appCSink) + "_" + appCType + std::to_string(appCConf);
  //   tempAppCHolder.push_back(appCType);
  //   tempAppCHolder.push_back(std::to_string(appCConf));
  //   tempAppCHolder.push_back(std::to_string(appCStart));
  // }
  // else {
  //   tempAppCHolder.push_back("");
  //   tempAppCHolder.push_back(std::to_string(0));
  //   tempAppCHolder.push_back(std::to_string(0.));
  // }
  // if (appCSink == 1) {
  //   appSettingsSink1.push_back(tempAppCHolder);
  // } else if (appCSink == 2) {
  //   appSettingsSink2.push_back(tempAppCHolder);
  // }
  
  // if (appDType != "") {
  //   conf += "_" + std::to_string(appDSink) + "_" + appDType + std::to_string(appDConf);
  //   tempAppDHolder.push_back(appDType);
  //   tempAppDHolder.push_back(std::to_string(appDConf));
  //   tempAppDHolder.push_back(std::to_string(appDStart));
  // }
  // else {
  //   tempAppDHolder.push_back("");
  //   tempAppDHolder.push_back(std::to_string(0));
  //   tempAppDHolder.push_back(std::to_string(0.));
  // }
  // if (appDSink == 1) {
  //   appSettingsSink1.push_back(tempAppDHolder);
  // } else if (appDSink == 2) {
  //   appSettingsSink2.push_back(tempAppDHolder);
  // }
  
  // if (appEType != "") {
  //   conf += "_" + std::to_string(appESink) + "_" + appEType + std::to_string(appEConf);
  //   tempAppEHolder.push_back(appEType);
  //   tempAppEHolder.push_back(std::to_string(appEConf));
  //   tempAppEHolder.push_back(std::to_string(appEStart));
  // }
  // else {
  //   tempAppEHolder.push_back("");
  //   tempAppEHolder.push_back(std::to_string(0));
  //   tempAppEHolder.push_back(std::to_string(0.));
  // }
  // if (appESink == 1) {
  //   appSettingsSink1.push_back(tempAppEHolder);
  // } else if (appESink == 2) {
  //   appSettingsSink2.push_back(tempAppEHolder);
  // }

  srand(randomSeed);

  // uint32_t numSendersA = 1;
  // uint32_t numSendersB = 1;

  // std::string ccaTypeSink1, ccaTypeSink2;
  size_t last_slash_position = appConfigFile.find_last_of("/");
  size_t last_dot_position = appConfigFile.find_last_of(".");
  std::string configurationsfilename = appConfigFile.substr(last_slash_position+1, last_dot_position-last_slash_position-1);
  std::cout << "configurationsfilename = " << configurationsfilename << std::endl;
  std::string conf = configurationsfilename + "_" + pawMode;

  std::regex regex(".*/([^/]+)\\.conf$");
  std::smatch match;
  if (deltaBwConfigFile.compare("")!=0) {
    std::regex_match(deltaBwConfigFile, match, regex);
    conf += "_" + (std::string)(match[1]);
    std::cout << "current deltabw conf: " << conf << std::endl;
  }

  if (fixedVaryConfigFile.compare("")!=0) {
    std::regex_match(fixedVaryConfigFile, match, regex);
    conf += "_" + (std::string)(match[1]);
    std::cout << "current fixedvary conf: " << conf << std::endl;
  }

  if (queueDiscType.compare("CoDel")==0) {
    conf += "/codel_" + codelTarget + "_" + codelInterval;
  }
  if (queueDiscType.compare("Pie")==0) {
    conf += "/pie_" + pieTarget;
  }
  if (queueDiscType.compare("Cobalt")==0) {
    conf += "/cobalt_" + codelTarget + "_" + codelInterval;
  }

  // parse appConfigFile
  std::vector<uint32_t> numSendersArray;
  std::vector<std::vector<uint32_t>> sendersNumFlowsArray;
  for (uint32_t sink=0; sink<numSinks; sink++) {
    std::vector<uint32_t> sendersNumFlowsThisSink;
    sendersNumFlowsArray.push_back(sendersNumFlowsThisSink);
    std::vector<double> sendersSrcLinkRatesThisSink;
    sendersSrcLinkRatesArray.push_back(sendersSrcLinkRatesThisSink);
    std::vector < std::vector < std::vector<std::string> > > appSettingsThisSink;
    appSettingsArray.push_back(appSettingsThisSink);
  }
  // std::vector < uint32_t > sendersAnumFlows, sendersBnumFlows;
  std::ifstream appConfigStream(appConfigFile);
  if (!appConfigStream.is_open()) {
    std::cerr << "Error opening file: " << appConfigFile << std::endl;
    return 1;
  }
  std::string line;
  uint32_t lineCount = 0;
  while (std::getline(appConfigStream, line)) {
    if (lineCount < numSinks) {
      uint32_t numSenders = std::stoi(line);
      numSendersArray.push_back(numSenders);
      for (uint32_t i=0; i<numSenders; i++) {
        std::vector < std::vector<std::string> > appSettingsPerSender;
        appSettingsArray[lineCount].push_back(appSettingsPerSender);
        sendersSrcLinkRatesArray[lineCount].push_back(0);
        sendersNumFlowsArray[lineCount].push_back(0);
      }
      std::cout << "appSettings of sink " << lineCount << ": " << appSettingsArray[lineCount].size() << std::endl;
    }
    // if (lineCount == 0) {
    //   numSendersA = std::stoi(line);
    //   for (uint32_t i=0; i<numSendersA; i++) {
    //     std::vector < std::vector<std::string> > appSettingsPerSender;
    //     appSettingsSink1.push_back(appSettingsPerSender);
    //     sendersAsrcLinkRates.push_back(0);
    //     sendersAnumFlows.push_back(0);
    //   }
    //   std::cout << "appSettingsSink1: " << appSettingsSink1.size() << std::endl;
    // } else if (lineCount == 1) {
    //   numSendersB = std::stoi(line);
    //   for (uint32_t i=0; i<numSendersB; i++) {
    //     std::vector < std::vector<std::string> > appSettingsPerSender;
    //     appSettingsSink2.push_back(appSettingsPerSender);
    //     sendersBsrcLinkRates.push_back(0);
    //     sendersBnumFlows.push_back(0);
    //   }
    //   std::cout << "appSettingsSink2: " << appSettingsSink2.size() << std::endl;
    // } 
    else {
      std::stringstream ss(line);
      uint64_t appConf;
      uint32_t appSink,appSender,appDiffServ, numFlows;
      float appStart, srcLinkRate;
      std::string appType,ccaType;
      ss >> appSink >> appSender >> appType >> appConf >> appStart >> ccaType >> appDiffServ >> srcLinkRate >> numFlows;
      if (mainRoomQueueScheme == CCA) {
        uint32_t ccaOption = std::stoi(ccaType);
        if (ccaQueueidMapping.find(ccaOption) == ccaQueueidMapping.end()) {
          uint32_t currNumCCA = ccaQueueidMapping.size();
          ccaQueueidMapping.insert( std::pair<uint32_t,uint32_t>(ccaOption,currNumCCA+1) );
        }
      }
      std::vector <std::string> tempAppHolder;
      if (appType != "NULL") {
        // conf += "_" + std::to_string(appSink) + "_" + std::to_string(appSender) + "_" + appType + "_" + std::to_string(appConf) + "_" + ccaType + "_" + std::to_string(appDiffServ);
        tempAppHolder.push_back(appType);
        tempAppHolder.push_back(std::to_string(appConf));
        tempAppHolder.push_back(std::to_string(appStart));
        tempAppHolder.push_back(ccaType);
        tempAppHolder.push_back(std::to_string(appDiffServ));
      } else {
        tempAppHolder.push_back("");
        tempAppHolder.push_back(std::to_string(0));
        tempAppHolder.push_back(std::to_string(0.));
        tempAppHolder.push_back("");
        tempAppHolder.push_back(std::to_string(0));
      }
      // if (appSink == 0) {
      //   appSettingsSink1.at(appSender).push_back(tempAppHolder);
      //   sendersAsrcLinkRates[appSender] = srcLinkRate;
      //   sendersAnumFlows[appSender] += numFlows;
      // } else if (appSink == 1) {
      //   appSettingsSink2.at(appSender).push_back(tempAppHolder);
      //   sendersBsrcLinkRates[appSender] = srcLinkRate;
      //   sendersBnumFlows[appSender] += numFlows;
      // }
      appSettingsArray[appSink].at(appSender).push_back(tempAppHolder);
      sendersSrcLinkRatesArray[appSink][appSender] = srcLinkRate;
      sendersNumFlowsArray[appSink][appSender] += numFlows;
    }
    lineCount++;
  }
  appConfigStream.close();
  // conf = conf.substr(1);

  // if (hop == 3) {
  //   conf += "_" + srcBandwidth + "_" + midBandwidth + "_" + dstBandwidth;
  // }

  // uint16_t randomSeed = 2;
  // srand(randomSeed);

  // AnnC: temporary changes for copa_burstv2
  // conf += "/" + senderAsrcDelay + "_" + senderAmidDelay + "_" + senderBsrcDelay + "_" + senderBmidDelay;
  // conf += "/"+std::to_string(std::stoi(appSettingsSink2.at(0)[0].at(2)));

  /* qdisc settings */
  // conf += "/" + std::to_string(diffServ) + "_" + std::to_string(bufferSize) + "_" + std::to_string((uint16_t)sinkAalpha) + "_" + std::to_string((uint16_t)sinkBalpha) + "_" + queueDiscType;
  // conf += "/" + std::to_string(bufferSize) + "_" + std::to_string((uint16_t)sinkAalpha) + "_" + std::to_string((uint16_t)sinkBalpha) + "_" + queueDiscType;
  conf += "/" + std::to_string(bufferSize) + "_" + std::to_string(bufferAlgorithm);
  if (bufferAlgorithm == DT) {
    // conf += "_" + std::to_string((uint16_t)sinkAalpha) + "_" + std::to_string((uint16_t)sinkBalpha);
    conf += "_" + alphaString;
  } else if (bufferAlgorithm >= MY) {
    conf += "_" + std::to_string(burstReserve);
  }
  // if (queueDiscType == "Dwrr") {
  //   conf += "_" + std::to_string (dwrrPrioRatio).substr (0, 4);
  // } else if (queueDiscType == "Auto") {
  //   conf += "_" + std::to_string (dwrrPrioRatio).substr (0, 4) + "_" + 
  //     autoDecayingFunc + std::to_string (autoDecayingCoef).substr (0, 4);
  // } else if (queueDiscType == "         FqCoDel" || queueDiscType == "CoDel") {
  //   // conf += codelTarget + std::to_string (queueDiscSize);
  //   conf += codelTarget;
  // } else if (queueDiscType == "Tbf") {
  //   conf += "_" + tbfRate + std::to_string (tbfBurst);
  // } else {
  //   // conf += std::to_string (queueDiscSize);
  // }

  // conf += "/" + std::to_string(headRoomQueueScheme) + "_" + std::to_string(headRoomNumQueues) + "_" + std::to_string(mainRoomQueueScheme) + "_" + std::to_string(mainRoomNumQueues);
  conf += "_" + std::to_string(mainRoomQueueScheme) + "_" + std::to_string(mainRoomNumQueues) + "_" + std::to_string(startProbeBuffer);

  // conf += "/"+ std::to_string(aSrcBw) + "_" + std::to_string(bSrcBw);
  conf += "/" + srcBwString + "_" + std::to_string(targetBW);

  // Calculate average sink RTT
  // uint32_t sendersAtotalNumFlows = 0;
  // double sendersAaverageSrcDelay = 0;
  // for (uint32_t i=0; i<sendersAnumFlows.size(); i++) {
  //   sendersAaverageSrcDelay += sendersAnumFlows[i]*sendersAsrcLinkRates[i];
  //   sendersAtotalNumFlows += sendersAnumFlows[i];
  // }
  // if (sendersAtotalNumFlows > 0) {
  //   sendersAaverageSrcDelay /= sendersAtotalNumFlows;
  // } else {
  //   sendersAaverageSrcDelay = 0;
  // }
  
  // uint32_t sendersBtotalNumFlows = 0;
  // double sendersBaverageSrcDelay = 0;
  // for (uint32_t i=0; i<sendersBnumFlows.size(); i++) {
  //   sendersBaverageSrcDelay += sendersBnumFlows[i]*sendersBsrcLinkRates[i];
  //   sendersBtotalNumFlows += sendersBnumFlows[i];
  // }
  // if (sendersBtotalNumFlows > 0) {
  //   sendersBaverageSrcDelay /= sendersBtotalNumFlows;
  // } else {
  //   sendersBaverageSrcDelay = 0;
  // }
  // double aRTTms = 2 * (sendersAaverageSrcDelay + std::stod(aMidDelay));
  // double bRTTms = 2 * (sendersBaverageSrcDelay + std::stod(bMidDelay));
  // conf += "/" + toStringNoTrailingZeros(std::to_string(aRTTms)) + "_" + toStringNoTrailingZeros(std::to_string(bRTTms));
  conf += "/" + rttTag;

  conf += "/" + toStringNoTrailingZeros(std::to_string(dropRateThreshold)) + "_" + std::to_string(monitorlongms) + "_" + adaptiveIncreaseParameterString + "_" + adaptiveDecreaseParameterString; // toStringNoTrailingZeros(std::to_string(adaptiveIncreaseParameter)) + "_" + toStringNoTrailingZeros(std::to_string(adaptiveDecreaseParameter));

  conf += "/" + std::to_string(ParHistLen) + "_" + std::to_string(ParRemoveStartLen) + "_" + std::to_string(ParRemoveStartThres) + "_" + std::to_string(ParExploreThres) + "_" + std::to_string(ParSafeThres)
  + "_" + std::to_string(ParConsecIncreaseThres) + "_" + std::to_string(ParStepIncreaseCap) + "_" + std::to_string(ParIncreaseRatio)
  + "_" + std::to_string(ParConsecDecreaseThres) + "_" + std::to_string(ParStepDecreaseCap) + "_" + std::to_string(ParDecreaseRatio)
  + "_" + std::to_string(ParMinQOutlier) + "_" + std::to_string(ParMinQHold);

  conf += "/" + std::to_string(smoothQlenCollectionByUs) + "_" + std::to_string(smoothWindowByNumData) + "_" + std::to_string(smoothOutlierThresholdByMultiple);

  if (trace != "") {
    std::string traceDir, traceName;
    std::stringstream tracePath (trace);
    std::getline (tracePath, traceDir, '/');
    std::getline (tracePath, traceName, '/');
    conf += "/" + traceName;
  }

  conf += "/" + std::to_string(randomSeed);

  // Parse alphaString,srcBwString,midBwString,midDelayString
  std::vector<double> alphaArray;
  if (alphaString!="") {
    std::string delimiter = "_";
    size_t pos = 0;
    std::string token;
    while ((pos = alphaString.find(delimiter)) != std::string::npos) {
        token = alphaString.substr(0, pos);
        alphaArray.push_back(std::stod(token));
        alphaString.erase(0, pos + delimiter.length());
    }
    alphaArray.push_back(std::stod(alphaString));
  }
  std::cout << "alpha: ";
  for (uint32_t i=0; i<alphaArray.size(); i++) {
    std::cout << alphaArray[i] << ", ";
  }
  std::cout << std::endl;

  std::vector<std::string> srcBwArray;
  if (srcBwString!="") {
    std::string delimiter = "_";
    size_t pos = 0;
    std::string token;
    while ((pos = srcBwString.find(delimiter)) != std::string::npos) {
        token = srcBwString.substr(0, pos);
        srcBwArray.push_back(token);
        srcBwString.erase(0, pos + delimiter.length());
    }
    srcBwArray.push_back(srcBwString);
  }
  std::cout << "srcBw: ";
  for (uint32_t i=0; i<srcBwArray.size(); i++) {
    std::cout << srcBwArray[i] << ", ";
  }
  std::cout << std::endl;

  std::vector<std::string> midBwArray;
  if (midBwString!="") {
    std::string delimiter = "_";
    size_t pos = 0;
    std::string token;
    while ((pos = midBwString.find(delimiter)) != std::string::npos) {
        token = midBwString.substr(0, pos);
        midBwArray.push_back(token);
        midBwString.erase(0, pos + delimiter.length());
    }
    midBwArray.push_back(midBwString);
  }
  std::cout << "midBw: ";
  for (uint32_t i=0; i<midBwArray.size(); i++) {
    std::cout << midBwArray[i] << ", ";
  }
  std::cout << std::endl;

  std::vector<std::string> midDelayArray;
  if (midDelayString!="") {
    std::string delimiter = "_";
    size_t pos = 0;
    std::string token;
    while ((pos = midDelayString.find(delimiter)) != std::string::npos) {
        token = midDelayString.substr(0, pos);
        midDelayArray.push_back(token);
        midDelayString.erase(0, pos + delimiter.length());
    }
    midDelayArray.push_back(midDelayString);
  }
  std::cout << "midDelay: ";
  for (uint32_t i=0; i<midDelayArray.size(); i++) {
    std::cout << midDelayArray[i] << ", ";
  }
  std::cout << std::endl;

  // Parse adaptiveIncreaseParameterString,adaptiveDecreaseParameterString
  std::vector<double> incArray;
  if (adaptiveIncreaseParameterString!="") {
    std::string delimiter = "_";
    size_t pos = 0;
    std::string token;
    while ((pos = adaptiveIncreaseParameterString.find(delimiter)) != std::string::npos) {
        token = adaptiveIncreaseParameterString.substr(0, pos);
        incArray.push_back(std::stod(token));
        adaptiveIncreaseParameterString.erase(0, pos + delimiter.length());
    }
    incArray.push_back(std::stod(adaptiveIncreaseParameterString));
  }
  std::cout << "adaptiveIncreaseParameter: ";
  for (uint32_t i=0; i<incArray.size(); i++) {
    std::cout << incArray[i] << ", ";
  }
  std::cout << std::endl;

  std::vector<double> decArray;
  if (adaptiveDecreaseParameterString!="") {
    std::string delimiter = "_";
    size_t pos = 0;
    std::string token;
    while ((pos = adaptiveDecreaseParameterString.find(delimiter)) != std::string::npos) {
        token = adaptiveDecreaseParameterString.substr(0, pos);
        decArray.push_back(std::stod(token));
        adaptiveDecreaseParameterString.erase(0, pos + delimiter.length());
    }
    decArray.push_back(std::stod(adaptiveDecreaseParameterString));
  }
  std::cout << "adaptiveDecreaseParameter: ";
  for (uint32_t i=0; i<decArray.size(); i++) {
    std::cout << decArray[i] << ", ";
  }
  std::cout << std::endl;

  // if (appAType != "") {
  //   conf += "_" + toStringNoTrailingZeros(std::to_string(appAStart));
  // }
  // if (appBType != "") {
  //   conf += "_" + toStringNoTrailingZeros(std::to_string(appBStart));
  // }
  // if (appCType != "") {
  //   conf += "_" + toStringNoTrailingZeros(std::to_string(appCStart));
  // }
  // if (appDType != "") {
  //   conf += "_" + toStringNoTrailingZeros(std::to_string(appDStart));
  // }
  // if (appEType != "") {
  //   conf += "_" + toStringNoTrailingZeros(std::to_string(appEStart));
  // }
  
  // boost::filesystem::create_directories (dir + conf);
  namespace fs = std::filesystem;
  fs::create_directories(dir + conf);
  std::string pcapFileName    = dir + conf + "/pcap";
  std::string pcapTrFileName  = dir + conf + "/pcap.tr";
  std::string appTrFileNamePrefix   = dir + conf + "/app";
  std::string cwndTrFileName  = dir + conf + "/cwnd.tr";
  std::string qdiscTrFileNamePrefix = dir + conf + "/qdisc";
  std::string bwTrFileNamePrefix    = dir + conf + "/bw"; 
  std::string fctTrFileNamePrefix   = dir + conf + "/fct"; 
  std::string gptTrFileNamePrefix   = dir + conf + "/gpt"; 
  std::string dropTrFileNamePrefix  = dir + conf + "/drop"; 
  std::string torOutFile = dir + conf + "/tor.tr";

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

  torStats = torTraceHelper.CreateFileStream (torOutFile);
	*torStats->GetStream ()
	<< "time "
	<< "bufferSizeMB "
	<< "occupiedBufferPct";
	for (int i=0; i<numSinks; i++) {
		for (int j=0; j<nPrior; j++) {
			*torStats->GetStream () << " sink_port_" << i << "_queue_" << j <<"_qSize";
			*torStats->GetStream () << " sink_port_" << i << "_queue_" << j <<"_throughput";
			*torStats->GetStream () << " sink_port_" << i << "_queue_" << j <<"_sentBytes";
			*torStats->GetStream () << " sink_port_" << i << "_queue_" << j <<"_droppedBytes";
			*torStats->GetStream () << " sink_port_" << i << "_queue_" << j <<"_maxSize";
		}
	}
	*torStats->GetStream () << std::endl;

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
  // Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MicroSeconds (1000000)) );
  // Config::SetDefault ("ns3::TcpSocketBase::RTO", TimeValue (MicroSeconds (1000000)) );
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  Config::SetDefault ("ns3::TcpCopa::ModeSwitch", BooleanValue (false));
  Config::SetDefault ("ns3::PacketTcpSender::SockBufDutyRatio", DoubleValue (sockBufDutyRatio));
  Config::SetDefault ("ns3::FifoQueueDisc::MaxSize", StringValue (std::to_string(queueDiscSize) + "p"));
  // Config::SetDefault ("ns3::PfifoFastQueueDisc::MaxSize", StringValue (std::to_string(queueDiscSize) + "p"));
  Config::SetDefault("ns3::GenQueueDisc::BufferAlgorithm", UintegerValue(bufferAlgorithm));
  Config::SetDefault("ns3::SharedMemoryBuffer::BufferSize", UintegerValue(bufferSize));
  // Config::SetDefault ("ns3::Ipv4GlobalRouting::FlowEcmpRouting", BooleanValue(true));
  Config::SetDefault("ns3::GenQueueDisc::nPrior", UintegerValue(nPrior));
  Config::SetDefault("ns3::GenQueueDisc::RoundRobin", UintegerValue(1));
  Config::SetDefault("ns3::GenQueueDisc::StrictPriority", UintegerValue(0));

  // Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue (MilliSeconds (10))); // syn retry interval
  // uint32_t BDP = aRTTms * aMidBw/(nPrior-1) / 8.0 *1000;
  // Config::SetDefault ("ns3::TcpSocketBase::RTTBytes", UintegerValue ( BDP ));  //(MilliSeconds (5))
  // Config::SetDefault ("ns3::TcpSocketBase::ClockGranularity", TimeValue (NanoSeconds (10))); //(MicroSeconds (100))
  // Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (200))); //TimeValue (MicroSeconds (80))
	// Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (1073725440)); //1073725440
	// Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (1073725440));
  // Config::SetDefault ("ns3::TcpSocket::ConnCount", UintegerValue (6));  // Syn retry count
  // Config::SetDefault ("ns3::TcpSocketBase::Timestamp", BooleanValue (true));
  // // Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (PACKET_SIZE));
  // Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
  // Config::SetDefault ("ns3::TcpSocket::PersistTimeout", TimeValue (Seconds (20)));

  sharedMemory = CreateObject<SharedMemoryBuffer>();
	sharedMemory->SetAttribute("BufferSize",UintegerValue(bufferSize));
	sharedMemory->SetSharedBufferSize(bufferSize);
  
  // Create gateway, source, and sink
  NodeContainer sinksNodes, bufferNodes;
  std::vector<NodeContainer> sendersNodesArray;
  // sendersANodes.Create(numSendersA);
  // sendersBNodes.Create(numSendersB);
  sinksNodes.Create(numSinks);
  bufferNodes.Create(1);
  for (uint32_t sink=0; sink<numSinks; sink++) {
    NodeContainer sendersNodesThisSink;
    sendersNodesThisSink.Create(numSendersArray[sink]);
    sendersNodesArray.push_back(sendersNodesThisSink);
  }
  // NodeContainer nodes;
  // nodes.Create (hop + 1);

  // Create and configure access link and bottleneck link
  // srcLink is the access link and midLink is the bottleneck link
  // PointToPointHelper senderAmidLink;
  // senderAmidLink.SetDeviceAttribute ("DataRate", StringValue (senderAmidBandwidth));
  // senderAmidLink.SetChannelAttribute ("Delay", StringValue (senderAmidDelay));

  // PointToPointHelper senderBmidLink;
  // senderBmidLink.SetDeviceAttribute ("DataRate", StringValue (senderBmidBandwidth));
  // senderBmidLink.SetChannelAttribute ("Delay", StringValue (senderBmidDelay));

  // std::cout << "***Debug: senderBmidDelay=" << senderBmidDelay << ", senderAmidDelay=" << senderAmidDelay << std::endl;

  std::vector<PointToPointHelper> senderMidLinkArray;
  for (uint32_t sink=0; sink<numSinks; sink++) {
    PointToPointHelper senderMidLink;
    senderMidLink.SetDeviceAttribute ("DataRate", StringValue (midBwArray[sink]+"Mbps"));
    senderMidLink.SetChannelAttribute ("Delay", StringValue (midDelayArray[sink]+"ms"));
    senderMidLinkArray.push_back(senderMidLink);
  }
  
  // PointToPointHelper dstLink;
  // dstLink.SetDeviceAttribute ("DataRate", StringValue (dstBandwidth));
  // dstLink.SetChannelAttribute ("Delay", StringValue (dstDelay));

  InternetStackHelper stack;
  stack.InstallAll ();

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");

  // Configure the source and sink net devices
  // and the channels between the source/sink and the gateway
  TrafficControlHelper tc;
  uint16_t handle = tc.SetRootQueueDisc ("ns3::GenQueueDisc");
  TrafficControlHelper::ClassIdList cid = tc.AddQueueDiscClasses (handle, nPrior , "ns3::QueueDiscClass");
  for(uint32_t num=0;num<nPrior;num++){
      // tc.AddChildQueueDisc (handle, cid[num], "ns3::FifoQueueDisc", "MaxSize", StringValue (std::to_string(queueDiscSize) + "MB"));
      if (queueDiscType.compare("CoDel")==0) {
        tc.AddChildQueueDisc (handle, cid[num], "ns3::"+queueDiscType+"QueueDisc", "Target", StringValue (codelTarget), "Interval", StringValue (codelInterval));
        Config::SetDefault ("ns3::CoDelQueueDisc::MaxSize", StringValue (std::to_string(queueDiscSize) + "p"));
        Config::SetDefault ("ns3::CoDelQueueDisc::Target", StringValue (codelTarget));
        Config::SetDefault ("ns3::CoDelQueueDisc::Interval", StringValue (codelInterval));
      } else if (queueDiscType.compare("Pie")==0) {
        tc.AddChildQueueDisc (handle, cid[num], "ns3::"+queueDiscType+"QueueDisc", "QueueDelayReference", StringValue (pieTarget));
        Config::SetDefault ("ns3::PieQueueDisc::MaxSize", StringValue (std::to_string(queueDiscSize) + "p"));
        Config::SetDefault ("ns3::PieQueueDisc::QueueDelayReference", StringValue (pieTarget));
      } else if (queueDiscType.compare("Cobalt")==0) {
        tc.AddChildQueueDisc (handle, cid[num], "ns3::"+queueDiscType+"QueueDisc", "Target", StringValue (codelTarget), "Interval", StringValue (codelInterval));
        Config::SetDefault ("ns3::CobaltQueueDisc::MaxSize", StringValue (std::to_string(queueDiscSize) + "p"));
        Config::SetDefault ("ns3::CobaltQueueDisc::Target", StringValue (codelTarget));
        Config::SetDefault ("ns3::CobaltQueueDisc::Interval", StringValue (codelInterval));
      }else {
        tc.AddChildQueueDisc (handle, cid[num], "ns3::"+queueDiscType+"QueueDisc");
      }
  }
  // tchBottleneck.SetRootQueueDisc ("ns3::FifoQueueDisc", 
  //                                   "MaxSize", StringValue (std::to_string(queueDiscSize) + "p"));

  std::cout << "monitorLongMs=" << monitorlongms << ", dropRateThreshold=" << dropRateThreshold << std::endl;

  std::vector<std::pair<uint32_t,uint32_t>> fvThresVec;
  if (fixedVaryConfigFile.compare("")!=0) {
    std::ifstream fixedVaryConfigStream(fixedVaryConfigFile);
    if (!fixedVaryConfigStream.is_open()) {
      std::cerr << "Error opening file: " << fixedVaryConfigFile << std::endl;
      return 1;
    }
    std::string dbwLine;
    while (std::getline(fixedVaryConfigStream, dbwLine)) {
      std::stringstream ss(dbwLine);
      uint32_t fvSecond, fvThres;
      ss >> fvSecond >> fvThres;
      fvThresVec.emplace_back(fvSecond,fvThres);
    }
    fixedVaryConfigStream.close();
  }

  uint32_t portid = 0;
  // for (uint32_t senderA=0; senderA<sendersANodes.GetN(); senderA++) {
  //   PointToPointHelper senderAsrcLink;
  //   senderAsrcLink.SetDeviceAttribute ("DataRate", StringValue (senderAsrcBandwidth));
  //   std::string senderAsrcDelay = std::to_string(sendersAsrcLinkRates[senderA]) + "ms";
  //   senderAsrcLink.SetChannelAttribute ("Delay", StringValue (senderAsrcDelay));
  //   NetDeviceContainer devices = senderAsrcLink.Install(sendersANodes.Get(senderA),bufferNodes.Get(0));
  //   QueueDiscContainer queuediscs = tc.Install(devices.Get(1)); // queuedisc on bufferNode
  //   bottleneckQueueDiscsCollection.Add(queuediscs.Get(0));
  //   Ptr<GenQueueDisc> genDisc = DynamicCast<GenQueueDisc> (queuediscs.Get(0));
  //   genDisc->SetPortId(portid++);
  //   genDisc->setNPrior(nPrior);
  //   genDisc->setPortBw(MbpsStringToGbpsDouble(senderAsrcBandwidth)); // double in Gbps
  //   // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
  //   genDisc->setRTT(aRTTms);
  //   genDisc->SetSharedMemory(sharedMemory);
	// 	genDisc->SetBufferAlgorithm(bufferAlgorithm);
  //   // genDisc->setHeadRoomQueueScheme(headRoomQueueScheme);
  //   genDisc->setMainRoomQueueScheme(mainRoomQueueScheme);
  //   // genDisc->setHeadRoomNumQueues(headRoomNumQueues);
  //   genDisc->setMainRoomNumQueues(mainRoomNumQueues);
  //   // genDisc->setStartProbeBuffer(startProbeBuffer);
  //   // genDisc->setMonitorLongMs(monitorlongms);
  //   // genDisc->setDropRateThreshold(dropRateThreshold);
  //   genDisc->setProbingStats(startProbeBuffer,monitorlongms,dropRateThreshold,adaptiveIncreaseParameter,adaptiveDecreaseParameter,smoothQlenCollectionByUs,smoothWindowByNumData,smoothOutlierThresholdByMultiple,pawMode);
  //   genDisc->setUpTrackingStats(nPrior);
  //   for(uint32_t n=0;n<nPrior;n++){
	// 		genDisc->alphas[n] = 8;
	// 	}
	// 	address.NewNetwork ();
	// 	address.Assign (devices); // only useful for sinks
  // }

  // for (uint32_t senderB=0; senderB<sendersBNodes.GetN(); senderB++) {
  //   PointToPointHelper senderBsrcLink;
  //   senderBsrcLink.SetDeviceAttribute ("DataRate", StringValue (senderBsrcBandwidth));
  //   std::string senderBsrcDelay = std::to_string(sendersBsrcLinkRates[senderB]) + "ms";
  //   senderBsrcLink.SetChannelAttribute ("Delay", StringValue (senderBsrcDelay));
  //   NetDeviceContainer devices = senderBsrcLink.Install(sendersBNodes.Get(senderB),bufferNodes.Get(0));
  //   QueueDiscContainer queuediscs = tc.Install(devices.Get(1)); // queuedisc on bufferNode
  //   bottleneckQueueDiscsCollection.Add(queuediscs.Get(0));
  //   Ptr<GenQueueDisc> genDisc = DynamicCast<GenQueueDisc> (queuediscs.Get(0));
  //   genDisc->SetPortId(portid++);
  //   genDisc->setNPrior(nPrior);
  //   genDisc->setPortBw(MbpsStringToGbpsDouble(senderBsrcBandwidth)); // double in Gbps
  //   // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
  //   genDisc->setRTT(bRTTms);
  //   genDisc->SetSharedMemory(sharedMemory);
	// 	genDisc->SetBufferAlgorithm(bufferAlgorithm);
  //   // genDisc->setHeadRoomQueueScheme(headRoomQueueScheme);
  //   genDisc->setMainRoomQueueScheme(mainRoomQueueScheme);
  //   // genDisc->setHeadRoomNumQueues(headRoomNumQueues);
  //   genDisc->setMainRoomNumQueues(mainRoomNumQueues);
  //   // genDisc->setStartProbeBuffer(startProbeBuffer);
  //   // genDisc->setMonitorLongMs(monitorlongms);
  //   // genDisc->setDropRateThreshold(dropRateThreshold);
  //   genDisc->setProbingStats(startProbeBuffer,monitorlongms,dropRateThreshold,adaptiveIncreaseParameter,adaptiveDecreaseParameter,smoothQlenCollectionByUs,smoothWindowByNumData,smoothOutlierThresholdByMultiple,pawMode);
  //   genDisc->setUpTrackingStats(nPrior);
  //   for(uint32_t n=0;n<nPrior;n++){
	// 		genDisc->alphas[n] = 8;
	// 	}
	// 	address.NewNetwork ();
	// 	address.Assign (devices); // only useful for sinks
  // }

  for (uint32_t sink=0; sink<numSinks; sink++) {
    for (uint32_t sender=0; sender<sendersNodesArray[sink].GetN(); sender++) {
      PointToPointHelper senderSrcLink;
      senderSrcLink.SetDeviceAttribute ("DataRate", StringValue (srcBwArray[sink]+"Mbps"));
      senderSrcLink.SetChannelAttribute ("Delay", StringValue (std::to_string(sendersSrcLinkRatesArray[sink][sender]) + "ms"));
      NetDeviceContainer devices = senderSrcLink.Install(sendersNodesArray[sink].Get(sender),bufferNodes.Get(0));
      QueueDiscContainer queuediscs = tc.Install(devices.Get(1)); // queuedisc on bufferNode
      bottleneckQueueDiscsCollection.Add(queuediscs.Get(0));
      Ptr<GenQueueDisc> genDisc = DynamicCast<GenQueueDisc> (queuediscs.Get(0));
      genDisc->SetPortId(portid++);
      genDisc->setNPrior(nPrior);
      // genDisc->setPortBw(MbpsStringToGbpsDouble(senderBsrcBandwidth)); // double in Gbps
      genDisc->setPortBw(MbpsStringToGbpsDouble(srcBwArray[sink]+"Mbps"));
      genDisc->setTargetBw(targetBW);
      genDisc->setParameters(ParHistLen,ParRemoveStartLen,ParRemoveStartThres,ParExploreThres,ParSafeThres,ParConsecIncreaseThres,ParStepIncreaseCap,ParIncreaseRatio,ParConsecDecreaseThres,ParStepDecreaseCap,ParDecreaseRatio,ParMinQOutlier,ParMinQHold);
      // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
      // genDisc->setRTT(bRTTms);
      genDisc->SetSharedMemory(sharedMemory);
      genDisc->SetBufferAlgorithm(bufferAlgorithm);
      // genDisc->setHeadRoomQueueScheme(headRoomQueueScheme);
      genDisc->setMainRoomQueueScheme(mainRoomQueueScheme);
      // genDisc->setHeadRoomNumQueues(headRoomNumQueues);
      genDisc->setMainRoomNumQueues(mainRoomNumQueues);
      // genDisc->setStartProbeBuffer(startProbeBuffer);
      // genDisc->setMonitorLongMs(monitorlongms);
      // genDisc->setDropRateThreshold(dropRateThreshold);
      genDisc->setProbingStats(startProbeBuffer,monitorlongms,dropRateThreshold,incArray[sink],decArray[sink],smoothQlenCollectionByUs,smoothWindowByNumData,smoothOutlierThresholdByMultiple,pawMode);
      genDisc->setUpTrackingStats(nPrior);
      for(uint32_t n=0;n<nPrior;n++){
        genDisc->alphas[n] = 8; // this is the input port
      }
      for (const auto& entry : fvThresVec) {
        genDisc->insertIntoFixedVaryThresVec(entry.first,entry.second);
      }
      address.NewNetwork ();
      address.Assign (devices); // only useful for sinks
    }
  }
  std::cout << "Finish setting up input ports" << std::endl;

  // Ipv4InterfaceContainer sinkInterfaces;
  // AnnC: assume there are 2 sinks, one for senderA and the other for senderB
  // for (uint32_t sink=0; sink<sinksNodes.GetN(); sink++) {
  //   NetDeviceContainer devices;
  //   if (sink == 0) {
  //     devices = senderAmidLink.Install(bufferNodes.Get(0), sinksNodes.Get(sink));
  //   } else if (sink == 1) {
  //     devices = senderBmidLink.Install(bufferNodes.Get(0), sinksNodes.Get(sink));
  //   }
  //   QueueDiscContainer queuediscs = tc.Install(devices.Get(0)); // queuedisc on bufferNode
  //   bottleneckQueueDiscsCollection.Add(queuediscs.Get(0));
  //   Ptr<GenQueueDisc> genDisc = DynamicCast<GenQueueDisc> (queuediscs.Get(0));
  //   genDisc->SetPortId(portid++);
  //   genDisc->setNPrior(nPrior);
  //   if (sink == 0) {
  //     genDisc->setPortBw(MbpsStringToGbpsDouble(senderAmidBandwidth)); // double in Gbps
  //     // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
  //     genDisc->setRTT(aRTTms);
  //   } else if (sink == 1) {
  //     genDisc->setPortBw(MbpsStringToGbpsDouble(senderBmidBandwidth)); // double in Gbps
  //     // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
  //     genDisc->setRTT(bRTTms);
  //   }
  //   genDisc->SetSharedMemory(sharedMemory);
	// 	genDisc->SetBufferAlgorithm(bufferAlgorithm);
  //   // genDisc->setHeadRoomQueueScheme(headRoomQueueScheme);
  //   genDisc->setMainRoomQueueScheme(mainRoomQueueScheme);
  //   // genDisc->setHeadRoomNumQueues(headRoomNumQueues);
  //   genDisc->setMainRoomNumQueues(mainRoomNumQueues);
  //   // genDisc->setStartProbeBuffer(startProbeBuffer);
  //   // genDisc->setMonitorLongMs(monitorlongms);
  //   // genDisc->setDropRateThreshold(dropRateThreshold);
  //   genDisc->setProbingStats(startProbeBuffer,monitorlongms,dropRateThreshold,adaptiveIncreaseParameter,adaptiveDecreaseParameter,smoothQlenCollectionByUs,smoothWindowByNumData,smoothOutlierThresholdByMultiple,pawMode);
  //   genDisc->setUpTrackingStats(nPrior);
  //   if (sink == 0) {
  //     for(uint32_t n=0;n<nPrior;n++){
  //       genDisc->alphas[n] = sinkAalpha;
  //     }
  //   } else if (sink == 1) {
  //     for(uint32_t n=0;n<nPrior;n++){
  //       genDisc->alphas[n] = sinkBalpha;
  //     }
  //   }
	// 	address.NewNetwork ();
	// 	// sinkInterfaces.Add(address.Assign (devices).Get(1)); // only useful for sinks
  //   address.Assign (devices); // not even for sinks
  // }

  for (uint32_t sink=0; sink<numSinks; sink++) {
    NetDeviceContainer devices;
    // if (sink == 0) {
    //   devices = senderAmidLink.Install(bufferNodes.Get(0), sinksNodes.Get(sink));
    // } else if (sink == 1) {
    //   devices = senderBmidLink.Install(bufferNodes.Get(0), sinksNodes.Get(sink));
    // }
    devices = senderMidLinkArray[sink].Install(bufferNodes.Get(0), sinksNodes.Get(sink));
    QueueDiscContainer queuediscs = tc.Install(devices.Get(0)); // queuedisc on bufferNode
    bottleneckQueueDiscsCollection.Add(queuediscs.Get(0));
    outputQueueDiscsCollection.Add(queuediscs.Get(0));
    Ptr<GenQueueDisc> genDisc = DynamicCast<GenQueueDisc> (queuediscs.Get(0));
    genDisc->SetPortId(portid++);
    genDisc->setNPrior(nPrior);
    // if (sink == 0) {
    //   genDisc->setPortBw(MbpsStringToGbpsDouble(senderAmidBandwidth)); // double in Gbps
    //   // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
    //   genDisc->setRTT(aRTTms);
    // } else if (sink == 1) {
    //   genDisc->setPortBw(MbpsStringToGbpsDouble(senderBmidBandwidth)); // double in Gbps
    //   // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
    //   genDisc->setRTT(bRTTms);
    // }
    genDisc->setPortBw(MbpsStringToGbpsDouble(midBwArray[sink]+"Mbps")); // double in Gbps
    genDisc->setTargetBw(targetBW);
    genDisc->setParameters(ParHistLen,ParRemoveStartLen,ParRemoveStartThres,ParExploreThres,ParSafeThres,ParConsecIncreaseThres,ParStepIncreaseCap,ParIncreaseRatio,ParConsecDecreaseThres,ParStepDecreaseCap,ParDecreaseRatio,ParMinQOutlier,ParMinQHold);
    genDisc->SetSharedMemory(sharedMemory);
		genDisc->SetBufferAlgorithm(bufferAlgorithm);
    // genDisc->setHeadRoomQueueScheme(headRoomQueueScheme);
    genDisc->setMainRoomQueueScheme(mainRoomQueueScheme);
    // genDisc->setHeadRoomNumQueues(headRoomNumQueues);
    genDisc->setMainRoomNumQueues(mainRoomNumQueues);
    // genDisc->setStartProbeBuffer(startProbeBuffer);
    // genDisc->setMonitorLongMs(monitorlongms);
    // genDisc->setDropRateThreshold(dropRateThreshold);
    genDisc->setProbingStats(startProbeBuffer,monitorlongms,dropRateThreshold,incArray[sink],decArray[sink],smoothQlenCollectionByUs,smoothWindowByNumData,smoothOutlierThresholdByMultiple,pawMode);
    genDisc->setUpTrackingStats(nPrior);
    // if (sink == 0) {
    //   for(uint32_t n=0;n<nPrior;n++){
    //     genDisc->alphas[n] = sinkAalpha;
    //   }
    // } else if (sink == 1) {
    //   for(uint32_t n=0;n<nPrior;n++){
    //     genDisc->alphas[n] = sinkBalpha;
    //   }
    // }
    if (alphaArray.size()>0) {
      double alphaThisSink = alphaArray[sink];
      for(uint32_t n=0;n<nPrior;n++){
        genDisc->alphas[n] = alphaThisSink; // It means all queues at the same output port would have the same alpha
      }
    }
    for (const auto& entry : fvThresVec) {
      genDisc->insertIntoFixedVaryThresVec(entry.first,entry.second);
    }
		address.NewNetwork ();
		// sinkInterfaces.Add(address.Assign (devices).Get(1)); // only useful for sinks
    address.Assign (devices); // not even for sinks
  }
  std::cout << "Finish setting up output ports" << std::endl;

  if (deltaBwConfigFile.compare("")!=0) {
    std::ifstream deltaBwConfigStream(deltaBwConfigFile);
    if (!deltaBwConfigStream.is_open()) {
      std::cerr << "Error opening file: " << deltaBwConfigFile << std::endl;
      return 1;
    }
    std::string dbwLine;
    uint32_t dbwLineCount = 0;
    while (std::getline(deltaBwConfigStream, dbwLine)) {
      std::stringstream ss(dbwLine);
      float dbwSecond;
      std::string dbwMbps;
      uint32_t dbwTargetBW;
      ss >> dbwSecond >> dbwMbps >> dbwTargetBW;
      Simulator::Schedule(Seconds(dbwSecond), &DeltaBandwidth, bufferNodes.Get(0), dbwSecond, dbwMbps, dbwTargetBW, numSinks);
    }
    deltaBwConfigStream.close();
  }
  
  sharedMemory->SetAttribute("BurstReserve",UintegerValue(burstReserve));
  double smallestRTTms = 0;
  // if (aRTTms == 0) {
  //   smallestRTTms = bRTTms;
  // } else if (bRTTms == 0 || bRTTms == 2) {
  //   smallestRTTms = aRTTms;
  // } else {
  //   smallestRTTms = std::min(aRTTms,bRTTms);
  // }
  uint32_t myNumPorts = 0;
  for (uint32_t sink=0; sink<numSinks; sink++) {
    myNumPorts += 1;
    myNumPorts += sendersNodesArray[sink].GetN();
  }
  sharedMemory->setUp(myNumPorts, nPrior, randomSeed, smallestRTTms, mainRoomNumQueues);

  // NetDeviceContainer devicesSrcLink, devicesDstLink, devicesMidLink;

  // devicesSrcLink = srcLink.Install (nodes.Get (0), nodes.Get (1));
  // InstallQdisc ("Fifo", queueDiscSize, devicesSrcLink);

  // address.NewNetwork ();
  // Ipv4InterfaceContainer interfaces = address.Assign (devicesSrcLink);

  // devicesMidLink = midLink.Install (nodes.Get (1), nodes.Get (2));
  // QueueDiscContainer qdiscs = InstallQdisc (queueDiscType, queueDiscSize, devicesMidLink);

  // // reset queue disc after bandwidth reduction, only in cca-cut exps
  // if (resetQueueDisc) {
  //   uint32_t cutRatio = resetQueueDisc % 100;
  //   uint32_t cutTime = resetQueueDisc / 100;
    
  //   if (cutRatio > 5) {
  //     uint32_t newQueueSize = queueDiscSize * 5. / cutRatio;
  //     Simulator::Schedule (MilliSeconds (cutTime * traceInterval + 1000), &ResetQueueSize, qdiscs.Get (0), newQueueSize);
  //   }
  // }

  // address.NewNetwork ();
  // interfaces = address.Assign (devicesMidLink);

  // // Only for experiments where Confucius is not on the bottleneck.
  // if (hop == 3) {
  //   devicesDstLink = dstLink.Install (nodes.Get (2), nodes.Get (3));
  //   InstallQdisc ("Fifo", queueDiscSize, devicesDstLink);
  //   address.NewNetwork ();
  //   interfaces = address.Assign (devicesDstLink);
  // }

  // Ipv4InterfaceContainer sinkInterface;
  // sinkInterface.Add (interfaces.Get (1));

  NS_LOG_WARN ("Initialize Global Routing.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // uint32_t portBase = 10000;
  uint32_t portBase = 1000;
  uint32_t sender_id = 0;
  uint32_t random_seed = randomSeed;
  // for (uint32_t senderA=0; senderA<sendersANodes.GetN(); senderA++) {
  //   std::string appTrFileName = appTrFileNamePrefix + "_sender" + std::to_string(sender_id) + ".tr";
  //   std::string bwTrFileName = bwTrFileNamePrefix + "_sender" + std::to_string(sender_id) + ".tr";
  //   std::string fctTrFileName = fctTrFileNamePrefix + "_sender" + std::to_string(sender_id);
  //   std::string gptTrFileName = gptTrFileNamePrefix + "_sender" + std::to_string(sender_id);
  //   InstallApp(sendersANodes.Get(senderA),sinksNodes.Get(0),appTrFileName,bwTrFileName,fctTrFileName,gptTrFileName,stopTime,0,senderA, portBase,random_seed, mainRoomQueueScheme);
  //   // portBase += 10000*appSettingsSink1.at(senderA).size();
  //   portBase += 5000*appSettingsSink1.at(senderA).size();
  //   sender_id++;
  //   random_seed++;
  // }

  // for (uint32_t senderB=0; senderB<sendersBNodes.GetN(); senderB++) {
  //   std::string appTrFileName = appTrFileNamePrefix + "_sender" + std::to_string(sender_id) + ".tr";
  //   std::string bwTrFileName = bwTrFileNamePrefix + "_sender" + std::to_string(sender_id) + ".tr";
  //   std::string fctTrFileName = fctTrFileNamePrefix + "_sender" + std::to_string(sender_id);
  //   std::string gptTrFileName = gptTrFileNamePrefix + "_sender" + std::to_string(sender_id);
  //   InstallApp(sendersBNodes.Get(senderB),sinksNodes.Get(1),appTrFileName,bwTrFileName,fctTrFileName,gptTrFileName,stopTime,1,senderB, portBase,random_seed, mainRoomQueueScheme);
  //   // portBase += 10000*appSettingsSink2.at(senderB).size();
  //   portBase += 5000*appSettingsSink2.at(senderB).size();
  //   sender_id++;
  //   random_seed++;
  // }

  for (uint32_t sink=0; sink<numSinks; sink++) {
    for (uint32_t sender=0; sender<sendersNodesArray[sink].GetN(); sender++) {
      std::string appTrFileName = appTrFileNamePrefix + "_sender" + std::to_string(sender_id) + ".tr";
      std::string bwTrFileName = bwTrFileNamePrefix + "_sender" + std::to_string(sender_id) + ".tr";
      std::string fctTrFileName = fctTrFileNamePrefix + "_sender" + std::to_string(sender_id);
      std::string gptTrFileName = gptTrFileNamePrefix + "_sender" + std::to_string(sender_id);
      InstallApp(sendersNodesArray[sink].Get(sender),sinksNodes.Get(sink),appTrFileName,bwTrFileName,fctTrFileName,gptTrFileName,stopTime,sink,sender, portBase,random_seed, mainRoomQueueScheme);
      // portBase += 10000*appSettingsSink2.at(senderB).size();
      portBase += 1000*appSettingsArray[sink].at(sender).size();
      sender_id++;
      random_seed++;
    }
  }
  std::cout << "Finish setting up apps" << std::endl;

  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.InstallAll();

  // InstallApp (nodes.Get(0), nodes.Get(hop), sinkInterface, ccaType,
  //   appTrFileName, bwTrFileName, fctTrFileName, stopTime);

  if (trace != "") {
    // for (uint32_t senderA=0; senderA<sendersANodes.GetN(); senderA++) {
    //   BandwidthTrace (sendersANodes.Get(0), bufferNodes.Get(0), trace, 
    //   BW_CHANGE & ~DELAY_CHANGE & ~LOSS_CHANGE, 
    //   traceInterval, true);
    // }

    // for (uint32_t senderB=0; senderB<sendersBNodes.GetN(); senderB++) {
    //   BandwidthTrace (sendersBNodes.Get(0), bufferNodes.Get(0), trace, 
    //   BW_CHANGE & ~DELAY_CHANGE & ~LOSS_CHANGE, 
    //   traceInterval, true);
    // }
    // BandwidthTrace (nodes.Get(0), nodes.Get(1), trace, 
    //   BW_CHANGE & ~DELAY_CHANGE & ~LOSS_CHANGE, 
    //   traceInterval, true);
  }

  if (torstats_sinkonly) {
    Simulator::Schedule(MicroSeconds (10),&InvokeToRStatsSinkOnly,torStats, bufferSize, nPrior, bufferAlgorithm, numSinks, queueDiscType);
  } else {
    Simulator::Schedule(MicroSeconds (10),&InvokeToRStats,torStats, bufferSize, nPrior, bufferAlgorithm);
  }
  
  AsciiTraceHelper ascii;
  Packet::EnablePrinting ();
  for (uint32_t port=0; port<bottleneckQueueDiscsCollection.GetN(); port++) {
    std::string dropTrFileName = dropTrFileNamePrefix + "_port" + std::to_string(port) + ".tr";
    Ptr<GenQueueDisc> qdisc = DynamicCast<GenQueueDisc>(bottleneckQueueDiscsCollection.Get(port));
    if (!light_logging) {
      Simulator::Schedule (MicroSeconds (10), &TraceQdiscDrop, qdisc, ascii.CreateFileStream (dropTrFileName));
    }
  }

  for (uint32_t port=0; port<bottleneckQueueDiscsCollection.GetN(); port++) {
    std::string qdiscTrFileName = qdiscTrFileNamePrefix + "_port" + std::to_string(port) + ".tr";
    Ptr<GenQueueDisc> q = DynamicCast<GenQueueDisc>(bottleneckQueueDiscsCollection.Get(port));
    if (!light_logging) {
      Simulator::Schedule (Seconds(statIntervalSec + 1), &StatQdisc, q, queueDiscType, ascii.CreateFileStream (qdiscTrFileName));
    }
  }
  // Simulator::Schedule (MicroSeconds (10), &TraceQdiscDrop, qdiscs.Get (0), ascii.CreateFileStream (dropTrFileName));
  // Simulator::Schedule (Seconds(statIntervalSec + 1), &StatQdisc, qdiscs.Get (0), queueDiscType, ascii.CreateFileStream (qdiscTrFileName));

  if (isPcapEnabled) {
    // srcLink.EnablePcap (pcapFileName, nodes, true);
    // AsciiTraceHelper ascii;
    // srcLink.EnableAsciiAll (ascii.CreateFileStream (pcapTrFileName));
  }

  RngSeedManager::SetSeed (randomSeed);

  Simulator::Stop (Seconds (stopTime));
  Simulator::Run ();

  flowMonitor->SerializeToXmlFile(dir + conf + "/flowmonitor.xml", true, true);

  // sharedMemory->printDesignZeroVec(sharedMemory->getTotalProbers()-1); // AnnC: hard-coded for a single-port scenario
  Simulator::Destroy ();
  return 0;
}
