/*
* Ann Zhou, copied this file entirely from Vamsi (I think). It looks like
* this file is adapted from prio-queue-disc.cc
*/

/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
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
 * Authors:  Stefano Avallone <stavallo@unina.it>
 */

#include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/object-factory.h"
#include "ns3/socket.h"
#include "gen-queue-disc.h"
#include <algorithm>
#include <iterator>
#include <deque>
#include <tuple>
#include <set>
#include <map>

#include "ns3/queue.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/tcp-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/ppp-header.h"
#include "ns3/flow-id-tag.h"
#include "ns3/custom-priority-tag.h"
#include "ns3/unsched-tag.h"
#include "ns3/homa-header.h"
#include "ns3/int-header.h"

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

/*StatusTracker*/
# define HREntering 301
# define HRFlowEnd 302
# define HRClearPackets 303
# define MRWaitRoom 304
# define MRProbing 305
# define MRFlowEnd 306
# define MRClearPackets 307
# define StatusNone 308

# define verbose false
# define debug false

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("GenQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (GenQueueDisc);

TypeId GenQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::GenQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<GenQueueDisc> ()
    .AddAttribute ("nPrior","number of queues", UintegerValue (5),
                                     MakeUintegerAccessor (&GenQueueDisc::nPrior),
                                        MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("sat","saturation detection",
                    UintegerValue (20*1400),
                    MakeUintegerAccessor (&GenQueueDisc::sat),
                    MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("BufferAlgorithm","BufferAlgorithm",
                    UintegerValue (DT),
                    MakeUintegerAccessor (&GenQueueDisc::bufferalg),
                    MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("HomaQueue","HomaQueue (this attribute is for the sake of finding priority tag in Homa header specifically. Make sure to set strict priority when using HOMA.)",
                    BooleanValue (false),
                    MakeBooleanAccessor (&GenQueueDisc::is_homa),
                    MakeBooleanChecker())
    .AddAttribute ("enableDPPQueue","whether to use extra priority queue or not. This concerns IB algorithm. Turn this off in single queue setting.",
                    BooleanValue (false),
                    MakeBooleanAccessor (&GenQueueDisc::enableDPPQueue),
                    MakeBooleanChecker())
    .AddAttribute ("alphaUnsched","alphaUnsched",
                    DoubleValue (1024),
                    MakeDoubleAccessor (&GenQueueDisc::alphaUnsched),
                    MakeDoubleChecker<double> ())
    .AddAttribute ("portBW","portBW in Gbps",
                    DoubleValue (10),
                    MakeDoubleAccessor (&GenQueueDisc::portBW),
                    MakeDoubleChecker<double> ())
    // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
    .AddAttribute ("RTTms","RTT in ms",
                    DoubleValue (100),
                    MakeDoubleAccessor (&GenQueueDisc::RTTms),
                    MakeDoubleChecker<double> ())

    .AddAttribute ("updateInterval","NANOSECONDS update interval for dequeue rate and N in ActiveBufferManagement", UintegerValue(30000),
                  MakeUintegerAccessor(&GenQueueDisc::updateInterval),
                  MakeUintegerChecker<uint64_t>())
    .AddAttribute ("staticBuffer","static buffer",
                              UintegerValue (0),
                              MakeUintegerAccessor (&GenQueueDisc::staticBuffer),
                              MakeUintegerChecker<uint64_t> ())
    .AddAttribute ("RoundRobin","round robin scheduling",
                              UintegerValue (1),
                              MakeUintegerAccessor (&GenQueueDisc::round_robin),
                              MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("StrictPriority","strict priority scheduling",
                              UintegerValue (0),
                              MakeUintegerAccessor (&GenQueueDisc::strict_priority),
                              MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}

GenQueueDisc::GenQueueDisc ()
  : QueueDisc (QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::BYTES)
{
  NS_LOG_FUNCTION (this);
  alphas = nullptr;
  for (uint32_t i=0;i<1008;i++){
    firstSeen[i]= Seconds(0);
    lastAccepted[i]=ns3::Simulator::Now();
    numBytesSent[i]=0;
    firstSeenQueue[i]= Seconds(0);
    lastAcceptedQueue[i]=ns3::Simulator::Now();
    numBytesSentQueue[i]=0;
    droppedBytes[i]=0;
    DeqRate[i]=1;
    Deq[i]=0;
    MFair[i]=1000*1000*4;
    QRefAfd[i]=1000*15;
    nofP[i] = 0;
    DPPQueue = 1;
  }
}

GenQueueDisc::~GenQueueDisc ()
{
  NS_LOG_FUNCTION (this);
  if (alphas)
    delete alphas;
}

uint64_t
GenQueueDisc::GetBuffersize(uint32_t p){
  uint64_t temp = bufferMax[p];
  bufferMax[p]=0;
  return temp;
}

double
GenQueueDisc::GetThroughputEnQueue(uint32_t p, double nanodelay){
    double th = 1e9*8*numBytesSent[p]/nanodelay;
    numBytesSent[p]=0;
  return th;
}

uint64_t
GenQueueDisc::GetDroppedBytes(uint32_t p){
  uint64_t droppedB = droppedBytes[p];
  droppedBytes[p]=0;
  return droppedB;
}

double GenQueueDisc::GetAlpha(uint32_t p){
  return alphas[p];
}

double GenQueueDisc::GetRemainingBuffer() {
  return sharedMemory->GetRemainingBuffer();
}

bool GenQueueDisc::DynamicThresholds(uint32_t priority, Ptr<Packet> packet){

  // uint8_t flag = 0;
  // PacketMetadata::ItemIterator mdit = packet->BeginItem();
  // PacketMetadata::Item item;
  // while (mdit.HasNext()) {
  //   item = mdit.Next();
  //   if (item.tid.GetName() == "ns3::TcpHeader") {
  //     NS_ASSERT (item.tid.HasConstructor ());
  //     Callback<ObjectBase *> constructor = item.tid.GetConstructor ();
  //     NS_ASSERT (!constructor.IsNull ());
  //     ObjectBase *instance = constructor ();
  //     NS_ASSERT (instance != 0);
  //     TcpHeader *tcpHeader = dynamic_cast<TcpHeader *> (instance);
  //     NS_ASSERT (tcpHeader != 0);
  //     tcpHeader->Deserialize(item.current);

  //     flag = tcpHeader->GetFlags();
  //     // std::cout << "############flag=" << (uint32_t)flag << ", portid=" << portId << ", priority=" << priority << std::endl;
  //     break;
  //   }
  // }
  // /* Find flow-id if exists */
  // bool found;
  // uint32_t flowId = 0;
  // FlowIdTag tag;
  // found = packet->PeekPacketTag (tag);
  // if(found){
  //   flowId=tag.GetFlowId();
  // } else {
  //   std::cout << "mybm flowId not found" << std::endl;
  // }
  // std::cout << Simulator::Now() << ": flag=" << (uint32_t)flag << ", flowid=" << flowId << "***********Debug" << std::endl;

  double remaining = sharedMemory->GetRemainingBuffer();
  uint64_t maxSize = alphas[priority]*remaining;
  if (maxSize> UINT32_MAX)
    maxSize = UINT32_MAX-1500;

  uint32_t qSize = GetQueueDiscClass (priority)->GetQueueDisc ()->GetNBytes();
  // std::cout << Simulator::Now() << ",priority=" << priority << ",remaining=" << remaining << ",maxSize=" << maxSize << ",qSize=" << qSize << std::endl;
  if ( ((qSize + packet->GetSize()) >  maxSize) || (sharedMemory->GetRemainingBuffer() < packet->GetSize())  ){
    return false; // drop
  }
  else{
    return true;
  }

}

void
GenQueueDisc::UpdateDequeueRate(double nanodelay){ // delay in NANOSECONDS. Pay attention here.
  double num=0;
  /* This is because of round-robin scheduling. More to be added soon. In general, its better to measure dequeue rate like PIE */
  // for (uint32_t p=0;p<nPrior;p++){
  //   if (GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes()>sat){
  //     num++;
  //   }
  // }

  // if (num==0)
  //   num=1;

  // for (uint32_t p=0;p<nPrior;p++){
  //   if (GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes()>sat){
  //     DeqRate[p] = double(1.0/num);
  //   }
  //   else{
  //     DeqRate[p]=1;
  //   }
  // }
  for (uint32_t p=0;p<nPrior;p++){
    double th = 8*Deq[p]/nanodelay/portBW; // portBW should be in Gbps
    if (th < 1.0/double(nPrior) || th > 1){
      th = 1;
    }
    DeqRate[p] = th;
    Deq[p] = 0;
  }
}

void GenQueueDisc::UpdateNofP(){
  for (uint32_t i=0; i< nPrior; i++){
    nofP[i] = sharedMemory->GetNofP(i);
  }
}


void GenQueueDisc::InvokeUpdates(double nanodelay){
  UpdateDequeueRate(nanodelay);
  UpdateNofP();
  Simulator::Schedule(NanoSeconds(nanodelay),&GenQueueDisc::InvokeUpdates,this,nanodelay);
}

bool GenQueueDisc::ActiveBufferManagement(uint32_t priority, Ptr<Packet> packet){

  double alpha = 1;

  /* A tag is attached by the end-hosts on all the packets which are unscheduled (first RTT bytes). Find the tag first.*/
  bool found;
  uint32_t unsched = 0;
  UnSchedTag tag;
  found = packet->PeekPacketTag (tag);
  if(found){
    unsched=tag.GetValue();
  }

  /* prioritize unscheduled packets */
  if (unsched){
    alpha = alphaUnsched;
  }
  else{
    alpha = alphas[priority];
  }

  uint64_t currentSize=GetQueueDiscClass (priority)->GetQueueDisc ()->GetNBytes();

  double satLevel = double(currentSize)/sat;
  if (satLevel>1){
    satLevel=1;
  }


  sharedMemory->setSaturated(portId,priority,satLevel);

  if (firstTimeUpdate){
    firstTimeUpdate=false;
    InvokeUpdates(updateInterval);
  }

  double remaining = sharedMemory->GetRemainingBuffer();
  // std::cout << "alpha " << alpha << " n " << nofP[priority] << " deq " << DeqRate[priority] << std::endl;
  uint64_t maxSize = double(alpha*(remaining)/nofP[priority])*DeqRate[priority];

  if (maxSize> UINT32_MAX)
    maxSize = UINT32_MAX-1500;

  uint32_t qSize = GetQueueDiscClass (priority)->GetQueueDisc ()->GetNBytes();
  if ( ((qSize + packet->GetSize()) >  maxSize) || (sharedMemory->GetRemainingBuffer() < packet->GetSize())  ){
    return false; // drop
  }
  else{
    return true;
  }
}


bool GenQueueDisc::FlowAwareBuffer(uint32_t priority, Ptr<Packet> packet){

  double alpha;

  /* Find flow-id if exists */
  bool found;
  uint32_t flowId = 0;
  FlowIdTag tag;
  found = packet->PeekPacketTag (tag);
  if(found){
    flowId=tag.GetFlowId();
  }

  /* Find the flow entry */
  if(FlowCount.find(flowId) == FlowCount.end()){
    FlowCount[flowId].first=0;
    FlowCount[flowId].second=Simulator::Now();
  }

  /* If the flow did not appear in the last FabWindow duration, reset its bytes counter to zero. */
  if(Simulator::Now()-FlowCount[flowId].second>FabWindow){
    FlowCount[flowId].first=0;
  }

  /* Per-flow counters - increment bytes count and last updated time. */
  FlowCount[flowId].first+=packet->GetSize();
  FlowCount[flowId].second=Simulator::Now();

  /* If the flow sent less than FabThreshold no.of bytes in the last FabWindow, then prioritize these packets */
  if(FlowCount[flowId].first<FabThreshold){
    alpha = alphaUnsched; // alphaUnsched is usually set to a high value i.e., these packets are prioritized.
  }
  else{
    alpha=alphas[priority];
  }

  double remaining = sharedMemory->GetRemainingBuffer();
  uint64_t maxSize = alpha*remaining;
  if (maxSize> UINT32_MAX)
    maxSize = UINT32_MAX-1500;


  uint32_t qSize = GetQueueDiscClass (priority)->GetQueueDisc ()->GetNBytes();
  if ( ((qSize + packet->GetSize()) >  maxSize) || (sharedMemory->GetRemainingBuffer() < packet->GetSize())  ){
    return false; // drop
  }
  else{
    return true;
  }

}

bool GenQueueDisc::CompleteSharing(uint32_t priority, Ptr<Packet> packet){
  if(sharedMemory->GetRemainingBuffer() < packet->GetSize()){
    return false;// drop
  }
  else{
    return true;
  }
}

void
GenQueueDisc::SetQrefAfd(uint32_t p, uint32_t ref){
  QRefAfd[p]=ref;
}
uint32_t
GenQueueDisc::GetQrefAfd(uint32_t p){
  return QRefAfd[p];
}

int
GenQueueDisc::DropAfd(double prob,uint32_t priority){
  uint32_t qsize = GetQueueDiscClass (priority)->GetQueueDisc ()->GetNBytes();
  double x = double(rand())/RAND_MAX;
  // 150*1024 is the recommended value for 10Gbps links https://www.cisco.com/c/en/us/products/collateral/switches/nexus-9000-series-switches/white-paper-c11-738488.html
  return ((x<prob) && (qsize>150*1024));
}


bool GenQueueDisc::IntelligentBuffer(uint32_t priority, Ptr<Packet> packet){
  bool accept;
  if(Simulator::Now() > AfdWindow + timeSinceLastChangeAdf){
    for(auto it=M.begin();it!=M.end();++it){
      it->second.first=it->second.second;
      it->second.second=1; //1 just to avoid divide by zero errors
    }
    for(uint32_t i=0;i<nPrior;i++){
      uint32_t Qnow = GetQueueDiscClass (i)->GetQueueDisc ()->GetNBytes();
      MFair[i]=MFair[i]-a1*((double)Qnow - (double)QRefAfd[i])+a2*((double)Qold[i] - (double)QRefAfd[i]); // a1 and a2 --> 1.8 and 1.7
      if(MFair[i]<0)
        MFair[i]=0;

      Qold[i]=Qnow;
    }
    timeSinceLastChangeAdf=Simulator::Now();
  }

  bool found;
  uint32_t flowId = 0;
  FlowIdTag tag;
  found = packet->PeekPacketTag (tag);
  if(found){flowId=tag.GetFlowId();}

  if(FlowCount.find(flowId) == FlowCount.end()){
      FlowCount[flowId].first=0;
      FlowCount[flowId].second=Simulator::Now();
  }

  //DPP
  if(Simulator::Now()-FlowCount[flowId].second>DppWindow)
    FlowCount[flowId].first=0;

  FlowCount[flowId].first+=1;
  FlowCount[flowId].second=Simulator::Now();

  if(FlowCount[flowId].first<DppThreshold && enableDPPQueue){ // Short flows are sent to queue-0 which is a priority queue.
    DPPQueue=0;
    accept = DynamicThresholds(DPPQueue,packet);
  }
  else{
    M[priority].second += packet->GetSize();

    if(!M[priority].first){
      M[priority].first=1; // Just to avoid divide by zero.
    }
    double dropP = 1.0-(double(std::min(15*M[priority].first,uint32_t(MFair[priority])))/(15*M[priority].first));
    if(dropP<0){
      dropP=0;
    }

    DPPQueue = priority;
    accept = (DynamicThresholds(DPPQueue,packet) && !DropAfd(DPPQueue,dropP));
  }
  return accept;
}



bool GenQueueDisc::AcceptPacket(uint32_t priority, Ptr<Packet> packet){
  bool accept;
  switch (bufferalg){
    case DT:
      accept = DynamicThresholds(priority,packet);
      break;
    case ABM:
      accept = ActiveBufferManagement(priority,packet);
      break;
    case FAB:
      accept = FlowAwareBuffer(priority,packet);
      break;
    case CS:
      accept = CompleteSharing(priority,packet);
      break;
    case IB:
      accept = IntelligentBuffer(priority,packet);
      break;
    case MY:
      accept = MyBM(priority,packet);
      break;
    case LLM_DT_COT_0:
      LLM_collect_stats(priority);
      accept = LLM_DT_COT_0_BM(priority,packet);
      break;
    case LLM_DT_COT_1:
      LLM_collect_stats(priority);
      accept = LLM_DT_COT_1_BM(priority,packet);
      break;
    case LLM_DT_COT_2:
      LLM_collect_stats(priority);
      accept = LLM_DT_COT_2_BM(priority,packet);
      break;
    case LLM_DT_BRDG_0:
      LLM_collect_stats(priority);
      accept = LLM_DT_BRDG_0_BM(priority,packet);
      break;
    case LLM_DT_BRDG_1:
      LLM_collect_stats(priority);
      accept = LLM_DT_BRDG_1_BM(priority,packet);
      break;
    case LLM_DT_BRDG_2:
      LLM_collect_stats(priority);
      accept = LLM_DT_BRDG_2_BM(priority,packet);
      break;
    case LLM_TTR_COT_0:
      accept = MyBM(priority,packet,LLM_TTR_COT_0);
      break;
    case LLM_TTR_COT_1:
      accept = MyBM(priority,packet,LLM_TTR_COT_1);
      break;
    case LLM_TTR_COT_2:
      accept = MyBM(priority,packet,LLM_TTR_COT_2);
      break;
    default:
      accept = DynamicThresholds(priority,packet);
  }
  return accept;
}

void
GenQueueDisc::TrimPacket(Ptr<Packet> packetCopy){
  TcpHeader th; Ipv4Header ih; PppHeader ph; IntHeader inth; HomaHeader hh; FlowIdTag ft; MyPriorityTag mt;
  uint32_t trimsize = 0;
  uint32_t thremoved = packetCopy->RemoveHeader(th);
  uint32_t ihremoved = packetCopy->RemoveHeader(ih);
  uint32_t phremoved = packetCopy->RemoveHeader(ph);
  uint32_t hhremoved = packetCopy->RemoveHeader(hh);
  bool intremoved = packetCopy->RemovePacketTag(inth);
  bool ftremoved = packetCopy->RemovePacketTag(ft);
  bool mtremoved = packetCopy->RemovePacketTag(mt);
  packetCopy->RemoveAtEnd(packetCopy->GetSize());
  ft.SetTrim(1);
  if(intremoved){packetCopy->AddPacketTag(inth);}
  if(ftremoved){packetCopy->AddPacketTag(ft);}
  if(mtremoved){packetCopy->AddPacketTag(mt);}
  if(thremoved){packetCopy->AddHeader(th);}
  if(hhremoved){packetCopy->AddHeader(hh);}
  if(ihremoved){packetCopy->AddHeader(ih);}
  if(phremoved){packetCopy->AddHeader(ph);}

  std::cout << packetCopy->GetSize() << std::endl;
}

bool
GenQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  Ptr<Packet> packet = item->GetPacket();

  uint32_t p=0;

  bool found;
  MyPriorityTag a;
  if(!is_homa){
    found = packet->PeekPacketTag(a);
    if(found)p=a.GetPriority();
  }
  else{
    found = packet->PeekPacketTag(a);
    if(found)p=a.GetPriority();

    HomaHeader homaHeader;
    if (!found && item->GetPacket()->PeekHeader(homaHeader)){
      p = homaHeader.GetPrio();
    }
  }
  if (uint32_t(p)>=nPrior)
    p = uint32_t(nPrior-1);

  // bool foundFid;
  // uint32_t flowId = 0;
  // FlowIdTag tag;
  // foundFid = packet->PeekPacketTag (tag);
  // if(foundFid){
  //   flowId=tag.GetFlowId();
  // } else {
  //   std::cout << "doenqueue flowId not found: p=" << p << std::endl;
  //   if (p>0) std::cout << "ERROR: flowId is not found when DoEnqueue but priority is non-zero: p=" << p << std::endl;
  // }

  // if (isMyBM) {
  //   if (p>0) {
  //     // if (flowidMRqueueidMapping.find(flowId) == flowidMRqueueidMapping.end()) {
  //     //   flowidMRqueueidMapping.insert( std::pair<uint32_t,uint32_t>(flowId, p) );
  //     // }

  //     // if (flowidHRqueueidMapping.find(flowId) == flowidHRqueueidMapping.end()) {
  //     // // if (isNewFlow(flowId)) { // AnnC: cannot use isNewFlow here, it would insert the flow
  //     //   // new flow
  //     //   flowidMRqueueidMapping.insert( std::pair<uint32_t,uint32_t>(flowId, p) );
  //     //   if (headRoomQueueScheme == FQ) {
  //     //     if (verbose) std::cout << Simulator::Now() << ": new flow, HR-FQ, flowid=" << flowId << ", MRqueueid=" << p << ", HRqueueid=" << nextAvailableHRqueueid << std::endl;
  //     //     flowidHRqueueidMapping.insert( std::pair<uint32_t,uint32_t>(flowId, nextAvailableHRqueueid) );
  //     //     p = nextAvailableHRqueueid;
  //     //     nextAvailableHRqueueid++;
  //     //   } else if (headRoomQueueScheme == CCA) {
  //     //     if (ccaHRqueueidMapping.find(p) == ccaHRqueueidMapping.end()) {
  //     //       ccaHRqueueidMapping.insert( std::pair<uint32_t,uint32_t>(p,nextAvailableHRqueueid) );
  //     //       nextAvailableHRqueueid++;
  //     //     }
  //     //     if (verbose) std::cout << Simulator::Now() << ": new flow, HR-CCA, flowid=" << flowId << ", MRqueueid=" << p << ", HRqueueid=" << ccaHRqueueidMapping[p] << std::endl;
  //     //     flowidHRqueueidMapping.insert( std::pair<uint32_t,uint32_t>(flowId, ccaHRqueueidMapping[p]) );
  //     //     p = ccaHRqueueidMapping[p];
  //     //   } else if (headRoomQueueScheme == RTT) {
  //     //     uint32_t HRqueueid = p+mainRoomNumQueues; // p already has that +1 in it, thus using mainRoomNumQueues here instead of nextAvailableHRqueueid
  //     //     if (verbose) std::cout << Simulator::Now() << ": new flow, HR-RTT, flowid=" << flowId << ", MRqueueid=" << p << ", HRqueueid=" << HRqueueid << std::endl;
  //     //     flowidHRqueueidMapping.insert( std::pair<uint32_t,uint32_t>(flowId, HRqueueid) );
  //     //     p = HRqueueid;
  //     //   } else if (headRoomQueueScheme == SQ) {
  //     //     uint32_t HRqueueid = nextAvailableHRqueueid;
  //     //     if (verbose) std::cout << Simulator::Now() << ": new flow, HR-SQ, flowid=" << flowId << ", MRqueueid=" << p << ", HRqueueid=" << HRqueueid << std::endl;
  //     //     flowidHRqueueidMapping.insert( std::pair<uint32_t,uint32_t>(flowId, HRqueueid) );
  //     //     p = HRqueueid;
  //     //   }

  //     //   sharedMemory->setStatus(sharedMemory->getProberId(portId,p),flowId,HREntering);
  //     // } else {
  //     //   uint32_t HRproberid = sharedMemory->getProberId(portId, flowidHRqueueidMapping[flowId]);
  //     //   uint32_t HRflowStatus = sharedMemory->getStatus(HRproberid,flowId);
  //     //   uint32_t MRproberid = sharedMemory->getProberId(portId, flowidMRqueueidMapping[flowId]);
  //     //   uint32_t MRflowStatus = sharedMemory->getStatus(MRproberid,flowId);

  //     //   // check whether this is a retransmitted packet
  //     //   bool isRetransmission = false;
  //     //   if (MRflowStatus==StatusNone) {
  //     //     // flow never went into MR
  //     //     if (HRflowStatus==HRFlowEnd or HRflowStatus==HRClearPackets) isRetransmission = true;
  //     //   } else {
  //     //     // flow latest in MR
  //     //     if (MRflowStatus==MRFlowEnd or MRflowStatus==MRClearPackets) isRetransmission = true;
  //     //   }

  //     //   if (isRetransmission) {
  //     //     if (verbose) std::cout << Simulator::Now() << ": retransmission, flowid=" << flowId << ", HRproberid=" << HRproberid << ", HRflowStatus=" << HRflowStatus << ", MRproberid=" << MRproberid << ", MRflowStatus=" << MRflowStatus << std::endl;
  //     //     // p = flowidHRqueueidMapping[flowId];
  //     //     // removeFromFlowIdSeen(flowId);
  //     //     // sharedMemory->setStatus(HRproberid,flowId,HREntering);
  //     //     // sharedMemory->setStatus(MRproberid,flowId,StatusNone);
  //     //     p = 0;
  //     //   } else {
  //     //     // not retransmission
  //     //     if (HRflowStatus == HRFlowEnd) {
  //     //       // flow is in MR
  //     //       if (p != flowidMRqueueidMapping[flowId]) std::cout << "ERROR: p!=MRqueueid, p=" << p << ", MRqueueid=" << flowidMRqueueidMapping[flowId] << std::endl;
  //     //     } else if (HRflowStatus == HREntering) {
  //     //       // flow is in HR
  //     //       p = flowidHRqueueidMapping[flowId]; 
  //     //     }
  //     //   }
  //     // }
  //     // a.SetPriority(p);
  //   }
  // }

  uint32_t proberId = sharedMemory->getProberId(portId, p);

  /* Arrival Statistics*/
  numBytesSent[p]+=item->GetSize();
  uint64_t sizenow = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
  if (bufferMax[p] < sizenow){
    bufferMax[p]=sizenow;
  }

  /*Check if we can use the reserved space*/
  // if (GetCurrentSize().GetValue() + item->GetSize() <= staticBuffer){
  //   bool ret = GetQueueDiscClass (p)->GetQueueDisc ()->Enqueue (item);

  //   if(firstSeen[p]==Seconds(0)){
  //     firstSeen[p]=Simulator::Now();
  //   }
  //   lastAccepted[p]=Simulator::Now();
  //   if (isMyBM) {
  //     sharedMemory->setQSize(proberId, GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes());
  //     uint32_t currBuffer = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
  //     if (currBuffer < sharedMemory->probeMinMinBufferUsed[proberId]) sharedMemory->probeMinMinBufferUsed[proberId] = currBuffer;
  //     if (currBuffer > sharedMemory->probeMinMaxBufferUsed[proberId]) sharedMemory->probeMinMaxBufferUsed[proberId] = currBuffer;
  //   }
  //   return ret;
  // }

  /*Check if the packet can be put in the shared buffer*/
  bool enqueue = AcceptPacket(p,packet);
  if (!enqueue) {

      NS_LOG_LOGIC ("Queue disc limit exceeded -- dropping packet");
      // std::cout << " maxSize " << maxSize << " remaining " << sharedMemory->GetRemainingBuffer() << " packetSize " << item->GetSize() << " priority " << uint32_t(p) << " alpha " << alphas[p] << " thresh " << uint64_t (alphas[p]*(sharedMemory->GetRemainingBuffer())) << " deq " << DeqRate[p] << " N " << sharedMemory->GetNofP(p) << std::endl;

      droppedBytes[p]+=item->GetSize();
      if (isMyBM) startWindowAfterDrop(p);

      if (isMyBM) {
        sharedMemory->probeMinTotalDropBytes[proberId] += item->GetSize();
        // std::map<int64_t,uint32_t>::iterator it1;
        // for (it1=sharedMemory->probeMinTotalDropBytesMonitorMap[proberId].begin(); it1!=sharedMemory->probeMinTotalDropBytesMonitorMap[proberId].end(); it1++) {
        //     it1->second += item->GetSize();
        // }

        // if ((not sharedMemory->isProberInHeadRoom(proberId)) and (not sharedMemory->isProberInWaitRoom(proberId)) and (sharedMemory->getDoMonitorDrop(proberId))) {
        //   uint32_t qSize = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
        //   probeMinMonitorDropInvoke(proberId,qSize);
        // }
        // if (debug) sharedMemory->debugTotalDropBytes[proberId] += item->GetSize();
      }

      DropBeforeEnqueue (item, LIMIT_EXCEEDED_DROP);
      if (isMyBM) {
        // sharedMemory->setQSize(proberId, GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes());
        uint32_t currBuffer = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
        if (currBuffer < sharedMemory->probeMinMinBufferUsed[proberId]) sharedMemory->probeMinMinBufferUsed[proberId] = currBuffer;
        if (currBuffer > sharedMemory->probeMinMaxBufferUsed[proberId]) sharedMemory->probeMinMaxBufferUsed[proberId] = currBuffer;
      }
      return false;
  }

  if (isMyBM) {
    uint32_t currBuffer = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
    // if (currBuffer < sharedMemory->probeMinMinBufferUsed[proberId]) {
    //   sharedMemory->probeMinMinBufferUsed[proberId] = currBuffer;
    // }

    // std::map<int64_t,uint32_t>::iterator it1;
    // for (it1=sharedMemory->probeMinMinBufferUsedMonitorMap[proberId].begin(); it1!=sharedMemory->probeMinMinBufferUsedMonitorMap[proberId].end(); it1++) {
    //   if (currBuffer < it1->second) {
    //     it1->second = currBuffer;
    //   }
    // }

    if (currBuffer == 0) {
      sharedMemory->probeMinPacketCountZeroQueue[proberId]+=1;
    }
    sharedMemory->probeMinPacketCountTotal[proberId]+=1;
    int64_t now = Simulator::Now().GetMicroSeconds();
    if (currBuffer == 0) {
      if (sharedMemory->probeMinLastTimestampNonZeroQueue[proberId] > -1) {
        sharedMemory->probeMinDurationZeroQueue[proberId] += now - sharedMemory->probeMinLastTimestampNonZeroQueue[proberId];
      }
    }

    // if (currBuffer == 0) {
    //   int64_t lastTimestampNonZeroQueue = sharedMemory->probeMinLastTimestampNonZeroQueue[proberId];
    //   if (lastTimestampNonZeroQueue > -1) {
    //     std::map<int64_t,int64_t>::iterator it2;
    //     for (it2=sharedMemory->probeMinDurationZeroQueueMonitorMap[proberId].begin(); it2!=sharedMemory->probeMinDurationZeroQueueMonitorMap[proberId].end(); it2++) {
    //       if (lastTimestampNonZeroQueue < it2->first) {
    //         it2->second += now - it2->first;
    //       } else{
    //         it2->second += now - lastTimestampNonZeroQueue;
    //       }
    //     }
    //   }
    // }

    sharedMemory->probeMinLastTimestampNonZeroQueue[proberId] = now; // since I'm going to enqueue 1 packet later
    // if (debug) {
    //   if (currBuffer < sharedMemory->debugMinBufferUsed[proberId]) {
    //     sharedMemory->debugMinBufferUsed[proberId] = currBuffer;
    //   }
    // }
  
    currBuffer += item->GetSize();
    // if (currBuffer > sharedMemory->probeMinMaxBufferUsed[proberId]) {
    //   sharedMemory->probeMinMaxBufferUsed[proberId] = currBuffer;
    // }

    // for (it1=sharedMemory->probeMinMaxBufferUsedMonitorMap[proberId].begin(); it1!=sharedMemory->probeMinMaxBufferUsedMonitorMap[proberId].end(); it1++) {
    //   if (currBuffer > it1->second) {
    //     it1->second = currBuffer;
    //   }
    // }

    // if (debug) {
    //   if (currBuffer > sharedMemory->debugMaxBufferUsed[proberId]) {
    //     sharedMemory->debugMaxBufferUsed[proberId] = currBuffer;
    //   }
    // }
  }

  /*If algorithm is Intelligent Buffer, it may change the queue to zero (DPP prioritizes short flows to separate queue)*/
  if (bufferalg==IB && enableDPPQueue){
    p = DPPQueue;
  }

  /*increment shared buffer occupancy*/
  bool retval;
  bool hasenqueuedbuffer = true;
  if(!sharedMemory->EnqueueBuffer(item->GetSize())) {
    droppedBytes[p]+=item->GetSize();
    if (isMyBM) startWindowAfterDrop(p);
    DropBeforeEnqueue (item, LIMIT_EXCEEDED_DROP);
    retval = false;
    hasenqueuedbuffer = false;
    // std::cout << "BM drop" << std::endl;
  }
  else{
    sharedMemory->PerPriorityStatEnq(item->GetSize(),p);
    retval = GetQueueDiscClass (p)->GetQueueDisc ()->Enqueue (item);
    // AnnC: AQM could drop packets here
    // if (!retval) std::cout << "AQM drop" << std::endl;
  }

  if (!retval)
    {
      // if (item->GetIsDroppedByCodel()) {
      //   droppedBytes[p]+=item->GetSize(); // AnnC: there's a DropBeforeEnqueue in CoDelQueueDisc::DoEnqueue
      // } else{

      // AnnC: with PieQueueDisc, we could see DropBeforeEnqueue
      // NS_LOG_WARN ("Packet enqueue failed. Check the size of the internal queues");
      if (hasenqueuedbuffer) {
        droppedBytes[p]+=item->GetSize();
        if (isMyBM) startWindowAfterDrop(p); // AnnC: this should never happen
        sharedMemory->DequeueBuffer(item->GetSize());
        sharedMemory->PerPriorityStatDeq(item->GetSize(),p);
      }

      // AnnC: log the drops due to AQM
      // sharedMemory->DequeueBuffer(item->GetSize());
      // droppedBytes[p]+=item->GetSize();

      // if (isMyBM) {
      //   sharedMemory->probeMinTotalDropBytes[proberId] += item->GetSize();
      // // }
      // // DropBeforeEnqueue (item, LIMIT_EXCEEDED_DROP);
      // // if (isMyBM) {
      //   sharedMemory->setQSize(proberId, GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes());
      //   uint32_t currBuffer = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
      //   if (currBuffer < sharedMemory->probeMinMinBufferUsed[proberId]) sharedMemory->probeMinMinBufferUsed[proberId] = currBuffer;
      //   if (currBuffer > sharedMemory->probeMinMaxBufferUsed[proberId]) sharedMemory->probeMinMaxBufferUsed[proberId] = currBuffer;
      // }
      // }
    }
  else{
    if(firstSeen[p]==Seconds(0)){
      firstSeen[p]=Simulator::Now();
    }
    lastAccepted[p]=Simulator::Now();

    if (isMyBM) {
      uint32_t qSize = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
      // qSize += item->GetSize(); // the actual enqueue happens after this // AnnC: cannot have this line
      // std::cout << Simulator::Now() << "," << proberId << ",Enqueue," << qSize << std::endl;
      if (sharedMemory->designZeroStart[proberId]!=-1) {
        if (qSize==0) std::cout << "**Error: DoEnqueue, proberId=" << proberId << ", qSize should be >0 since we enqueued packet, qSize=" << qSize << std::endl;
        if (qSize!=0) {
          // std::cout << "TempLog," << proberId << "," << Simulator::Now().GetNanoSeconds()-sharedMemory->designZeroStart[proberId] << std::endl;
          sharedMemory->designZeroVec[proberId].push_back(Simulator::Now().GetNanoSeconds()-sharedMemory->designZeroStart[proberId]);
          sharedMemory->designZeroWindowSum[proberId] += Simulator::Now().GetNanoSeconds()-std::max(sharedMemory->designZeroStart[proberId],sharedMemory->designZeroWindowStart[proberId]*1000);
          sharedMemory->designZeroTimestamp[proberId].push_back(sharedMemory->designZeroStart[proberId]);
          sharedMemory->designZeroStart[proberId] = -1;
        }
      }
    }
  }

  NS_LOG_LOGIC ("Number packets p " << p << ": " << GetQueueDiscClass (p)->GetQueueDisc ()->GetNPackets ());

  if (isMyBM) {
    // sharedMemory->setQSize(proberId, GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes());
    uint32_t currBuffer = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
    if (currBuffer < sharedMemory->probeMinMinBufferUsed[proberId]) sharedMemory->probeMinMinBufferUsed[proberId] = currBuffer;
    if (currBuffer > sharedMemory->probeMinMaxBufferUsed[proberId]) sharedMemory->probeMinMaxBufferUsed[proberId] = currBuffer;
  }
  return retval;
}

// std::pair<double,double>
std::vector<double>
GenQueueDisc::GetThroughputQueue(uint32_t p, double nanodelay){
    double tmp = numBytesSentQueue[p];
    double th = 8*numBytesSentQueue[p]/nanodelay/portBW;
    numBytesSentQueue[p]=0;
    std::vector<double> result;
    result.push_back(th);
    result.push_back(th/8*nanodelay*portBW);
    // return std::pair(th,tmp);
    // std::cout << "GetThroughputQueue " << th << " " << tmp << " " << th/8*nanodelay*portBW << std::endl;
    return result;
}

double
GenQueueDisc::GetThroughputPort(double nanodelay){ // delay must be in nanoseconds
    double th = 8*numBytesSentQueue[107]/nanodelay/portBW;
    numBytesSentQueue[107]=0;
    return th;
}

Ptr<QueueDiscItem>
GenQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<QueueDiscItem> item;

  /* Round robin scheduling. Nothing fancy here. More scheduling algorithms to be added later. */
  if (round_robin){
    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
      {
        if ((item = GetQueueDiscClass (dequeueIndex)->GetQueueDisc ()->Dequeue ()) != 0)
          {

            Ptr<Packet> packet = item->GetPacket();

            uint32_t p = dequeueIndex;

            uint8_t countIsDroppedByCodel = item->GetIsDroppedByCodel();
            uint8_t countIsDequeuedByCodel = item->GetIsDequeuedByCodel();
            if (countIsDroppedByCodel>0) {
              // CoDel
              droppedBytes[p]+=(item->GetSize())*countIsDroppedByCodel; 
              if (isMyBM) startWindowAfterDrop(p); // AnnC: this should never happen
              if (countIsDequeuedByCodel>0) {
                numBytesSentQueue[p]+=item->GetSize();

                // 10 is used for aggregate. Assuming that the actual number of queues are less than 10. // AnnC: 107 now
                numBytesSentQueue[107]+=item->GetSize();
              }
            } else {
              // Non-CoDel
              numBytesSentQueue[p]+=item->GetSize();

              // 10 is used for aggregate. Assuming that the actual number of queues are less than 10. // AnnC: 107 now
              numBytesSentQueue[107]+=item->GetSize();
            }

            Deq[p]+=item->GetSize();

            uint32_t proberId = sharedMemory->getProberId(portId, p);
            if (isMyBM) {
              sharedMemory->probeMinAverageThroughput[proberId] += item->GetSize();
              // if (debug) sharedMemory->debugAverageThroughput[proberId] += item->GetSize();
            }

            if (GetCurrentSize().GetValue() + packet->GetSize() > staticBuffer){
              if (countIsDequeuedByCodel>0) {
                // CoDel
                sharedMemory->DequeueBuffer((item->GetSize())*countIsDequeuedByCodel);
                sharedMemory->PerPriorityStatDeq((item->GetSize())*countIsDequeuedByCodel,p);
              } else {
                // Non-CoDel
                sharedMemory->DequeueBuffer(item->GetSize());
                sharedMemory->PerPriorityStatDeq(item->GetSize(),p);
              }
            }

            dequeueIndex++;
            if (dequeueIndex>=GetNQueueDiscClasses())
              dequeueIndex=0;

            IntHeader Int;
            bool found;
            found = packet->PeekPacketTag(Int);
            if(found){
              Int.setTelemetryQlenDeq(Int.getHopCount(), GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes()); // queue length at dequeue
              Int.setTelemetryTsDeq(Int.getHopCount(), Simulator::Now().GetNanoSeconds()); // timestamp at dequeue
              Int.setTelemetryBw(Int.getHopCount(), portBW*1e9);
              Int.setTelemetryTxBytes(Int.getHopCount(), txBytesInt);
              Int.incrementHopCount(); // Incrementing hop count at Dequeue. Don't do this at enqueue.
              packet->ReplacePacketTag(Int); // replacing the tag with new values
              // std::cout << "found " << Int.getHopCount() << std::endl;
            }
            txBytesInt+=packet->GetSize();
            if (isMyBM) {
              uint32_t qSize = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
              // if (qSize > 0) qSize -= item->GetSize(); // the actual dequeue happens after this; it can happen that there's nothing to dequeue // AnnC: cannot have this line
              // std::cout << Simulator::Now() << "," << proberId << ",Dequeue," << qSize << std::endl;
              if (sharedMemory->designZeroStart[proberId]!=-1) std::cout << "**Error: DoDequeue, proberId=" << proberId << ", designZeroStart should be -1 since we had packet, designZeroStart=" << sharedMemory->designZeroStart[proberId] << std::endl;;
              if (sharedMemory->designZeroStart[proberId]==-1 && qSize==0) {
                sharedMemory->designZeroStart[proberId] = Simulator::Now().GetNanoSeconds();
              }

              // sharedMemory->setQSize(proberId, qSize);
              uint32_t currBuffer = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
              if (currBuffer < sharedMemory->probeMinMinBufferUsed[proberId]) sharedMemory->probeMinMinBufferUsed[proberId] = currBuffer;
              if (currBuffer > sharedMemory->probeMinMaxBufferUsed[proberId]) sharedMemory->probeMinMaxBufferUsed[proberId] = currBuffer;

              if (currBuffer==0) {
                int64_t now = Simulator::Now().GetMicroSeconds();
                sharedMemory->probeMinLastTimestampNonZeroQueue[proberId] = now-1;
              }

              bool foundFid;
              uint32_t flowId = 0;
              FlowIdTag tag;
              foundFid = packet->PeekPacketTag (tag);
              if(foundFid){
                flowId=tag.GetFlowId();
                // if (sharedMemory->isFlowEnded(flowId)) {
                // if (sharedMemory->isFlowIdByProberIdEnded(flowId,proberId)) { 
                // if (sharedMemory->getStatus(proberId,flowId) == HRFlowEnd) { // AnnC: running under the assumption for now that long flows do not end; will add support for short flows later
                //   // sharedMemory->adjustHeadRoomForFlowIdEnded(proberId); // may not be able to adjust?
                //   sharedMemory->allocateBufferSpace(proberId, 0);
                //   // if (qSize == 0) {
                //   //   sharedMemory->setStatus(proberId,flowId,HRClearPackets);
                //   //   // all packets from this flow have been sent out
                //   //   // sharedMemory->removeFromProberInHeadRoom(proberId);
                //   //   // sharedMemory->setMinBufferThreshold(proberId, 0);
                //   // }
                //   bool shouldSetHRClearPackets = true;
                //   std::map<uint32_t,uint32_t> m = sharedMemory->statusTracker[proberId];
                //   std::map<uint32_t,uint32_t>::iterator it;
                //   for (it=m.begin(); it!=m.end(); it++) {
                //     if (it->second==HREntering) {
                //       shouldSetHRClearPackets = false;
                //       break;
                //     }
                //   }
                //   if (shouldSetHRClearPackets and qSize==0) {
                //     for (it=m.begin(); it!=m.end(); it++) {
                //       it->second = HRClearPackets;
                //     }
                //   }
                // }
                // if (sharedMemory->getStatus(proberId,flowId) == MRFlowEnd) { 
                //   sharedMemory->allocateBufferSpace(proberId, 0);
                //   // if (qSize == 0) sharedMemory->setStatus(proberId,flowId,MRClearPackets);
                //   bool shouldSetMRClearPackets = true;
                //   std::map<uint32_t,uint32_t> m = sharedMemory->statusTracker[proberId];
                //   std::map<uint32_t,uint32_t>::iterator it;
                //   for (it=m.begin(); it!=m.end(); it++) {
                //     if (it->second==MRWaitRoom or it->second==MRProbing) {
                //       shouldSetMRClearPackets = false;
                //       break;
                //     }
                //   }
                //   if (shouldSetMRClearPackets and qSize==0) {
                //     for (it=m.begin(); it!=m.end(); it++) {
                //       it->second = MRClearPackets;
                //     }
                //   }
                // }
              } else {
                std::cout << "dodequeue flowId not found: p=" << p << std::endl;
              }
            }
            return item;
          }
        Deq[dequeueIndex]+=1472;

        // probeMinAverageThroughput[dequeueIndex] += 1472;

        dequeueIndex++;
        if (dequeueIndex>=GetNQueueDiscClasses())
          dequeueIndex=0;
      }
  }
  else{
    /*Strict priority scheduling*/
    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
      {
        if ((item = GetQueueDiscClass (i)->GetQueueDisc ()->Dequeue ()) != 0)
          {

            Ptr<Packet> packet = item->GetPacket();

            uint32_t p = i;

            uint8_t countIsDroppedByCodel = item->GetIsDroppedByCodel();
            uint8_t countIsDequeuedByCodel = item->GetIsDequeuedByCodel();
            if (countIsDroppedByCodel) {
              // CoDel
              droppedBytes[p]+=(item->GetSize())*countIsDroppedByCodel;
              if (isMyBM) startWindowAfterDrop(p); // AnnC: this should never happen
              if (countIsDequeuedByCodel>0) {
                numBytesSentQueue[p]+=item->GetSize();

                // 10 is used for aggregate. Assuming that the actual number of queues are less than 10.
                numBytesSentQueue[107]+=item->GetSize();
              }
            } else {
              // Non-CoDel
              numBytesSentQueue[p]+=item->GetSize();

              // 10 is used for aggregate. Assuming that the actual number of queues are less than 10.
              numBytesSentQueue[107]+=item->GetSize();
            }

            Deq[p]+=item->GetSize();

            uint32_t proberId = sharedMemory->getProberId(portId, p);
            if (isMyBM) {
              sharedMemory->probeMinAverageThroughput[proberId] += item->GetSize();
              // if (debug) sharedMemory->debugAverageThroughput[proberId] += item->GetSize();
            }

            if (GetCurrentSize().GetValue() + packet->GetSize() > staticBuffer){
              if (countIsDequeuedByCodel>0) {
                // CoDel
                sharedMemory->DequeueBuffer((item->GetSize())*countIsDequeuedByCodel);
                sharedMemory->PerPriorityStatDeq((item->GetSize())*countIsDequeuedByCodel,p);
              } else {
                // Non-CoDel
                sharedMemory->DequeueBuffer(item->GetSize());
                sharedMemory->PerPriorityStatDeq(item->GetSize(),p);
              }
            }

            IntHeader Int;
            bool found;
            found = packet->PeekPacketTag(Int);
            if(found){
              Int.setTelemetryQlenDeq(Int.getHopCount(), GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes()); // queue length at dequeue
              Int.setTelemetryTsDeq(Int.getHopCount(), Simulator::Now().GetNanoSeconds()); // timestamp at dequeue
              Int.setTelemetryBw(Int.getHopCount(), portBW*1e9);
              Int.setTelemetryTxBytes(Int.getHopCount(), txBytesInt);
              Int.incrementHopCount(); // Incrementing hop count at Dequeue. Don't do this at enqueue.
              packet->ReplacePacketTag(Int); // replacing the tag with new values
              // std::cout << "found " << Int.getHopCount() << std::endl;
            }
            txBytesInt+=packet->GetSize();
            if (isMyBM) {
              uint32_t qSize = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
              // sharedMemory->setQSize(proberId, qSize);
              uint32_t currBuffer = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
              if (currBuffer < sharedMemory->probeMinMinBufferUsed[proberId]) sharedMemory->probeMinMinBufferUsed[proberId] = currBuffer;
              if (currBuffer > sharedMemory->probeMinMaxBufferUsed[proberId]) sharedMemory->probeMinMaxBufferUsed[proberId] = currBuffer;

              bool foundFid;
              uint32_t flowId = 0;
              FlowIdTag tag;
              foundFid = packet->PeekPacketTag (tag);
              if(foundFid){
                flowId=tag.GetFlowId();
                // AnnC: leave out this part for now
                // if (sharedMemory->isFlowEnded(flowId)) {
                // if (sharedMemory->isFlowIdByProberIdEnded(flowId,proberId)) {                  
                //   sharedMemory->adjustHeadRoomForFlowIdEnded(proberId);
                //   if (qSize == 0) {
                //     // all packets from this flow have been sent out
                //     sharedMemory->removeFromProberInHeadRoom(proberId);
                //     // sharedMemory->setMinBufferThreshold(proberId, 0);
                //   }
                // }
              } else {
                std::cout << "dodequeue flowId not found" << std::endl;
              }
            }
            return item;
          }
        Deq[i]+=1472;

        // probeMinAverageThroughput[i] += 1472;
      }
  }
  NS_LOG_LOGIC ("Queue empty");
  return item;
}

Ptr<const QueueDiscItem>
GenQueueDisc::DoPeek (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<const QueueDiscItem> item;

  for (uint32_t i = 0; i < GetNQueueDiscClasses (); i++)
    {
      if ((item = GetQueueDiscClass (i)->GetQueueDisc ()->Peek ()) != 0)
        {
          NS_LOG_LOGIC ("Peeked from band " << i << ": " << item);
          NS_LOG_LOGIC ("Number packets band " << i << ": " << GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets ());
          return item;
        }
    }

  NS_LOG_LOGIC ("Queue empty");
  return item;
}

bool
GenQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNInternalQueues () > 0)
    {
      NS_LOG_ERROR ("GenQueueDisc cannot have internal queues");
      return false;
    }

  if (GetNQueueDiscClasses () == 0)
    {
      // create 3 fifo queue discs
      ObjectFactory factory;
      factory.SetTypeId ("ns3::FifoQueueDisc");
      for (uint8_t i = 0; i < 2; i++)
        {
          Ptr<QueueDisc> qd = factory.Create<QueueDisc> ();
          qd->Initialize ();
          Ptr<QueueDiscClass> c = CreateObject<QueueDiscClass> ();
          c->SetQueueDisc (qd);
          AddQueueDiscClass (c);
        }
    }

  if (GetNQueueDiscClasses () < 2)
    {
      NS_LOG_ERROR ("GenQueueDisc needs at least 2 classes");
      return false;
    }

  return true;
}

void
GenQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);
}

bool GenQueueDisc::isNewFlow(uint32_t flowId) {
	if (flowIdSeen.find(flowId) != flowIdSeen.end()) {
    return false;
  }
  flowIdSeen.insert(flowId);
  return true;
}

bool GenQueueDisc::isNewProber(uint32_t proberId) {
	if (proberStarted.find(proberId) != proberStarted.end()) {
    return false;
  }
  proberStarted.insert(proberId);
  return true;
}

bool GenQueueDisc::MyBM(uint32_t priority, Ptr<Packet> packet, uint32_t bmType){
  isMyBM = true;

  // std::cout << "****Debug: MyBM" << std::endl;

  uint8_t flag = 0;
  PacketMetadata::ItemIterator mdit = packet->BeginItem();
  PacketMetadata::Item item;
  while (mdit.HasNext()) {
    item = mdit.Next();
    if (item.tid.GetName() == "ns3::TcpHeader") {
      NS_ASSERT (item.tid.HasConstructor ());
      Callback<ObjectBase *> constructor = item.tid.GetConstructor ();
      NS_ASSERT (!constructor.IsNull ());
      ObjectBase *instance = constructor ();
      NS_ASSERT (instance != 0);
      TcpHeader *tcpHeader = dynamic_cast<TcpHeader *> (instance);
      NS_ASSERT (tcpHeader != 0);
      tcpHeader->Deserialize(item.current);

      flag = tcpHeader->GetFlags();
      // std::cout << "############flag=" << (uint32_t)flag << ", portid=" << portId << ", priority=" << priority << std::endl;
      break;
    }
  }

  // std::cout << "****Debug: 0" << std::endl;

  // handle control packets (priority 0)
  if (priority == 0) { 
    if (sharedMemory->GetRemainingBuffer() < packet->GetSize()){
      return false;
    }
    return true;
  }

  // std::cout << "****Debug: 1" << std::endl;

  /* Find flow-id if exists */
  bool found;
  uint32_t flowId = 0;
  FlowIdTag tag;
  found = packet->PeekPacketTag (tag);
  if(found){
    flowId=tag.GetFlowId();
  } else {
    std::cout << "mybm flowId not found" << std::endl;
  }

  // std::cout << "****Debug: 2" << std::endl;

  uint32_t proberId = sharedMemory->getProberId(portId, priority);

  // std::cout << "flag=" << (uint32_t)flag << ", proberid=" << proberId << ", flowid=" << flowId << "***********Debug" << std::endl;

  // if (flag==17 and priority>0) {
  //   if (verbose) std::cout << Simulator::Now() << ": flowend, proberid=" << proberId << ", portid=" << portId << ", priority=" << priority << ", flowid=" << flowId << ", flag=17" << std::endl;
  //   // sharedMemory->addToFlowIdEnded(flowId);
  //   // sharedMemory->addToFlowIdByProberIdEnded(flowId,proberId);
  //   if (sharedMemory->getStatus(proberId,flowId)==HREntering) {
  //     sharedMemory->setStatus(proberId,flowId,HRFlowEnd);
  //   } else {
  //     if (sharedMemory->getStatus(proberId,flowId)!=MRWaitRoom and sharedMemory->getStatus(proberId,flowId)!=MRProbing) std::cout << "ERROR: status should be MRWaitRoom or MRProbing, status=" << sharedMemory->getStatus(proberId,flowId) << std::endl;
  //     sharedMemory->setStatus(proberId,flowId,MRFlowEnd);
  //   }
  // }

  // std::cout << "****Debug: 3" << std::endl;

  // if (isNewFlow(flowId)) {
  //   setUpHeadRoomNonProber(priority, flowId, bmType);
  // }
  // if (isNewProber(proberId)) startProbing(proberId, flowId, bmType);
  uint32_t proberid = sharedMemory->getProberId(portId, priority);
  if (isNewProber(proberId)) sharedMemory->allocateBufferSpaceSimple(proberid, startProbeBuffer);

  uint32_t maxSize = sharedMemory->getCurrMaxSizeAllowed(proberId);
  uint32_t instantaneousQSize = GetQueueDiscClass (priority)->GetQueueDisc ()->GetNBytes();
  uint32_t averageQSize = static_cast<uint32_t>(std::round(smoothGetAverageQlen(priority)));
  // AnnC: I want to make sure there is at least one more packet space for this packet to get in
  // Can be a dirty fix. But hopefully is ok.
  // I want to make sure that maxSize is at least 1 packet size more than qSize
  // if (flag==17 and priority>0) {
  //   if (maxSize < qSize+1500) maxSize = qSize+1500;
  // }

  // if (sharedMemory->getMinBufferThreshold(proberId) == 1) {
  //   int64_t now = Simulator::Now().GetMicroSeconds();
  //   uint32_t minCurrMaxSizeAllowed = 100000000;
  //   std::map<uint32_t,int64_t> myMap = sharedMemory->idealMinBufferUsedData[proberId];
  //   std::map<uint32_t,int64_t>::iterator it;
  //   for (it=myMap.begin(); it!=myMap.end(); it++) {
  //     if (it->second-now >= monitorlongNRTT*RTTms*1000) {
  //       minCurrMaxSizeAllowed = std::min(minCurrMaxSizeAllowed,it->first);
  //     }
  //   }
  //   if (minCurrMaxSizeAllowed < 100000000) {
  //     sharedMemory->setMinBufferThreshold(proberId,minCurrMaxSizeAllowed);
  //     if (verbose) std::cout << Simulator::Now() << ": setMinBufferThreshold, proberId=" << proberId << ", minBufferThreshold=" << minCurrMaxSizeAllowed << std::endl;
  //     // AnnC: I cannot simply set CMSA to minBufferThreshold here
  //   }
  // }

  // int64_t now = Simulator::Now().GetMicroSeconds();
  // int64_t CMSALastChanged = sharedMemory->getCurrMaxSizeAllowedLastChanged(proberId);
  // if (CMSALastChanged > 0) {
  //   int64_t duration = now - CMSALastChanged;
  //   if (duration >= aggregateNRTT*RTTms*1000 and not sharedMemory->isProberInHeadRoom(proberId) and not sharedMemory->isKeyInThroughputData(proberId,sharedMemory->getCurrMaxSizeAllowed(proberId))) {
      
  //     // std::cout << "***Debug: now=" << now << ", CMSALastChanged=" << CMSALastChanged << ", duration=" << duration << std::endl;

  //     uint32_t totaldropbytes = sharedMemory->probeMinTotalDropBytes[proberId];
  //     uint32_t totalsentbytes = sharedMemory->probeMinAverageThroughput[proberId];
  //     uint32_t packetcountzeroqueue = sharedMemory->probeMinPacketCountZeroQueue[proberId];
  //     uint32_t packetcounttotal = sharedMemory->probeMinPacketCountTotal[proberId];
  //     int64_t durationZeroQueue = sharedMemory->probeMinDurationZeroQueue[proberId];
  //     double droprate = totaldropbytes / (double)totalsentbytes;
  //     double throughput = 8.0*totalsentbytes/duration/(portBW/(nPrior-1))/1000.0;
  //     double packetratezeroqueue = packetcountzeroqueue / (double)packetcounttotal;
  //     double durationpctZeroQueue = durationZeroQueue / (double)duration;
  //     sharedMemory->probeMinTotalDropBytes[proberId] = 0;
  //     sharedMemory->probeMinAverageThroughput[proberId] = 0;
  //     sharedMemory->probeMinPacketCountZeroQueue[proberId] = 0;
  //     sharedMemory->probeMinPacketCountTotal[proberId] = 0;
  //     sharedMemory->probeMinDurationZeroQueue[proberId] = 0;
  //     sharedMemory->setThroughputData(proberId,maxSize,throughput);
  //     sharedMemory->setDroprateData(proberId,maxSize,droprate);
  //     sharedMemory->setPacketRateZeroQueueData(proberId,maxSize,packetratezeroqueue);
  //     sharedMemory->setPacketCountZeroQueueData(proberId,maxSize,packetcountzeroqueue);
  //     sharedMemory->setPacketCountTotalData(proberId,maxSize,packetcounttotal);
  //     sharedMemory->setDurationZeroQueueData(proberId,maxSize,durationpctZeroQueue);
  //   }
  // }

  int32_t remainingBuffer = sharedMemory->GetRemainingBuffer();
  // std::cout << "****Debug: remainingBuffer=" << remainingBuffer << ", qSize=" << qSize;
  if (not sharedMemory->isProberInHeadRoom(proberId)) remainingBuffer -= sharedMemory->getBurstReserve();
  if (remainingBuffer < 0) remainingBuffer = 0;
  // std::cout << ", burstReserve=" << sharedMemory->getBurstReserve() << ", remainingBuffer=" << remainingBuffer << ", maxSize=" << maxSize << ", packetSize=" << packet->GetSize() << std::endl;

  // std::cout << "***Debug: " << Simulator::Now() << ", proberid=" << proberId << ", iqSize=" << instantaneousQSize << ", aqSize=" << averageQSize << ", recordLen=" << smoothQlenRecord[priority].size() << ", maxSize=" << maxSize << ", remainingBuffer=" << sharedMemory->GetRemainingBuffer() << ", packetSize=" << packet->GetSize() << std::endl;

  // if ( ((qSize + packet->GetSize()) >  maxSize) || (remainingBuffer < packet->GetSize())  ){
  bool shouldDrop = false;
  if (pawMode.compare("paw")==0) {
    shouldDrop = remainingBuffer<packet->GetSize() || ((instantaneousQSize+packet->GetSize())>maxSize && averageQSize>maxSize);
  } else if (pawMode.compare("pa")==0) {
    shouldDrop = remainingBuffer<packet->GetSize() || ((instantaneousQSize+packet->GetSize())>maxSize && averageQSize>maxSize);
  } else if (pawMode.compare("aw")==0) {
    shouldDrop = remainingBuffer<packet->GetSize() || ((instantaneousQSize+packet->GetSize())>maxSize && averageQSize>maxSize);
  } else if (pawMode.compare("fixed")==0) {
    shouldDrop = remainingBuffer<packet->GetSize() || (instantaneousQSize+packet->GetSize())>maxSize;
  } else if (pawMode.compare("fixed_vary")==0) {
    for (const auto& entry : fixedVaryThresVec) {
      int64_t now = Simulator::Now().GetMicroSeconds();
      if (now > entry.first*1e6) {
        maxSize = entry.second*1e3;
      } else {
        break;
      }
    }
    shouldDrop = remainingBuffer<packet->GetSize() || (instantaneousQSize+packet->GetSize())>maxSize;
  } else if (pawMode.compare("p")==0) {
    shouldDrop = remainingBuffer<packet->GetSize() || (instantaneousQSize+packet->GetSize())>maxSize;
  }

  // std::cout << "test," << shouldDrop << "," << maxSize << "," << instantaneousQSize << "," << averageQSize << "," << remainingBuffer << std::endl;

  if (shouldDrop) {
    // std::cout << "***Debug: drop" << std::endl;
    // std::cout << Simulator::Now() << ": drop, proberid=" << proberId << ", flowid=" << flowId << ", maxSize=" << maxSize << ", qSize=" << qSize << ", remainingBuffer=" << remainingBuffer << " -- DEBUG***********" << std::endl;

    // if ( ((qSize + packet->GetSize()) <=  maxSize) ) {
    //   // std::cout << Simulator::Now() << ": drop, due to remainingbuffer, proberid=" << proberId << ", flowid=" << flowId << ", maxSize=" << maxSize << ", qSize=" << qSize << ", remainingBuffer=" << remainingBuffer << " -- DEBUG***********" << std::endl;
    //   // if (verbose) std::cout << Simulator::Now() << ": drop due to remainingbuffer, proberid=" << proberId << ", qSize=" << qSize << std::endl;
    //   // int64_t now = Simulator::Now().GetMicroSeconds();
    //   // sharedMemory->setCurrMaxSizeAllowedLastChanged(proberId,now);
    //   // sharedMemory->setMainRoomBeliefScalingToDefault = true;
    //   // sharedMemory->setEffectiveCurrMaxSizeAllowed(proberId,remainingBuffer);
    //   // sharedMemory->updateDropsDueToRemainingBuffer(proberId,false,true);
    //   if (not sharedMemory->isProberInHeadRoom(proberId)) {
    //     uint32_t absoluteRemainingBuffer = sharedMemory->GetRemainingBuffer();
    //     if (absoluteRemainingBuffer >= packet->GetSize()) {
    //       if (verbose) std::cout << Simulator::Now() << ": allow invasion into the headroom, proberid=" << proberId << ", absoluteRemainingBuffer=" << absoluteRemainingBuffer << ", effectiveCMSA=" << qSize << std::endl;
    //       sharedMemory->setEffectiveCurrMaxSizeAllowed(proberId,qSize);
    //       sharedMemory->invadedHeadRoom = true;
    //       return true;
    //     }
    //   }
    // } else {
    //   // std::cout << Simulator::Now() << ": drop, due to threshold, proberid=" << proberId << ", flowid=" << flowId << ", maxSize=" << maxSize << ", qSize=" << qSize << ", remainingBuffer=" << remainingBuffer  << " -- DEBUG***********" << std::endl;
    //   // sharedMemory->setEffectiveCurrMaxSizeAllowed(proberId,maxSize);
    //   // sharedMemory->updateDropsDueToRemainingBuffer(proberId,true,false);
    // }
    // sharedMemory->setEffectiveCurrMaxSizeAllowed(proberId,qSize);

    return false; // drop
  }
  else{
    // std::cout << Simulator::Now() << ": enqueue, proberid=" << proberId << ", flowid=" << flowId << ", maxSize=" << maxSize << ", qSize=" << qSize << ", remainingBuffer=" << remainingBuffer  << " -- DEBUG***********" << std::endl;
    // std::cout << "EnqueuePacket " << Simulator::Now().GetMicroSeconds() << std::endl;
    return true;
  }

}

// void GenQueueDisc::probeMinBuffer(uint32_t priority) {
//   uint32_t proberId = sharedMemory->getProberId(portId, priority);
//   int64_t now = Simulator::Now().GetMicroSeconds();
//   if (now - sharedMemory->getCurrMaxSizeAllowedLastChanged(proberId) < (aggregateNRTT+2)*RTTms*1000 or now - probeMinBufferLastChecked[priority] < (aggregateNRTT+2)*RTTms*1000) {
//     Simulator::Schedule(MilliSeconds(RTTms),&GenQueueDisc::probeMinBuffer,this,priority);
//     return;
//   }

//   uint32_t cmsa = sharedMemory->getCurrMaxSizeAllowed(proberId);
// 	uint64_t minbuffer = sharedMemory->probeMinMinBufferUsed[proberId];
//   uint64_t maxbuffer = sharedMemory->probeMinMaxBufferUsed[proberId];
//   uint64_t prevmaxbuffer = prevMaxBufferUsed[priority]; // AnnC: not sure whether this part is right
// 	if (verbose) std::cout << Simulator::Now() << ": probeMin, proberId=" << proberId << ", currmaxsizeallowed=" << cmsa << ", minbuffer=" << minbuffer << ", maxbuffer=" << maxbuffer << ", prevmaxbuffer=" << prevmaxbuffer << std::endl;
//   sharedMemory->probeMinMaxBufferUsed[proberId] = 0;
// 	sharedMemory->probeMinMinBufferUsed[proberId] = 100000000;
//   prevMaxBufferUsed[priority] = maxbuffer;
//   probeMinBufferLastChecked[priority] = Simulator::Now().GetMicroSeconds();

//   if (prevmaxbuffer == maxbuffer) {
//     maxBufferUnchangingCount[priority] += 1;
//   } else {
//     maxBufferUnchangingCount[priority] = 0;
//   }

//   uint32_t idealMinBuffer = sharedMemory->getAbsoluteMinBuffer();
//   if (minbuffer < idealMinBuffer-50) {
//     idealMinBufferProbeCount[priority] = 0;
//     sharedMemory->allocateBufferSpace(proberId, 1500);
//   } else if (minbuffer > idealMinBuffer+50) {
//     idealMinBufferProbeCount[priority] = 0;
//     if (cmsa >= sharedMemory->getAbsoluteMinBuffer() + 1500) {
//       sharedMemory->allocateBufferSpace(proberId, -1500);
//     }
//   } else {
//     idealMinBufferProbeCount[priority]+=1;
//   }

//   if (verbose) std::cout << Simulator::Now() << ": probeMin, proberid=" << proberId << ", idealMinBufferProbeCount=" << idealMinBufferProbeCount[priority] << ", maxBufferUnchangingCount=" << maxBufferUnchangingCount[priority] << std::endl;
//   if (idealMinBufferProbeCount[priority] == 5 or maxBufferUnchangingCount[priority] == 5) {
//     if (verbose) std::cout << "setMinBufferThreshold, proberId=" << proberId << ", minBufferThreshold=" << maxbuffer << std::endl;
//     sharedMemory->setMinBufferThreshold(proberId,maxbuffer);
//     uint32_t prevCMSA = sharedMemory->getCurrMaxSizeAllowed(proberId);
//     sharedMemory->setCurrMaxSizeAllowed(proberId, maxbuffer);
//     sharedMemory->checkChangeInCurrMaxSizeAllowed(proberId,prevCMSA,maxbuffer);
//   } else {
//     Simulator::Schedule(MilliSeconds(RTTms),&GenQueueDisc::probeMinBuffer,this,priority);
//   }
// }

// void GenQueueDisc::probeMinBufferSetUp(uint32_t priority) {
//   uint32_t proberId = sharedMemory->getProberId(portId, priority);
//   sharedMemory->probeMinMaxBufferUsed[proberId] = 0;
// 	sharedMemory->probeMinMinBufferUsed[proberId] = 100000000;
//   prevMaxBufferUsed[priority] = 0;
//   Simulator::Schedule(MilliSeconds(RTTms), &GenQueueDisc::probeMinBuffer, this, priority);
// }

bool is_full_throughput(uint32_t sent,uint64_t totalbw, double packetsize, double droprate, double drtarget) {
  if ((sent>=totalbw-2*packetsize) && (droprate<=drtarget)) return true;
  return false;
}

void GenQueueDisc::probeMinMonitorLongInvoke2(uint32_t proberid, uint64_t nextmoveus) {
  uint32_t queueid = proberid % nPrior;
  if (verbose) std::cout << Simulator::Now() << ": probeMinMonitorLongInvoke2, proberId=" << proberid << ", queueid=" << queueid << std::endl;
  sharedMemory->probeMinMaxBufferUsed[proberid] = 0;
  sharedMemory->probeMinTotalDropBytes[proberid] = 0;
  sharedMemory->probeMinAverageThroughput[proberid] = 0;
  sharedMemory->probeMinMinBufferUsed[proberid] = 1000000000;
  sharedMemory->designZeroWindowSum[proberid] = 0;
  int64_t now = Simulator::Now().GetMicroSeconds();
  sharedMemory->designZeroWindowStart[proberid] = now;

  Simulator::Schedule(MicroSeconds(nextmoveus), &GenQueueDisc::probeMinMonitorLongCollectSimple2, this, proberid, now, 0, 0, 0, 0);
}

// uint32_t prevprevmaxbuffer = 0;
// uint32_t prevmaxbuffer = 0;
uint32_t decrease_count = 0;
uint32_t decrease_periodicity = 0;
uint32_t decrease_curr_periodicity = 0;
void GenQueueDisc::probeMinMonitorLongCollectSimple2(uint32_t proberid, int64_t intstartus, double mean, double variance, uint64_t count, double margin_error_prev) {
  uint32_t queueid = proberid % nPrior;
  int64_t now = Simulator::Now().GetMicroSeconds();
  uint32_t drop = sharedMemory->probeMinTotalDropBytes[proberid];
  uint32_t maxbuffer = sharedMemory->probeMinMaxBufferUsed[proberid];
  uint32_t sent = sharedMemory->probeMinAverageThroughput[proberid];
  uint32_t minbuffer = sharedMemory->probeMinMinBufferUsed[proberid];
  int64_t zeroqueueduration = sharedMemory->designZeroWindowSum[proberid]/1000; // in ns->us
  int64_t zerostart = sharedMemory->designZeroStart[proberid];
  uint32_t cmsa = sharedMemory->getCurrMaxSizeAllowed(proberid);
  double droprate = drop/(double)sent;
  if (verbose) std::cout << Simulator::Now() << ": probeMinMonitorLongCollectSimple2, proberId=" << proberid << ", queueuid=" << queueid << ", intstartus=" << intstartus;
  if (verbose) std::cout << ", drop=" << drop << ", maxbuffer=" << maxbuffer << ", sent=" << sent << ", minbuffer=" << minbuffer << ", cmsa=" << cmsa << ", zeroqueueduration=" << zeroqueueduration << std::endl;

  bool design_mar1725_v0 = false;
  bool design_mar1725_v1 = false;
  bool design_mar1725_v2 = false;
  bool is_log = false;

  double MTU = 1500;
  uint32_t DEFAULT_MI = 200000;
  uint8_t SAFE_THRES = 2;
  uint8_t DECREASE_SIZE = 3;
  uint8_t DECREASE_WINDOW = 10;
  if (design_mar1725_v2) { // run per 200ms
    if (drop==0) {
      // decrease_curr_periodicity += 1;
      if (is_log) std::cout << Simulator::Now() << ",drop=0,decrease_curr_periodicity=" << decrease_curr_periodicity << std::endl;
      Simulator::Schedule(MicroSeconds(DEFAULT_MI), &GenQueueDisc::probeMinMonitorLongCollectSimple2, this, proberid, intstartus, 0, 0, 0, 0);
      return;
    } else {
      // decrease_periodicity = std::max(decrease_periodicity,decrease_curr_periodicity);
      // decrease_curr_periodicity = 0;
      if (is_log) std::cout << Simulator::Now() << ",drop=" << drop << ",zeroqueueduration=" << zeroqueueduration << ",decrease_periodicity=" << decrease_periodicity << ",decrease_curr_periodicity=" << decrease_curr_periodicity;
      if (zeroqueueduration==0) {
        uint32_t safepoint = DECREASE_SIZE*(maxbuffer-minbuffer);
        if (minbuffer > safepoint && decrease_count > DECREASE_WINDOW*(decrease_periodicity+1)) {
          decrease_count = 0;
          if (is_log) std::cout << ",decrease_count=" << decrease_count << ",maxbuffer=" << maxbuffer << ",minbuffer=" << minbuffer << ",safepoint=" << safepoint << ",intstartus=" << intstartus << ",now=" << now << std::endl;
          sharedMemory->allocateBufferSpaceSimple(proberid,(uint32_t)(cmsa+safepoint)/2-cmsa);
        } else if (minbuffer > SAFE_THRES*MTU) {
          decrease_count += 1;
          if (is_log) std::cout << ",decrease_count=" << decrease_count << ",maxbuffer=" << maxbuffer << ",minbuffer=" << minbuffer << ",safepoint=" << safepoint << ",intstartus=" << intstartus << ",now=" << now << std::endl;
          sharedMemory->allocateBufferSpaceSimple(proberid,-MTU);
        }
      } else {
        decrease_count = 0;
        int64_t window = now-intstartus;
        if (is_log) std::cout << ",decrease_count=" << decrease_count << ",maxbuffer=" << maxbuffer << ",minbuffer=" << minbuffer << ",intstartus=" << intstartus << ",now=" << now << std::endl;
        sharedMemory->allocateBufferSpaceSimple(proberid,(uint32_t)(((double)window/(window-zeroqueueduration))*cmsa-cmsa));
      }
    }
    probeMinMonitorLongInvoke2(proberid,DEFAULT_MI);
    return;
  }

  // uint32_t FITTING_TIME = 1000000;
  if (design_mar1725_v1) { // run per 100ms
    if (drop==0) {
      // probeMinMonitorLongInvoke2(proberid,DEFAULT_MI);
      // if (now-intstartus>FITTING_TIME && (abs(prevprevmaxbuffer-prevmaxbuffer)<=MTU && abs(prevmaxbuffer-maxbuffer)<=MTU)) {
      //   sharedMemory->allocateBufferSpaceSimple(proberid,maxbuffer+MTU-cmsa);
      //   probeMinMonitorLongInvoke2(proberid,DEFAULT_MI);
      // } else {
        Simulator::Schedule(MicroSeconds(DEFAULT_MI), &GenQueueDisc::probeMinMonitorLongCollectSimple2, this, proberid, intstartus, 0, 0, 0, 0);
      // }
      // prevprevmaxbuffer = prevmaxbuffer;
      // prevmaxbuffer = maxbuffer;
      return;
    } else {
      if (zeroqueueduration==0) {
        if (minbuffer > SAFE_THRES*MTU) sharedMemory->allocateBufferSpaceSimple(proberid,-MTU);
      } else {
        // sharedMemory->allocateBufferSpaceSimple(proberid,MTU*(uint16_t)((zeroqueueduration-1)/100000+1));
        int64_t window = now-intstartus;
        sharedMemory->allocateBufferSpaceSimple(proberid,(uint32_t)(((double)window/(window-zeroqueueduration))*cmsa-cmsa));
      }
    }
    probeMinMonitorLongInvoke2(proberid,DEFAULT_MI);
    return;
  }

  if (design_mar1725_v0) { // run per 1ms
    if (sent==0) {
      if (minbuffer!=1000000000) std::cout << "****ERROR: design_mar1725_v0, sent=" << sent << ", minbuffer=" << minbuffer << std::endl;
      probeMinMonitorLongInvoke2(proberid,DEFAULT_MI);
    } else {
      if (zeroqueueduration!=0) {
        if (minbuffer!=0) std::cout << "****ERROR: design_mar1725_v0, zeroqueueduration=" << zeroqueueduration << ", zerostart=" << zerostart << std::endl;
        if (maxbuffer+MTU>=cmsa) {
          sharedMemory->allocateBufferSpaceSimple(proberid,(int32_t)((double)(now-intstartus)/(now-intstartus-zeroqueueduration)*cmsa)-cmsa);
          std::cout << "now=" << now << ", intstart=" << intstartus << ", zeroqueueduration=" << zeroqueueduration << ", ratio=" << (double)(now-intstartus)/(now-intstartus-zeroqueueduration) << std::endl;
          probeMinMonitorLongInvoke2(proberid,DEFAULT_MI);
        } else {
          // larger oscillation, need larger window
          Simulator::Schedule(MicroSeconds(DEFAULT_MI), &GenQueueDisc::probeMinMonitorLongCollectSimple2, this, proberid, intstartus, 0, 0, 0, 0);
        }
      } else {
        if (maxbuffer>cmsa) {
          // queue still draining
          probeMinMonitorLongInvoke2(proberid,DEFAULT_MI);
        } else {
          double mean_prev = mean;
          count += 1;
          mean = (minbuffer + (count-1)*mean_prev)/count;
          std::cout << "count=" << count << ", mean=" << mean << ", minbuffer=" << minbuffer << ", mean_prev=" << mean_prev << std::endl;
          if (count<10) {
            Simulator::Schedule(MicroSeconds(DEFAULT_MI), &GenQueueDisc::probeMinMonitorLongCollectSimple2, this, proberid, intstartus, mean, variance, count, margin_error_prev);
          } else {
            if (std::abs(mean-mean_prev) <= 2*MTU) {
              std::cout << "small" << std::endl;
              sharedMemory->allocateBufferSpaceSimple(proberid,-minbuffer);
              probeMinMonitorLongInvoke2(proberid,DEFAULT_MI);
            } else if (std::abs(mean-mean_prev) >= 10*MTU) {
              std::cout << "large" << std::endl;
              probeMinMonitorLongInvoke2(proberid,1);
            } else {
              if ((now-intstartus)>=100000) {
                probeMinMonitorLongInvoke2(proberid,1);
              } else {
                // mean_prev = mean;
                Simulator::Schedule(MicroSeconds(DEFAULT_MI), &GenQueueDisc::probeMinMonitorLongCollectSimple2, this, proberid, intstartus, mean, variance, count, margin_error_prev);
              }
            }
          }
          // std::cout << "mean=" << mean << ", var=" << variance << ", count=" << count << std::endl;
          // double mean_prev = mean;
          // count += 1;
          // mean += (minbuffer-mean)/count;
          // variance += (minbuffer-mean_prev)*(minbuffer-mean)/count;
          // double t = (1.96+6.0/(count-1))/std::sqrt(1+2.0/(count-1));
          // double margin_error = std::sqrt(variance/count)*t;
          // std::cout << "mean=" << mean << ", var=" << variance << ", count=" << count << ", t=" << t << ", me=" << margin_error << std::endl;
          // std::cout << "margin_error_diff=" << margin_error-margin_error_prev << std::endl;
          // if (std::abs(margin_error-margin_error_prev) <= 2*MTU) {
          //   std::cout << "small" << std::endl;
          //   sharedMemory->allocateBufferSpaceSimple(proberid,-minbuffer);
          //   probeMinMonitorLongInvoke2(proberid,1000);
          // } else if (std::abs(margin_error-margin_error_prev) >= 10*MTU) {
          //   std::cout << "large" << std::endl;
          //   probeMinMonitorLongInvoke2(proberid,1);
          // } else {
          //   if ((now-intstartus)>=100000) {
          //     probeMinMonitorLongInvoke2(proberid,1);
          //   } else {
          //     margin_error_prev = margin_error;
          //     Simulator::Schedule(MicroSeconds(1000), &GenQueueDisc::probeMinMonitorLongCollectSimple2, this, proberid, intstartus, mean, variance, count, margin_error_prev);
          //   }
          // }
        }
      }
    }
  }
}

// for a single queue
std::deque<std::tuple<uint32_t,uint32_t>> data_points;
std::deque<std::tuple<uint32_t,uint32_t>> data_points_lat;
// std::set<uint32_t> full_thpt_buffer;
// std::set<uint32_t> nonfull_thpt_buffer;
uint32_t configbuffer = 1000000;
uint32_t stay_count = 0;
uint32_t prev_cmsa = 0;
uint32_t same_thpt_stay_count = 0;
uint32_t same_thpt_minbuffer = configbuffer;
uint32_t thpt_stay_count = 0;
uint32_t thpt_minbuffer = configbuffer;
std::vector<uint32_t> thpt_minbuffer_vec;
bool is_retest = false;
uint32_t retest_prev_sent = 0;
std::map<uint32_t,int64_t> retest_latest_time;
bool can_remove_start = true;
uint32_t move_by_thpt_increase_count = 0;
uint32_t move_by_thpt_decrease_count = 0;
uint32_t maxbuffer_sent_sum = 0;
uint32_t maxbuffer_sent_count = 0;
// for multiple queues, initialized in startProbing()
std::map<uint32_t,std::deque<std::tuple<uint32_t,uint32_t>>> data_points_mp;
std::map<uint32_t,uint32_t> prev_cmsa_mp;
std::map<uint32_t,uint32_t> same_thpt_stay_count_mp;
std::map<uint32_t,uint32_t> same_thpt_minbuffer_mp;
std::map<uint32_t,uint32_t> thpt_stay_count_mp;
// std::map<uint32_t,uint32_t> thpt_minbuffer_mp;
std::map<uint32_t,std::vector<uint32_t>> thpt_minbuffer_vec_mp;
std::map<uint32_t,bool> is_retest_mp;
std::map<uint32_t,uint32_t> retest_prev_sent_mp;
std::map<uint32_t,uint32_t> is_maxbuffer_count_mp;
std::map<uint32_t,uint64_t> maxbuffer_prev_sent_mp;
std::map<uint32_t,std::map<uint32_t,int64_t>> retest_latest_time_mp;
std::map<uint32_t,bool> can_remove_start_mp;
std::map<uint32_t,uint32_t> move_by_thpt_increase_count_mp;
std::map<uint32_t,uint32_t> move_by_thpt_decrease_count_mp;
std::map<uint32_t,uint32_t> maxbuffer_sent_sum_mp;
std::map<uint32_t,uint32_t> maxbuffer_sent_count_mp;
std::set<uint32_t> full_thpt_buffer;
std::set<uint32_t> nonfull_thpt_buffer;
std::map<uint32_t,uint32_t> design_min_minq_mp;
std::map<uint32_t,uint32_t> design_count_minq_mp;
uint32_t design_minq1 = configbuffer;
uint32_t design_minq2 = configbuffer;
uint32_t design_minq3 = configbuffer;
uint32_t design_minq_min = configbuffer;
uint32_t design_minq_count = 0;
std::deque<uint32_t> design_drop;
std::deque<uint32_t> design_sent;
void GenQueueDisc::probeMinMonitorLongCollectSimple(uint32_t proberid, uint32_t bmType) {
  // std::cout << "**DEBUG: 4" << std::endl;
  uint32_t queueid = proberid % nPrior;
  if (verbose) std::cout << Simulator::Now() << ": probeMinMonitorLongCollectSimple, proberId=" << proberid << ", queueuid=" << queueid << std::endl;
  int64_t now = Simulator::Now().GetMicroSeconds();
  int64_t CMSALastChanged = sharedMemory->getCurrMaxSizeAllowedLastChanged(proberid);
  // if ((now - CMSALastChanged < RTTms*monitorlongNRTT*1000) or (sharedMemory->hasInstanceInBetweenProbeMinDurationZeroQueueMonitorMap(proberid,now-RTTms*monitorlongNRTT*1000,now))) {
  // if (now - CMSALastChanged < sharedMemory->smallestRTTms*monitorlongNRTT*1000) {
  if (now-latestLongCollect[queueid]<monitorlongms*1000) return;
  if (sharedMemory->isProberInWaitRoom(proberid)) return;

  if (now-CMSALastChanged<monitorlongms*1000) {
    probeMinMonitorLongInvoke(proberid,bmType);
    return;
  }

  uint32_t drop = sharedMemory->probeMinTotalDropBytes[proberid];
  uint32_t maxbuffer = sharedMemory->probeMinMaxBufferUsed[proberid];
  uint32_t sent = sharedMemory->probeMinAverageThroughput[proberid];
  uint32_t minbuffer = sharedMemory->probeMinMinBufferUsed[proberid];
  int64_t duration = sharedMemory->probeMinDurationZeroQueue[proberid];
  if (minbuffer == 100000000) {
    probeMinMonitorLongInvoke(proberid,bmType);
    return;
  }
  
  uint32_t cmsa = sharedMemory->getCurrMaxSizeAllowed(proberid);
  uint32_t prevmaxbuffer = prevMaxBufferUsed[queueid];
  double droprate = drop/(double)sent;
  prevMaxBufferUsed[queueid] = maxbuffer;
  double prevdroprate = prevDropRate[queueid];
  if (sent > 0) {
    prevDropRate[queueid] = droprate;
  } else {
    prevDropRate[queueid] = 1;
  }
  if (verbose) std::cout << Simulator::Now() << ": probeMinMonitorLongCollectSimple, valid instance, proberId=" << proberid << ", drop=" << drop << ", maxbuffer=" << maxbuffer << ", sent=" << sent << ", minbuffer=" << minbuffer << ", duration=" << duration << ", cmsa=" << cmsa << ", prevmaxbuffer=" << prevmaxbuffer << ", prevdroprate=" << prevdroprate << std::endl;
  
  // double value = (double)duration/(sharedMemory->smallestRTTms*monitorlongNRTT*1000);
  // sharedMemory->setLongDurationZeroQueueData(proberid,maxbuffer,value);

  // if (drop == 0) {
  //   if (prevmaxbuffer>=maxbuffer-1500 and prevmaxbuffer<=maxbuffer+1500) {
  //     zeroDropCount[queueid]++;
  //     if (zeroDropCount[queueid]>=4) sharedMemory->setDoMonitorDrop(proberid, false);
  //   } else {
  //     zeroDropCount[queueid] = 0;
  //   }
  // } else {
  //   zeroDropCount[queueid] = 0;
  //   double droprate = (double)drop / sent;
  //   if (verbose) std::cout << Simulator::Now() << ": proberid=" << proberid << ", droprate=" << droprate << std::endl;
  //   if (droprate > goodDroprate) sharedMemory->setDoMonitorDrop(proberid, false);
  // }
  // if (verbose) std::cout << Simulator::Now() << ": proberid=" << proberid << ", zeroDropCount=" << zeroDropCount[queueid] << std::endl;

  uint32_t absoluteMinBuffer = sharedMemory->getAbsoluteMinBuffer();
  // uint32_t currMinBufferThreshold = sharedMemory->getMinBufferThreshold(proberid);

  // bool isPrevious = false;
  bool isCubic = false;
  bool isBBRCopa = false;
  bool isGeneric = false;
  bool isAdaptive = false;
  bool isDropInclusive = false;
  bool thptonly_sent_linear_regression_naive = false;
  bool thptlat_sent_minqlen_linear_naive = false;
  bool thptonly_sent_linear_move = false;
  bool thpt_minq_record_linear = false;
  bool thpt_minq_record_nolinear = false;
  bool thpt_minq_record_nolinear_clean = false;
  bool thpt_minq_record_drop = false;
  bool design_naive = false;
  bool design_naive_fix1 = false;
  bool design_naive_fix2 = false;
  bool design_1cubic = false;
  bool design_1bbr_drop = false;
  bool design_feb2725_v0 = false;
  bool design_feb2725_v1 = false;
  bool is_log = true;

  std::map<uint32_t,uint32_t> totalbwmap;
  totalbwmap.insert(std::make_pair(100,1248001)); // 1248000,1249500
  totalbwmap.insert(std::make_pair(500,6243001)); // 6241500,6243000
  totalbwmap.insert(std::make_pair(1000,12484501)); // 12483000,12484500
  totalbwmap.insert(std::make_pair(5000,62418001)); // 62416500,62418000
  totalbwmap.insert(std::make_pair(10000,124834501)); // 124833000,124834500
  totalbwmap.insert(std::make_pair(20000,249667501)); // 249666000,249667500

  double MTU = 1500;

  uint16_t MINQ_COUNT = 20;
  uint16_t INC_RATIO = 1;
  double DEC_RATIO = 1; // DecreaseRatio;
  uint16_t DROP_COUNT = 500;
  double DR_TARGET = 0.01;
  if (design_feb2725_v1) {
    if (minbuffer != design_minq3) {
      design_minq1 = design_minq2;
      design_minq2 = design_minq3;
      design_minq3 = minbuffer;
      if (design_minq3>=SafeThres && design_minq2<design_minq1 && design_minq2<design_minq3) {
        design_minq_count += 1;
        design_minq_min = std::min(design_minq_min,design_minq2);
      }
    }
    design_drop.push_back(drop);
    design_sent.push_back(sent);
    if (design_drop.size() > DROP_COUNT) {
      design_drop.pop_front();
      design_sent.pop_front();
    }
    double design_drop_sum = 0, design_sent_sum = 0;
    for (auto dedrop : design_drop) {
      design_drop_sum += dedrop;
    }
    for (auto desent : design_sent) {
      design_sent_sum += desent;
    }
    double droprate_multint = design_drop_sum/design_sent_sum;
    
    if (droprate_multint > DR_TARGET) {
      sharedMemory->allocateBufferSpaceSimple(proberid,INC_RATIO*MTU); // cmsa*(droprate_multint/DR_TARGET)-cmsa
    } else {
      if (design_minq3<SafeThres) {
        sharedMemory->allocateBufferSpaceSimple(proberid,INC_RATIO*MTU);
      } else if (design_minq_count>=MINQ_COUNT) {
        sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)(design_minq_min/DEC_RATIO));
        design_minq_count = 0;
        design_minq_min = configbuffer;
      }
    }
  }

  if (design_feb2725_v0) {
    if (minbuffer != design_minq3) {
      design_minq1 = design_minq2;
      design_minq2 = design_minq3;
      design_minq3 = minbuffer;
    }
    if (design_minq3<SafeThres) {
      sharedMemory->allocateBufferSpaceSimple(proberid,INC_RATIO*MTU);
    } else if (design_minq2<design_minq1 && design_minq2<design_minq3) {
      design_minq_count += 1;
      design_minq_min = std::min(design_minq_min,design_minq2);
      if (design_minq_count==MINQ_COUNT) {
        sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)(design_minq_min/DEC_RATIO));
        design_minq_count = 0;
        design_minq_min = configbuffer;
      }
    }
  }

  if (design_1bbr_drop) {
    if (drop>0) sharedMemory->allocateBufferSpaceSimple(proberid,MTU);
  }

  if (design_1cubic) {
    if (minbuffer != design_minq3) {
      design_minq1 = design_minq2;
      design_minq2 = design_minq3;
      design_minq3 = minbuffer;
    }
    if (design_minq3<SafeThres) {
      sharedMemory->allocateBufferSpaceSimple(proberid,MTU);
    } else if (design_minq2<design_minq1 && design_minq2<design_minq3) {
      sharedMemory->allocateBufferSpaceSimple(proberid,-MTU);
    }
  }

  uint16_t PARAM1 = 10;

  if (design_naive_fix2) {
    if (minbuffer==0) {
      sharedMemory->allocateBufferSpaceSimple(proberid,MTU);
      design_count_minq_mp[proberid] = 0;
      design_min_minq_mp[proberid] = configbuffer;
    } else {
      if (drop>0) {
        design_count_minq_mp[proberid] += 1;
        design_min_minq_mp[proberid] = std::min(design_min_minq_mp[proberid],minbuffer);
        if (design_count_minq_mp[proberid]==PARAM1) {
          sharedMemory->allocateBufferSpaceSimple(proberid,-MTU);
          design_count_minq_mp[proberid] = 0;
          design_min_minq_mp[proberid] = configbuffer;
        }
      }
    }
  }

  if (design_naive_fix1) {
    if (minbuffer==0) {
      sharedMemory->allocateBufferSpaceSimple(proberid,MTU);
      design_count_minq_mp[proberid] = 0;
      design_min_minq_mp[proberid] = configbuffer;
    } else {
      design_count_minq_mp[proberid] += 1;
      design_min_minq_mp[proberid] = std::min(design_min_minq_mp[proberid],minbuffer);
      if (design_count_minq_mp[proberid]==PARAM1) {
        sharedMemory->allocateBufferSpaceSimple(proberid,-MTU);
        design_count_minq_mp[proberid] = 0;
        design_min_minq_mp[proberid] = configbuffer;
      }
    }
  }

  if (design_naive) {
    if (minbuffer > SafeThres) {
      sharedMemory->allocateBufferSpaceSimple(proberid,-MTU);
    } else if (minbuffer==0) {
      sharedMemory->allocateBufferSpaceSimple(proberid,MTU);
    } else {
      // pass
    }
  }

  if (thpt_minq_record_drop) {
    uint64_t totalbw = targetBW;
    if (targetBW==0) totalbw = totalbwmap[monitorlongms];
    double packetsize = 1500;
    double m=0, b=0, target=0;

    // handle small maxbuffer
    if (maxbuffer<=cmsa-packetsize && !is_full_throughput(sent,totalbw,packetsize,droprate,dropRateThreshold)) {
      if (is_log) std::cout << Simulator::Now() << ",maxbuffer," << proberid << "," << cmsa << "," << sent << "," << totalbw << "," << maxbuffer << "," << data_points_mp[proberid].size() << std::endl;
      is_maxbuffer_count_mp[proberid] += 1;
      maxbuffer_prev_sent_mp[proberid] += sent;
      probeMinMonitorLongInvoke(proberid,bmType);
      return;
    }

    if (is_maxbuffer_count_mp[proberid]>0) {
      // current interval is not maxbuffer, but previous interval(s) are not maxbuffer
      uint64_t alltotalbw = totalbw + is_maxbuffer_count_mp[proberid]*targetBW;
      uint64_t allsent = sent + maxbuffer_prev_sent_mp[proberid];
      sent = totalbw - (uint32_t)((alltotalbw-allsent)/(is_maxbuffer_count_mp[proberid]+1));
      is_maxbuffer_count_mp[proberid] = 0;
      maxbuffer_prev_sent_mp[proberid] = 0;
    }

    // decide whether I want to test at this point again
    // look at the past five times I tested at this point, if all past five are positive but I'm negative, then retest
    bool log_retest = false;
    if (!is_retest_mp[proberid]) {
      // do I need to retest or not
      if (log_retest) std::cout << "not_retest," << cmsa << "," << sent << ",";
      if (sent<totalbw-2*packetsize) {
        if (log_retest) std::cout << "nonfull,";
        bool should_retest = true;

        std::vector<uint32_t> history;
        auto i=0;
        for (auto it = data_points_mp[proberid].rbegin(); it != data_points_mp[proberid].rend(); ++it) {
          if (i>=HistLen) break;
          int x = std::get<0>(*it);
          int y = std::get<1>(*it);
          if ((int)(cmsa/packetsize)*packetsize<=x && x<((int)(cmsa/packetsize)+1)*packetsize) {
            history.push_back(y);
          } else {
            break;
          }
          i++;
        }
        if (log_retest) std::cout << "size" << history.size() << ",";

        if (history.size()<HistLen) {
          // if insufficient points in history, retest
          should_retest = false;
        } else {
          for (uint32_t hist_sent : history) {
            if (log_retest) std::cout << hist_sent << ",";
            if (hist_sent < totalbw-2*packetsize) {
              should_retest = false;
              break;
            }
          }
        }
        
        if (now-retest_latest_time_mp[proberid][cmsa]<=HistLen*monitorlongms*1000) should_retest = false;

        if (log_retest) std::cout << now-retest_latest_time_mp[proberid][cmsa] << "," << should_retest << ",";
        if (should_retest) {
          is_retest_mp[proberid] = true;
          retest_prev_sent_mp[proberid] = sent;
          if (log_retest) std::cout << "early_return,";
          if (is_log) std::cout << Simulator::Now() << ",pass," << cmsa << "," << sent << "," << totalbw << "," << data_points_mp[proberid].size() << std::endl;
          retest_latest_time_mp[proberid][cmsa] = now;
          probeMinMonitorLongInvoke(proberid,bmType);
          return;
        }
      }
    } else {
      // this is already the retest results
      if (log_retest) std::cout << "retest," << cmsa << "," << sent;
      if (sent < totalbw-2*packetsize) {
        // both results should count
        data_points_mp[proberid].push_back(std::make_tuple(cmsa,retest_prev_sent_mp[proberid]));
      }
      is_retest_mp[proberid] = false;
    }
    if (log_retest) std::cout << std::endl;

    // remove start
    data_points_mp[proberid].push_back(std::make_tuple(cmsa,sent));
    // if (data_points_mp[proberid].size()>=RemoveStartThres) {
    //   if (can_remove_start_mp[proberid]) {
    //     for (auto i=0; i<RemoveStartLen; i++) {
    //       data_points_mp[proberid].pop_front();
    //     }
    //     if (is_log) std::cout << "remove start" << std::endl;
    //     if (thpt_stay_count_mp[proberid]>(RemoveStartThres-RemoveStartLen)) {
    //       thpt_minbuffer_vec_mp[proberid].clear();
    //       for (const auto& point : data_points_mp[proberid]) {
    //         int x = std::get<0>(point);
    //         int y = std::get<1>(point);
    //         thpt_minbuffer_vec_mp[proberid].push_back(y);
    //       }
    //     }
    //     can_remove_start_mp[proberid] = false;
    //   }
    // }
    
    // collect data points and compute record boundaries
    // for (const auto& point : data_points_mp[proberid]) {
    //   int x = std::get<0>(point);
    //   int y = std::get<1>(point);
    //   if (!is_full_throughput(sent,totalbw,packetsize,droprate,dropRateThreshold)) {
    //     // non-full throughput
    //     nonfull_thpt_buffer.insert(x);
    //     auto it = full_thpt_buffer.lower_bound(x+1);
    //     full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
    //   } else {
    //     // full throughput
    //     full_thpt_buffer.insert(x);
    //     auto it = nonfull_thpt_buffer.upper_bound(x-1);
    //     nonfull_thpt_buffer.erase(it,nonfull_thpt_buffer.end());
    //   }
    // }
    if (!is_full_throughput(sent,totalbw,packetsize,droprate,dropRateThreshold)) {
      // non-full throughput
      nonfull_thpt_buffer.insert(cmsa);
      auto it = full_thpt_buffer.lower_bound(cmsa+1);
      full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
    } else {
      // full throughput
      full_thpt_buffer.insert(cmsa);
      auto it = nonfull_thpt_buffer.upper_bound(cmsa+1);
      nonfull_thpt_buffer.erase(it,nonfull_thpt_buffer.end());
    }

    bool move_by_thpt = false;
    bool should_explore = false;
    bool is_same_thpt_stay_count = false;

    bool should_increment_increase_count = false;
    bool should_increment_decrease_count = false;
    bool should_increment_decrease_count_wait = false;

    uint32_t currmin_full_buffer=0, currmax_nonfull_buffer=0;
    if (full_thpt_buffer.size()>0) currmin_full_buffer = *full_thpt_buffer.begin();
    if (nonfull_thpt_buffer.size()>0) currmax_nonfull_buffer = *nonfull_thpt_buffer.rbegin();

    // move by record
    if (currmin_full_buffer!=0 && currmax_nonfull_buffer!=0) {
      if (currmin_full_buffer<=currmax_nonfull_buffer+packetsize) {
        if (same_thpt_stay_count_mp[proberid]<ExploreThres) {
          sharedMemory->allocateBufferSpaceSimple(proberid,currmin_full_buffer-cmsa);
          if (prev_cmsa_mp[proberid]==cmsa) is_same_thpt_stay_count = true;
        } else {
          should_explore = true;
        }
      } else {
        move_by_thpt = true;
      } 
    } else {
      move_by_thpt = true;
    } 

    if (prev_cmsa_mp[proberid]==cmsa) {
      thpt_stay_count_mp[proberid]+=1;
      thpt_minbuffer_vec_mp[proberid].push_back(minbuffer);
    } else {
      thpt_stay_count_mp[proberid] = 0;
      thpt_minbuffer_vec_mp[proberid].clear();
    }
    prev_cmsa_mp[proberid] = cmsa;

    // move by throughput
    if (move_by_thpt) {
      uint32_t cmsa_after = cmsa;
      if (!is_full_throughput(sent,totalbw,packetsize,droprate,dropRateThreshold)) {
        should_increment_increase_count = true;
        if (move_by_thpt_increase_count_mp[proberid] < ConsecIncreaseThres) {
          if (sent < totalbw-2*packetsize) {
            // by throughput
            cmsa_after = cmsa + (uint32_t)std::min(packetsize*StepIncreaseCap,((totalbw-1-packetsize-sent)/IncreaseRatio));
          } else {
            // by drop
            cmsa_after = cmsa + (uint32_t)std::min(packetsize*StepIncreaseCap,((droprate-dropRateThreshold)*sent/IncreaseRatio));
          }
        } else {
          if (sent < totalbw-2*packetsize) {
            cmsa_after = cmsa + (uint32_t)((totalbw-1-packetsize-sent)/IncreaseRatio);
          } else {
            cmsa_after = cmsa + (uint32_t)((droprate-dropRateThreshold)*sent/IncreaseRatio);
          }
        }
      } else {
          uint32_t tmb_sum = 0;
          uint32_t tmb_count = thpt_minbuffer_vec_mp[proberid].size();
          for (auto tmb : thpt_minbuffer_vec_mp[proberid]) {
            tmb_sum += tmb;
          }
          double tmb_average = (double)tmb_sum/tmb_count;
          uint32_t thpt_minbuffer = configbuffer;
          for (auto tmb : thpt_minbuffer_vec_mp[proberid]) {
            if (tmb+packetsize*MinQOutlier < tmb_average) continue; // denoise minbuffer
            if (tmb < thpt_minbuffer) thpt_minbuffer = tmb;
          }

          if (thpt_stay_count_mp[proberid]>=MinQHold && thpt_minbuffer>=packetsize*SafeThres) {
            should_increment_decrease_count = true;
            if (move_by_thpt_decrease_count_mp[proberid] < ConsecDecreaseThres) {
              cmsa_after = cmsa - (uint32_t)std::min(packetsize*StepDecreaseCap,(thpt_minbuffer-packetsize*SafeThres)/DecreaseRatio);
            } else {
              cmsa_after = cmsa - (uint32_t)((thpt_minbuffer-packetsize*SafeThres)/DecreaseRatio);
            }
          } else {
            should_increment_decrease_count_wait = true;
          }
      }

      if (currmax_nonfull_buffer!=0 && cmsa_after < currmax_nonfull_buffer) cmsa_after = currmax_nonfull_buffer + packetsize; 
      if (currmin_full_buffer!=0 && cmsa_after > currmin_full_buffer) cmsa_after = currmin_full_buffer - packetsize;

      sharedMemory->allocateBufferSpaceSimple(proberid,cmsa_after-cmsa);      
    }

    if (should_explore) {
      if (same_thpt_minbuffer_mp[proberid]>=packetsize*SafeThres) sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)std::min(packetsize*StepDecreaseCap,(same_thpt_minbuffer_mp[proberid]-packetsize*SafeThres)/DecreaseRatio));
    }

    if (should_increment_increase_count) {
      move_by_thpt_increase_count_mp[proberid] += 1;
    } else {
      move_by_thpt_increase_count_mp[proberid] = 0;
    }
    if (should_increment_decrease_count) {
      move_by_thpt_decrease_count_mp[proberid] += 1;
    } else {
      if (!should_increment_decrease_count_wait) move_by_thpt_decrease_count_mp[proberid] = 0;
    }

    if (is_same_thpt_stay_count) {
      same_thpt_stay_count_mp[proberid] += 1;
      if (minbuffer < same_thpt_minbuffer_mp[proberid]) same_thpt_minbuffer_mp[proberid] = minbuffer;
    } else {
      same_thpt_stay_count_mp[proberid] = 0;
      same_thpt_minbuffer_mp[proberid] = configbuffer;
    }

    if (is_log) {
      std::cout << Simulator::Now() << "," << proberid << "," << cmsa << "," << sent << "," << totalbw << "," << minbuffer << "," << data_points_mp[proberid].size() << "," << m << "," << b << "," << target;
      // std::cout << "," << minbuffer << "," << data_points_lat.size() << "," << m_lat << "," << b_lat << "," << target_lat << "," << target_overall;
      std::cout << "," << currmin_full_buffer << "," << currmax_nonfull_buffer;
      std::cout << std::endl;
    }
  }

  if (thpt_minq_record_nolinear_clean) {
    uint64_t totalbw = targetBW;
    if (targetBW==0) totalbw = totalbwmap[monitorlongms];
    double packetsize = 1500;
    double m=0, b=0, target=0;

    // handle small maxbuffer
    if (maxbuffer<=cmsa-packetsize && sent<totalbw-2*packetsize) {
      if (is_log) std::cout << Simulator::Now() << ",maxbuffer," << proberid << "," << cmsa << "," << sent << "," << totalbw << "," << maxbuffer << "," << data_points_mp[proberid].size() << std::endl;
      is_maxbuffer_count_mp[proberid] += 1;
      maxbuffer_prev_sent_mp[proberid] += sent;
      probeMinMonitorLongInvoke(proberid,bmType);
      return;
    }

    if (is_maxbuffer_count_mp[proberid]>0) {
      // current interval is not maxbuffer, but previous interval(s) are not maxbuffer
      uint64_t alltotalbw = totalbw + is_maxbuffer_count_mp[proberid]*targetBW;
      uint64_t allsent = sent + maxbuffer_prev_sent_mp[proberid];
      sent = totalbw - (uint32_t)((alltotalbw-allsent)/(is_maxbuffer_count_mp[proberid]+1));
      is_maxbuffer_count_mp[proberid] = 0;
      maxbuffer_prev_sent_mp[proberid] = 0;
    }

    // decide whether I want to test at this point again
    // look at the past five times I tested at this point, if all past five are positive but I'm negative, then retest
    bool log_retest = false;
    if (!is_retest_mp[proberid]) {
      // do I need to retest or not
      if (log_retest) std::cout << "not_retest," << cmsa << "," << sent << ",";
      if (sent<totalbw-2*packetsize) {
        if (log_retest) std::cout << "nonfull,";
        bool should_retest = true;

        std::vector<uint32_t> history;
        auto i=0;
        for (auto it = data_points_mp[proberid].rbegin(); it != data_points_mp[proberid].rend(); ++it) {
          if (i>=HistLen) break;
          int x = std::get<0>(*it);
          int y = std::get<1>(*it);
          if ((int)(cmsa/packetsize)*packetsize<=x && x<((int)(cmsa/packetsize)+1)*packetsize) {
            history.push_back(y);
          } else {
            break;
          }
          i++;
        }
        if (log_retest) std::cout << "size" << history.size() << ",";

        if (history.size()<HistLen) {
          // if insufficient points in history, retest
          should_retest = false;
        } else {
          for (uint32_t hist_sent : history) {
            if (log_retest) std::cout << hist_sent << ",";
            if (hist_sent < totalbw-2*packetsize) {
              should_retest = false;
              break;
            }
          }
        }
        
        if (now-retest_latest_time_mp[proberid][cmsa]<=HistLen*monitorlongms*1000) should_retest = false;

        if (log_retest) std::cout << now-retest_latest_time_mp[proberid][cmsa] << "," << should_retest << ",";
        if (should_retest) {
          is_retest_mp[proberid] = true;
          retest_prev_sent_mp[proberid] = sent;
          if (log_retest) std::cout << "early_return,";
          if (is_log) std::cout << Simulator::Now() << ",pass," << cmsa << "," << sent << "," << totalbw << "," << data_points_mp[proberid].size() << std::endl;
          retest_latest_time_mp[proberid][cmsa] = now;
          probeMinMonitorLongInvoke(proberid,bmType);
          return;
        }
      }
    } else {
      // this is already the retest results
      if (log_retest) std::cout << "retest," << cmsa << "," << sent;
      if (sent < totalbw-2*packetsize) {
        // both results should count
        data_points_mp[proberid].push_back(std::make_tuple(cmsa,retest_prev_sent_mp[proberid]));
      }
      is_retest_mp[proberid] = false;
    }
    if (log_retest) std::cout << std::endl;

    // remove start
    data_points_mp[proberid].push_back(std::make_tuple(cmsa,sent));
    if (data_points_mp[proberid].size()>=RemoveStartThres) {
      if (can_remove_start_mp[proberid]) {
        for (auto i=0; i<RemoveStartLen; i++) {
          data_points_mp[proberid].pop_front();
        }
        if (is_log) std::cout << "remove start" << std::endl;
        if (thpt_stay_count_mp[proberid]>(RemoveStartThres-RemoveStartLen)) {
          thpt_minbuffer_vec_mp[proberid].clear();
          for (const auto& point : data_points_mp[proberid]) {
            int x = std::get<0>(point);
            int y = std::get<1>(point);
            thpt_minbuffer_vec_mp[proberid].push_back(y);
          }
        }
        can_remove_start_mp[proberid] = false;
      }
    }
    
    // collect data points and compute record boundaries
    full_thpt_buffer.clear();
    nonfull_thpt_buffer.clear();
    for (const auto& point : data_points_mp[proberid]) {
      int x = std::get<0>(point);
      int y = std::get<1>(point);
      if (y<totalbw-2*packetsize) {
        // non-full throughput
        nonfull_thpt_buffer.insert(x);
        auto it = full_thpt_buffer.lower_bound(x+1);
        full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
      } else {
        // full throughput
        full_thpt_buffer.insert(x);
        auto it = nonfull_thpt_buffer.upper_bound(x-1);
        nonfull_thpt_buffer.erase(it,nonfull_thpt_buffer.end());
      }
    }

    bool move_by_thpt = false;
    bool should_explore = false;
    bool is_same_thpt_stay_count = false;

    bool should_increment_increase_count = false;
    bool should_increment_decrease_count = false;
    bool should_increment_decrease_count_wait = false;

    uint32_t currmin_full_buffer=0, currmax_nonfull_buffer=0;
    if (full_thpt_buffer.size()>0) currmin_full_buffer = *full_thpt_buffer.begin();
    if (nonfull_thpt_buffer.size()>0) currmax_nonfull_buffer = *nonfull_thpt_buffer.rbegin();

    // move by record
    if (currmin_full_buffer!=0 && currmax_nonfull_buffer!=0) {
      if (currmin_full_buffer<=currmax_nonfull_buffer+packetsize) {
        if (same_thpt_stay_count_mp[proberid]<ExploreThres) {
          sharedMemory->allocateBufferSpaceSimple(proberid,currmin_full_buffer-cmsa);
          if (prev_cmsa_mp[proberid]==cmsa) is_same_thpt_stay_count = true;
        } else {
          should_explore = true;
        }
      } else {
        move_by_thpt = true;
      } 
    } else {
      move_by_thpt = true;
    } 

    if (prev_cmsa_mp[proberid]==cmsa) {
      thpt_stay_count_mp[proberid]+=1;
      thpt_minbuffer_vec_mp[proberid].push_back(minbuffer);
    } else {
      thpt_stay_count_mp[proberid] = 0;
      thpt_minbuffer_vec_mp[proberid].clear();
    }
    prev_cmsa_mp[proberid] = cmsa;

    // move by throughput
    if (move_by_thpt) {
      uint32_t cmsa_after = cmsa;
      if (sent<totalbw-packetsize*2) {
        should_increment_increase_count = true;
        if (move_by_thpt_increase_count_mp[proberid] < ConsecIncreaseThres) {
          cmsa_after = cmsa + (uint32_t)std::min(packetsize*StepIncreaseCap,((totalbw-1-packetsize-sent)/IncreaseRatio));
        } else {
          cmsa_after = cmsa + (uint32_t)((totalbw-1-packetsize-sent)/IncreaseRatio);
        }
      } else {
          uint32_t tmb_sum = 0;
          uint32_t tmb_count = thpt_minbuffer_vec_mp[proberid].size();
          for (auto tmb : thpt_minbuffer_vec_mp[proberid]) {
            tmb_sum += tmb;
          }
          double tmb_average = (double)tmb_sum/tmb_count;
          uint32_t thpt_minbuffer = configbuffer;
          for (auto tmb : thpt_minbuffer_vec_mp[proberid]) {
            if (tmb+packetsize*MinQOutlier < tmb_average) continue; // denoise minbuffer
            if (tmb < thpt_minbuffer) thpt_minbuffer = tmb;
          }

          if (thpt_stay_count_mp[proberid]>=MinQHold && thpt_minbuffer>=packetsize*SafeThres) {
            should_increment_decrease_count = true;
            if (move_by_thpt_decrease_count_mp[proberid] < ConsecDecreaseThres) {
              cmsa_after = cmsa - (uint32_t)std::min(packetsize*StepDecreaseCap,(thpt_minbuffer-packetsize*SafeThres)/DecreaseRatio);
            } else {
              cmsa_after = cmsa - (uint32_t)((thpt_minbuffer-packetsize*SafeThres)/DecreaseRatio);
            }
          } else {
            should_increment_decrease_count_wait = true;
          }
      }

      if (currmax_nonfull_buffer!=0 && cmsa_after < currmax_nonfull_buffer) cmsa_after = currmax_nonfull_buffer + packetsize; 
      if (currmin_full_buffer!=0 && cmsa_after > currmin_full_buffer) cmsa_after = currmin_full_buffer - packetsize;

      sharedMemory->allocateBufferSpaceSimple(proberid,cmsa_after-cmsa);      
    }

    if (should_explore) {
      if (same_thpt_minbuffer_mp[proberid]>=packetsize*SafeThres) sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)std::min(packetsize*StepDecreaseCap,(same_thpt_minbuffer_mp[proberid]-packetsize*SafeThres)/DecreaseRatio));
    }

    if (should_increment_increase_count) {
      move_by_thpt_increase_count_mp[proberid] += 1;
    } else {
      move_by_thpt_increase_count_mp[proberid] = 0;
    }
    if (should_increment_decrease_count) {
      move_by_thpt_decrease_count_mp[proberid] += 1;
    } else {
      if (!should_increment_decrease_count_wait) move_by_thpt_decrease_count_mp[proberid] = 0;
    }

    if (is_same_thpt_stay_count) {
      same_thpt_stay_count_mp[proberid] += 1;
      if (minbuffer < same_thpt_minbuffer_mp[proberid]) same_thpt_minbuffer_mp[proberid] = minbuffer;
    } else {
      same_thpt_stay_count_mp[proberid] = 0;
      same_thpt_minbuffer_mp[proberid] = configbuffer;
    }

    if (is_log) {
      std::cout << Simulator::Now() << "," << proberid << "," << cmsa << "," << sent << "," << totalbw << "," << minbuffer << "," << data_points_mp[proberid].size() << "," << m << "," << b << "," << target;
      // std::cout << "," << minbuffer << "," << data_points_lat.size() << "," << m_lat << "," << b_lat << "," << target_lat << "," << target_overall;
      std::cout << "," << currmin_full_buffer << "," << currmax_nonfull_buffer;
      std::cout << std::endl;
    }
  }

  if (thpt_minq_record_linear) {
    // double totalbw = portBW*1000000000/8.0/(nPrior-1)*monitorlongms/1000.0;
    // double totalbw = totalbwmap[monitorlongms];
    uint32_t totalbw = targetBW;
    if (targetBW==0) totalbwmap[monitorlongms];
    double packetsize = 1500;

    if (maxbuffer<=cmsa-packetsize && sent<totalbw-2*packetsize) {
      if (is_log) std::cout << Simulator::Now() << ",maxbuffer," << cmsa << "," << sent << "," << totalbw << "," << maxbuffer << "," << data_points.size() << std::endl;
    //   maxbuffer_sent_sum += sent;
    //   maxbuffer_sent_count += 1;
      probeMinMonitorLongInvoke(proberid,bmType);
      return;
    } else {
    //   if (maxbuffer_sent_count > 0) {
    //     maxbuffer_sent_sum += sent;
    //     maxbuffer_sent_count += 1;
    //     if (is_log) std::cout << Simulator::Now() << ",maxbuffer_fix," << maxbuffer_sent_sum << "," << maxbuffer_sent_count << "," << sent << ",";
    //     sent = (uint32_t)(maxbuffer_sent_sum/maxbuffer_sent_count);
    //     if (is_log) std::cout << sent << std::endl;
    //     maxbuffer_sent_sum=0;
    //     maxbuffer_sent_count=0;
    //   }
    }

    // decide whether I want to test at this point again
    // look at the past five times I tested at this point, if all past five are positive but I'm negative, then retest
    bool log_retest = false;
    if (!is_retest) {
      // do I need to retest or not
      if (log_retest) std::cout << "not_retest," << cmsa << "," << sent << ",";
      if (sent<totalbw-2*packetsize) {
        if (log_retest) std::cout << "nonfull,";
        bool should_retest = true;

        uint16_t hist_len = 5;
        std::vector<uint32_t> history;
        auto i=0;
        for (auto it = data_points.rbegin(); it != data_points.rend(); ++it) {
          if (i>=hist_len) break;
          int x = std::get<0>(*it);
          int y = std::get<1>(*it);
          if ((int)(cmsa/packetsize)*packetsize<=x && x<((int)(cmsa/packetsize)+1)*packetsize) {
            history.push_back(y);
          } else {
            break;
          }
          i++;
        }
        if (log_retest) std::cout << "size" << history.size() << ",";

        if (history.size()<hist_len) {
          // if insufficient points in history, retest
          should_retest = false;
        } else {
          for (uint32_t hist_sent : history) {
            if (log_retest) std::cout << hist_sent << ",";
            if (hist_sent < totalbw-2*packetsize) {
              should_retest = false;
              break;
            }
          }
        }
        
        if (now-retest_latest_time[cmsa]<=5*monitorlongms*1000) should_retest = false;

        if (log_retest) std::cout << now-retest_latest_time[cmsa] << "," << should_retest << ",";
        if (should_retest) {
          is_retest = true;
          retest_prev_sent = sent;
          if (log_retest) std::cout << "early_return,";
          if (is_log) std::cout << Simulator::Now() << ",pass," << cmsa << "," << sent << "," << totalbw << "," << data_points.size() << std::endl;
          retest_latest_time[cmsa] = now;
          probeMinMonitorLongInvoke(proberid,bmType);
          return;
        }
      } else {
        // full throughput, no need to retest
      }
    } else {
      // this is already the retest results
      if (log_retest) std::cout << "retest," << cmsa << "," << sent;
      if (sent < totalbw-2*packetsize) {
        // both results should count
        data_points.push_back(std::make_tuple(cmsa,retest_prev_sent));
        // if (data_points.size() >= 100) data_points.pop_front();
        // nonfull_thpt_buffer.insert(cmsa);
        // auto it = full_thpt_buffer.lower_bound(cmsa+1);
        // full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
      } else {
        // should ignore the previous result
      }
      is_retest = false;
    }
    if (log_retest) std::cout << std::endl;

    // collect data points
    data_points.push_back(std::make_tuple(cmsa,sent));
    if (data_points.size()>=50) {
      if (can_remove_start) {
        for (auto i=0; i<10; i++) {
          data_points.pop_front();
        }
        if (is_log) std::cout << "remove start" << std::endl;
        if (thpt_stay_count>40) {
          // thpt_minbuffer = configbuffer;
          thpt_minbuffer_vec.clear();
          for (const auto& point : data_points) {
            int x = std::get<0>(point);
            int y = std::get<1>(point);
            // if (x!=cmsa) {
            //   std::cout << "**ERROR**: x=" << x << ",cmsa=" << cmsa <<std::endl;
            //   exit(0);
            // }
            // if (y<thpt_minbuffer) thpt_minbuffer = y;
            thpt_minbuffer_vec.push_back(y);
          }
        }
        can_remove_start = false;
      }
    }
    // if (data_points.size() >= 100) data_points.pop_front();
    // if (is_log) std::cout << "DATA," << Simulator::Now() << "," << cmsa << "," << sent << "," << minbuffer << std::endl;
    // if (sent<totalbw-2*packetsize) {
    //   // non-full throughput
    //   nonfull_thpt_buffer.insert(cmsa);
    //   auto it = full_thpt_buffer.lower_bound(cmsa+1);
    //   full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
    //   // if (is_log) std::cout << Simulator::Now() << ",nonfull," << *full_thpt_buffer.begin() << "," << *full_thpt_buffer.rbegin() << "," << *nonfull_thpt_buffer.begin() << "," << *nonfull_thpt_buffer.rbegin() << std::endl;
    // } else {
    //   // full throughput
    //   full_thpt_buffer.insert(cmsa);
    //   auto it = nonfull_thpt_buffer.upper_bound(cmsa-1);
    //   nonfull_thpt_buffer.erase(it,nonfull_thpt_buffer.end());
    //   // if (is_log) std::cout << Simulator::Now() << ",full," << *full_thpt_buffer.begin() << "," << *full_thpt_buffer.rbegin() << "," << *nonfull_thpt_buffer.begin() << "," << *nonfull_thpt_buffer.rbegin() << std::endl;
    // }
    for (const auto& point : data_points) {
      int x = std::get<0>(point);
      int y = std::get<1>(point);
      if (y<totalbw-2*packetsize) {
        // non-full throughput
        nonfull_thpt_buffer.insert(x);
        auto it = full_thpt_buffer.lower_bound(x+1);
        full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
      } else {
        // full throughput
        full_thpt_buffer.insert(x);
        auto it = nonfull_thpt_buffer.upper_bound(x-1);
        nonfull_thpt_buffer.erase(it,nonfull_thpt_buffer.end());
      }
    }

    // data processing
    std::map<uint32_t, std::vector<uint32_t>> grouped_by_x;
    for (const auto& point : data_points) {
        int x = std::get<0>(point);
        int y = std::get<1>(point);
        grouped_by_x[x].push_back(y);
    }

    std::deque<std::tuple<uint32_t,uint32_t>> averaged_points;
    for (const auto& entry : grouped_by_x) {
      uint32_t x = entry.first;
      const std::vector<uint32_t>& y_values = entry.second;
      
      int sum_y = 0;
      for (int y : y_values) {
          sum_y += y;
      }
      uint32_t avg_y = sum_y / y_values.size();

      if (avg_y<totalbw-packetsize*2) averaged_points.push_back(std::make_tuple(x, avg_y));
    }

    // linear regression
    double m=0, b=0, target=0;
    if (averaged_points.size()>=2) {
      double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
      int n = averaged_points.size();

      for (const auto& point : averaged_points) {
          double x = std::get<0>(point);
          double y = std::get<1>(point);
          sum_x += x;
          sum_y += y;
          sum_x2 += x * x;
          sum_xy += x * y;
      }

      double denom = n * sum_x2 - sum_x * sum_x;
      if (denom == 0) {
        // sharedMemory->allocateBufferSpaceSimple(proberid,1500);
      } else {
        m = (n * sum_xy - sum_x * sum_y) / denom;
        b = (sum_y * sum_x2 - sum_x * sum_xy) / denom;
        if (m<=0 || b<=0) {
          // data_points.clear();
          // nonfull_thpt_buffer.clear();
          // full_thpt_buffer.clear();
          // move_by_thpt = true;
          // AnnC: target == 0
        } else {
          target = (std::floor((totalbw-b)/m/1500.0)+2)*1500; 
        }
      }
    } else {
      // move_by_thpt = true;
      // AnnC: target == 0
    }
    
    // move by record
    bool move_by_thpt = false;
    // uint32_t move_by_thpt_minbuffer = configbuffer;
    bool should_explore = false;
    bool is_same_thpt_stay_count = false;

    bool should_increment_increase_count = false;
    bool should_increment_decrease_count = false;
    bool should_increment_decrease_count_wait = false;

    uint32_t currmin_full_buffer=0, currmax_nonfull_buffer=0;
    if (full_thpt_buffer.size()>0) currmin_full_buffer = *full_thpt_buffer.begin();
    if (nonfull_thpt_buffer.size()>0) currmax_nonfull_buffer = *nonfull_thpt_buffer.rbegin();

    if (currmin_full_buffer!=0 && currmax_nonfull_buffer!=0) {
      if (currmin_full_buffer<=currmax_nonfull_buffer+packetsize) {
        if (same_thpt_stay_count<20) {
          sharedMemory->allocateBufferSpaceSimple(proberid,currmin_full_buffer-cmsa);
          if (prev_cmsa==cmsa) is_same_thpt_stay_count = true;
        } else {
          // move_by_thpt = true;
          // move_by_thpt_minbuffer = same_thpt_minbuffer;
          should_explore = true;
        }
      } else {
        if (target > 0) {
          if (target>=currmin_full_buffer) {
            // sharedMemory->allocateBufferSpaceSimple(proberid,currmin_full_buffer-1500-cmsa);
            move_by_thpt = true;
          } else if (target<=currmax_nonfull_buffer) {
            // sharedMemory->allocateBufferSpaceSimple(proberid,currmax_nonfull_buffer+1500-cmsa);
            move_by_thpt = true;
          } else {
            if (target>=4*packetsize) { // AnnC: target>=4*packetsize
              sharedMemory->allocateBufferSpaceSimple(proberid,target-cmsa);
            }
          }
        } else {
          move_by_thpt = true;
        }
      } 
    } else {
      if (currmin_full_buffer==0) {
        if (target>=4*packetsize && target>currmax_nonfull_buffer) { // AnnC: target>=4*packetsize
          should_increment_increase_count = true;
          if (move_by_thpt_increase_count < 3) {
            sharedMemory->allocateBufferSpaceSimple(proberid,std::min(packetsize*5,target-cmsa));
          } else {
            sharedMemory->allocateBufferSpaceSimple(proberid,target-cmsa);
          }
        } else {
          move_by_thpt = true;
        }
      } else if (currmax_nonfull_buffer==0) {
        move_by_thpt = true;
      }
    } 

    // move by throughput 
    if (prev_cmsa==cmsa) {
      thpt_stay_count+=1;
      // if (minbuffer < thpt_minbuffer) thpt_minbuffer = minbuffer;
      thpt_minbuffer_vec.push_back(minbuffer);
    } else {
      thpt_stay_count = 0;
      // thpt_minbuffer = configbuffer;
      thpt_minbuffer_vec.clear();
    }
    prev_cmsa = cmsa;

    if (move_by_thpt) {
      uint32_t cmsa_after = cmsa;
      if (sent<totalbw-packetsize*2) {
        should_increment_increase_count = true;
        if (move_by_thpt_increase_count < 3) {
          cmsa_after = cmsa + (uint32_t)std::min(packetsize*5,((totalbw-1-packetsize-sent)/10));
        } else {
          cmsa_after = cmsa + (uint32_t)((totalbw-1-packetsize-sent)/10);
        }
        // sharedMemory->allocateBufferSpaceSimple(proberid,(uint32_t)std::min(packetsize*5,((totalbw-1-packetsize-sent)/10)));
      } else {
        // if (same_thpt_minbuffer<configbuffer) {
        //   if (same_thpt_minbuffer>=packetsize*4) sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)((same_thpt_minbuffer-packetsize*4)/5));
        // } else {
          // if (minbuffer>=packetsize*4) sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)((minbuffer-packetsize*4)/3));

          uint32_t tmb_sum = 0;
          uint32_t tmb_count = thpt_minbuffer_vec.size();
          for (auto tmb : thpt_minbuffer_vec) {
            tmb_sum += tmb;
          }
          double tmb_average = (double)tmb_sum/tmb_count;
          thpt_minbuffer = configbuffer;
          for (auto tmb : thpt_minbuffer_vec) {
            if (tmb+packetsize*10 < tmb_average) continue; // denoise minbuffer
            if (tmb < thpt_minbuffer) thpt_minbuffer = tmb;
          }

          if (thpt_stay_count>=5 && thpt_minbuffer>=packetsize*4) {
            should_increment_decrease_count = true;
            if (move_by_thpt_decrease_count < 3) {
              cmsa_after = cmsa - (uint32_t)std::min(packetsize*5,(thpt_minbuffer-packetsize*4)/5);
            } else {
              cmsa_after = cmsa - (uint32_t)((thpt_minbuffer-packetsize*4)/5);
            }
            // sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)std::min(packetsize*5,(minbuffer-packetsize*4)/5));
          } else {
            should_increment_decrease_count_wait = true;
          }
        // }
      }

      if (currmax_nonfull_buffer!=0 && cmsa_after < currmax_nonfull_buffer) cmsa_after = currmax_nonfull_buffer + packetsize; 
      if (currmin_full_buffer!=0 && cmsa_after > currmin_full_buffer) cmsa_after = currmin_full_buffer - packetsize;

      sharedMemory->allocateBufferSpaceSimple(proberid,cmsa_after-cmsa);      
    }

    if (should_explore) {
      if (same_thpt_minbuffer>=packetsize*4) sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)std::min(packetsize*5,(same_thpt_minbuffer-packetsize*4)/5));
    }

    if (should_increment_increase_count) {
      move_by_thpt_increase_count += 1;
    } else {
      move_by_thpt_increase_count = 0;
    }
    if (should_increment_decrease_count) {
      move_by_thpt_decrease_count += 1;
    } else {
      if (!should_increment_decrease_count_wait) move_by_thpt_decrease_count = 0;
    }

    if (is_same_thpt_stay_count) {
      same_thpt_stay_count += 1;
      if (minbuffer < same_thpt_minbuffer) same_thpt_minbuffer = minbuffer;
    } else {
      same_thpt_stay_count = 0;
      same_thpt_minbuffer = configbuffer;
    }

    if (is_log) {
      std::cout << Simulator::Now() << "," << cmsa << "," << sent << "," << totalbw << "," << minbuffer << "," << data_points.size() << "," << m << "," << b << "," << target;
      // std::cout << "," << minbuffer << "," << data_points_lat.size() << "," << m_lat << "," << b_lat << "," << target_lat << "," << target_overall;
      std::cout << "," << currmin_full_buffer << "," << currmax_nonfull_buffer;
      std::cout << std::endl;
    }
  }

  if (thpt_minq_record_nolinear) {
    // double totalbw = portBW*1000000000/8.0/(nPrior-1)*monitorlongms/1000.0;
    // double totalbw = totalbwmap[monitorlongms];
    uint32_t totalbw = targetBW;
    if (targetBW==0) totalbwmap[monitorlongms];
    double packetsize = 1500;

    if (maxbuffer<=cmsa-packetsize && sent<totalbw-2*packetsize) {
      if (is_log) std::cout << Simulator::Now() << ",maxbuffer," << proberid << "," << cmsa << "," << sent << "," << totalbw << "," << maxbuffer << "," << data_points_mp[proberid].size() << std::endl;
    //   maxbuffer_sent_sum += sent;
    //   maxbuffer_sent_count += 1;
      probeMinMonitorLongInvoke(proberid,bmType);
      return;
    } else {
    //   if (maxbuffer_sent_count > 0) {
    //     maxbuffer_sent_sum += sent;
    //     maxbuffer_sent_count += 1;
    //     if (is_log) std::cout << Simulator::Now() << ",maxbuffer_fix," << maxbuffer_sent_sum << "," << maxbuffer_sent_count << "," << sent << ",";
    //     sent = (uint32_t)(maxbuffer_sent_sum/maxbuffer_sent_count);
    //     if (is_log) std::cout << sent << std::endl;
    //     maxbuffer_sent_sum=0;
    //     maxbuffer_sent_count=0;
    //   }
    }

    // decide whether I want to test at this point again
    // look at the past five times I tested at this point, if all past five are positive but I'm negative, then retest
    bool log_retest = false;
    if (!is_retest_mp[proberid]) {
      // do I need to retest or not
      if (log_retest) std::cout << "not_retest," << cmsa << "," << sent << ",";
      if (sent<totalbw-2*packetsize) {
        if (log_retest) std::cout << "nonfull,";
        bool should_retest = true;

        uint16_t hist_len = 5;
        std::vector<uint32_t> history;
        auto i=0;
        for (auto it = data_points_mp[proberid].rbegin(); it != data_points_mp[proberid].rend(); ++it) {
          if (i>=hist_len) break;
          int x = std::get<0>(*it);
          int y = std::get<1>(*it);
          if ((int)(cmsa/packetsize)*packetsize<=x && x<((int)(cmsa/packetsize)+1)*packetsize) {
            history.push_back(y);
          } else {
            break;
          }
          i++;
        }
        if (log_retest) std::cout << "size" << history.size() << ",";

        if (history.size()<hist_len) {
          // if insufficient points in history, retest
          should_retest = false;
        } else {
          for (uint32_t hist_sent : history) {
            if (log_retest) std::cout << hist_sent << ",";
            if (hist_sent < totalbw-2*packetsize) {
              should_retest = false;
              break;
            }
          }
        }
        
        if (now-retest_latest_time_mp[proberid][cmsa]<=5*monitorlongms*1000) should_retest = false;

        if (log_retest) std::cout << now-retest_latest_time_mp[proberid][cmsa] << "," << should_retest << ",";
        if (should_retest) {
          is_retest_mp[proberid] = true;
          retest_prev_sent_mp[proberid] = sent;
          if (log_retest) std::cout << "early_return,";
          if (is_log) std::cout << Simulator::Now() << ",pass," << cmsa << "," << sent << "," << totalbw << "," << data_points_mp[proberid].size() << std::endl;
          retest_latest_time_mp[proberid][cmsa] = now;
          probeMinMonitorLongInvoke(proberid,bmType);
          return;
        }
      } else {
        // full throughput, no need to retest
      }
    } else {
      // this is already the retest results
      if (log_retest) std::cout << "retest," << cmsa << "," << sent;
      if (sent < totalbw-2*packetsize) {
        // both results should count
        data_points_mp[proberid].push_back(std::make_tuple(cmsa,retest_prev_sent_mp[proberid]));
        // if (data_points.size() >= 100) data_points.pop_front();
        // nonfull_thpt_buffer.insert(cmsa);
        // auto it = full_thpt_buffer.lower_bound(cmsa+1);
        // full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
      } else {
        // should ignore the previous result
      }
      is_retest_mp[proberid] = false;
    }
    if (log_retest) std::cout << std::endl;

    // collect data points
    data_points_mp[proberid].push_back(std::make_tuple(cmsa,sent));
    if (data_points_mp[proberid].size()>=50) {
      if (can_remove_start_mp[proberid]) {
        for (auto i=0; i<10; i++) {
          data_points_mp[proberid].pop_front();
        }
        if (is_log) std::cout << "remove start" << std::endl;
        if (thpt_stay_count_mp[proberid]>40) {
          // thpt_minbuffer = configbuffer;
          thpt_minbuffer_vec_mp[proberid].clear();
          for (const auto& point : data_points_mp[proberid]) {
            int x = std::get<0>(point);
            int y = std::get<1>(point);
            // if (x!=cmsa) {
            //   std::cout << "**ERROR**: x=" << x << ",cmsa=" << cmsa <<std::endl;
            //   exit(0);
            // }
            // if (y<thpt_minbuffer) thpt_minbuffer = y;
            thpt_minbuffer_vec_mp[proberid].push_back(y);
          }
        }
        can_remove_start_mp[proberid] = false;
      }
    }
    // if (data_points.size() >= 100) data_points.pop_front();
    // if (is_log) std::cout << "DATA," << Simulator::Now() << "," << cmsa << "," << sent << "," << minbuffer << std::endl;
    // if (sent<totalbw-2*packetsize) {
    //   // non-full throughput
    //   nonfull_thpt_buffer.insert(cmsa);
    //   auto it = full_thpt_buffer.lower_bound(cmsa+1);
    //   full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
    //   // if (is_log) std::cout << Simulator::Now() << ",nonfull," << *full_thpt_buffer.begin() << "," << *full_thpt_buffer.rbegin() << "," << *nonfull_thpt_buffer.begin() << "," << *nonfull_thpt_buffer.rbegin() << std::endl;
    // } else {
    //   // full throughput
    //   full_thpt_buffer.insert(cmsa);
    //   auto it = nonfull_thpt_buffer.upper_bound(cmsa-1);
    //   nonfull_thpt_buffer.erase(it,nonfull_thpt_buffer.end());
    //   // if (is_log) std::cout << Simulator::Now() << ",full," << *full_thpt_buffer.begin() << "," << *full_thpt_buffer.rbegin() << "," << *nonfull_thpt_buffer.begin() << "," << *nonfull_thpt_buffer.rbegin() << std::endl;
    // }
    for (const auto& point : data_points_mp[proberid]) {
      int x = std::get<0>(point);
      int y = std::get<1>(point);
      if (y<totalbw-2*packetsize) {
        // non-full throughput
        nonfull_thpt_buffer.insert(x);
        auto it = full_thpt_buffer.lower_bound(x+1);
        full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
      } else {
        // full throughput
        full_thpt_buffer.insert(x);
        auto it = nonfull_thpt_buffer.upper_bound(x-1);
        nonfull_thpt_buffer.erase(it,nonfull_thpt_buffer.end());
      }
    }

    // data processing
    // std::map<uint32_t, std::vector<uint32_t>> grouped_by_x;
    // for (const auto& point : data_points_mp[proberid]) {
    //     int x = std::get<0>(point);
    //     int y = std::get<1>(point);
    //     grouped_by_x[x].push_back(y);
    // }

    // std::deque<std::tuple<uint32_t,uint32_t>> averaged_points;
    // for (const auto& entry : grouped_by_x) {
    //   uint32_t x = entry.first;
    //   const std::vector<uint32_t>& y_values = entry.second;
      
    //   int sum_y = 0;
    //   for (int y : y_values) {
    //       sum_y += y;
    //   }
    //   uint32_t avg_y = sum_y / y_values.size();

    //   if (avg_y<totalbw-packetsize*2) averaged_points.push_back(std::make_tuple(x, avg_y));
    // }

    // linear regression
    double m=0, b=0, target=0;
    // if (averaged_points.size()>=2) {
    //   double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
    //   int n = averaged_points.size();

    //   for (const auto& point : averaged_points) {
    //       double x = std::get<0>(point);
    //       double y = std::get<1>(point);
    //       sum_x += x;
    //       sum_y += y;
    //       sum_x2 += x * x;
    //       sum_xy += x * y;
    //   }

    //   double denom = n * sum_x2 - sum_x * sum_x;
    //   if (denom == 0) {
    //     // sharedMemory->allocateBufferSpaceSimple(proberid,1500);
    //   } else {
    //     m = (n * sum_xy - sum_x * sum_y) / denom;
    //     b = (sum_y * sum_x2 - sum_x * sum_xy) / denom;
    //     if (m<=0 || b<=0) {
    //       // data_points.clear();
    //       // nonfull_thpt_buffer.clear();
    //       // full_thpt_buffer.clear();
    //       // move_by_thpt = true;
    //       // AnnC: target == 0
    //     } else {
    //       target = (std::floor((totalbw-b)/m/1500.0)+2)*1500; 
    //     }
    //   }
    // } else {
    //   // move_by_thpt = true;
    //   // AnnC: target == 0
    // }
    
    // move by record
    bool move_by_thpt = false;
    // uint32_t move_by_thpt_minbuffer = configbuffer;
    bool should_explore = false;
    bool is_same_thpt_stay_count = false;

    bool should_increment_increase_count = false;
    bool should_increment_decrease_count = false;
    bool should_increment_decrease_count_wait = false;

    uint32_t currmin_full_buffer=0, currmax_nonfull_buffer=0;
    if (full_thpt_buffer.size()>0) currmin_full_buffer = *full_thpt_buffer.begin();
    if (nonfull_thpt_buffer.size()>0) currmax_nonfull_buffer = *nonfull_thpt_buffer.rbegin();

    if (currmin_full_buffer!=0 && currmax_nonfull_buffer!=0) {
      if (currmin_full_buffer<=currmax_nonfull_buffer+packetsize) {
        if (same_thpt_stay_count_mp[proberid]<20) {
          sharedMemory->allocateBufferSpaceSimple(proberid,currmin_full_buffer-cmsa);
          if (prev_cmsa_mp[proberid]==cmsa) is_same_thpt_stay_count = true;
        } else {
          // move_by_thpt = true;
          // move_by_thpt_minbuffer = same_thpt_minbuffer;
          should_explore = true;
        }
      } else {
        // if (target > 0) {
        //   if (target>=currmin_full_buffer) {
        //     // sharedMemory->allocateBufferSpaceSimple(proberid,currmin_full_buffer-1500-cmsa);
        //     move_by_thpt = true;
        //   } else if (target<=currmax_nonfull_buffer) {
        //     // sharedMemory->allocateBufferSpaceSimple(proberid,currmax_nonfull_buffer+1500-cmsa);
        //     move_by_thpt = true;
        //   } else {
        //     if (target>=4*packetsize) { // AnnC: target>=4*packetsize
        //       sharedMemory->allocateBufferSpaceSimple(proberid,target-cmsa);
        //     }
        //   }
        // } else {
          move_by_thpt = true;
        // }
      } 
    } else {
      // if (currmin_full_buffer==0) {
      //   if (target>=4*packetsize && target>currmax_nonfull_buffer) { // AnnC: target>=4*packetsize
      //     should_increment_increase_count = true;
      //     if (move_by_thpt_increase_count < 3) {
      //       sharedMemory->allocateBufferSpaceSimple(proberid,std::min(packetsize*5,target-cmsa));
      //     } else {
      //       sharedMemory->allocateBufferSpaceSimple(proberid,target-cmsa);
      //     }
      //   } else {
      //     move_by_thpt = true;
      //   }
      // } else if (currmax_nonfull_buffer==0) {
        move_by_thpt = true;
      // }
    } 

    // move by throughput 
    if (prev_cmsa_mp[proberid]==cmsa) {
      thpt_stay_count_mp[proberid]+=1;
      // if (minbuffer < thpt_minbuffer) thpt_minbuffer = minbuffer;
      thpt_minbuffer_vec_mp[proberid].push_back(minbuffer);
    } else {
      thpt_stay_count_mp[proberid] = 0;
      // thpt_minbuffer = configbuffer;
      thpt_minbuffer_vec_mp[proberid].clear();
    }
    prev_cmsa_mp[proberid] = cmsa;

    if (move_by_thpt) {
      uint32_t cmsa_after = cmsa;
      if (sent<totalbw-packetsize*2) {
        should_increment_increase_count = true;
        if (move_by_thpt_increase_count_mp[proberid] < 3) {
          cmsa_after = cmsa + (uint32_t)std::min(packetsize*5,((totalbw-1-packetsize-sent)/10));
        } else {
          cmsa_after = cmsa + (uint32_t)((totalbw-1-packetsize-sent)/10);
        }
        // sharedMemory->allocateBufferSpaceSimple(proberid,(uint32_t)std::min(packetsize*5,((totalbw-1-packetsize-sent)/10)));
      } else {
        // if (same_thpt_minbuffer<configbuffer) {
        //   if (same_thpt_minbuffer>=packetsize*4) sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)((same_thpt_minbuffer-packetsize*4)/5));
        // } else {
          // if (minbuffer>=packetsize*4) sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)((minbuffer-packetsize*4)/3));

          uint32_t tmb_sum = 0;
          uint32_t tmb_count = thpt_minbuffer_vec_mp[proberid].size();
          for (auto tmb : thpt_minbuffer_vec_mp[proberid]) {
            tmb_sum += tmb;
          }
          double tmb_average = (double)tmb_sum/tmb_count;
          // thpt_minbuffer_mp[proberid] = configbuffer;
          uint32_t thpt_minbuffer = configbuffer;
          for (auto tmb : thpt_minbuffer_vec_mp[proberid]) {
            if (tmb+packetsize*10 < tmb_average) continue; // denoise minbuffer
            // if (tmb < thpt_minbuffer_mp[proberid]) thpt_minbuffer_mp[proberid] = tmb;
            if (tmb < thpt_minbuffer) thpt_minbuffer = tmb;
          }

          // if (thpt_stay_count_mp[proberid]>=5 && thpt_minbuffer_mp[proberid]>=packetsize*4) {
          if (thpt_stay_count_mp[proberid]>=5 && thpt_minbuffer>=packetsize*4) {
            should_increment_decrease_count = true;
            if (move_by_thpt_decrease_count_mp[proberid] < 3) {
              // cmsa_after = cmsa - (uint32_t)std::min(packetsize*5,(thpt_minbuffer_mp[proberid]-packetsize*4)/5);
              cmsa_after = cmsa - (uint32_t)std::min(packetsize*5,(thpt_minbuffer-packetsize*4)/5);
            } else {
              // cmsa_after = cmsa - (uint32_t)((thpt_minbuffer_mp[proberid]-packetsize*4)/5);
              cmsa_after = cmsa - (uint32_t)((thpt_minbuffer-packetsize*4)/5);
            }
            // sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)std::min(packetsize*5,(minbuffer-packetsize*4)/5));
          } else {
            should_increment_decrease_count_wait = true;
          }
        // }
      }

      if (currmax_nonfull_buffer!=0 && cmsa_after < currmax_nonfull_buffer) cmsa_after = currmax_nonfull_buffer + packetsize; 
      if (currmin_full_buffer!=0 && cmsa_after > currmin_full_buffer) cmsa_after = currmin_full_buffer - packetsize;

      sharedMemory->allocateBufferSpaceSimple(proberid,cmsa_after-cmsa);      
    }

    if (should_explore) {
      if (same_thpt_minbuffer_mp[proberid]>=packetsize*4) sharedMemory->allocateBufferSpaceSimple(proberid,-(uint32_t)std::min(packetsize*5,(same_thpt_minbuffer_mp[proberid]-packetsize*4)/5));
    }

    if (should_increment_increase_count) {
      move_by_thpt_increase_count_mp[proberid] += 1;
    } else {
      move_by_thpt_increase_count_mp[proberid] = 0;
    }
    if (should_increment_decrease_count) {
      move_by_thpt_decrease_count_mp[proberid] += 1;
    } else {
      if (!should_increment_decrease_count_wait) move_by_thpt_decrease_count_mp[proberid] = 0;
    }

    if (is_same_thpt_stay_count) {
      same_thpt_stay_count_mp[proberid] += 1;
      if (minbuffer < same_thpt_minbuffer_mp[proberid]) same_thpt_minbuffer_mp[proberid] = minbuffer;
    } else {
      same_thpt_stay_count_mp[proberid] = 0;
      same_thpt_minbuffer_mp[proberid] = configbuffer;
    }

    if (is_log) {
      std::cout << Simulator::Now() << "," << proberid << "," << cmsa << "," << sent << "," << totalbw << "," << minbuffer << "," << data_points_mp[proberid].size() << "," << m << "," << b << "," << target;
      // std::cout << "," << minbuffer << "," << data_points_lat.size() << "," << m_lat << "," << b_lat << "," << target_lat << "," << target_overall;
      std::cout << "," << currmin_full_buffer << "," << currmax_nonfull_buffer;
      std::cout << std::endl;
    }
  }

  if (thptonly_sent_linear_move) {
    // double totalbw = portBW*1000000000/8.0/(nPrior-1)*monitorlongms/1000.0;
    // double totalbw = 6243001; // AnnC: hardcode for now for mi=500
    double totalbw = totalbwmap[monitorlongms];

    uint32_t mycmsa = cmsa;
    // if (maxbuffer<=cmsa-1500) {
    //   std::cout << "test,maxbuffer<=cmsa-1500," << maxbuffer << "," << cmsa << std::endl;
    //   sharedMemory->allocateBufferSpaceSimple(proberid,maxbuffer-cmsa);
    //   cmsa = sharedMemory->getCurrMaxSizeAllowed(proberid);
    // }

    data_points.push_back(std::make_tuple(cmsa,sent));
    if (sent<totalbw-1500*2) {
      // non-full throughput
      nonfull_thpt_buffer.insert(cmsa);
      auto it = full_thpt_buffer.lower_bound(cmsa+1);
      full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
      thpt_stay_count = 0;
      same_thpt_stay_count = 0;
    } else {
      // full throughput
      // v1
      // bool should_add = true;
      // for (auto it=nonfull_thpt_buffer.begin(); it!=nonfull_thpt_buffer.end(); it++) {
      //   if (*it>=cmsa) {
      //     should_add = false;
      //     break;
      //   }
      // }
      // if (should_add) full_thpt_buffer.insert(cmsa);
      // v2
      // full_thpt_buffer.insert(cmsa);
      // auto it = nonfull_thpt_buffer.upper_bound(cmsa-1);
      // nonfull_thpt_buffer.erase(it,nonfull_thpt_buffer.end());
      // v3
      if (thpt_stay_count>=3) {
        full_thpt_buffer.insert(cmsa);
        auto it = nonfull_thpt_buffer.upper_bound(cmsa-1);
        nonfull_thpt_buffer.erase(it,nonfull_thpt_buffer.end());
      }
      thpt_stay_count += 1;
      if (mycmsa == prev_cmsa) same_thpt_stay_count += 1;
      prev_cmsa = mycmsa;
    }

    std::map<uint32_t, std::vector<uint32_t>> grouped_by_x;
    for (const auto& point : data_points) {
        int x = std::get<0>(point);
        int y = std::get<1>(point);
        grouped_by_x[x].push_back(y);
    }

    std::deque<std::tuple<uint32_t,uint32_t>> averaged_points;
    for (const auto& entry : grouped_by_x) {
      uint32_t x = entry.first;
      const std::vector<uint32_t>& y_values = entry.second;
      
      int sum_y = 0;
      for (int y : y_values) {
          sum_y += y;
      }
      uint32_t avg_y = sum_y / y_values.size();

      if (avg_y<totalbw-1500*2) averaged_points.push_back(std::make_tuple(x, avg_y));
    }

    double m=0, b=0, target=0;
    uint32_t currmin_full_buffer=0, currmax_nonfull_buffer=0;
    bool move_by_thpt = false;
    if (averaged_points.size()>=2) {
      // linear regression
      double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
      int n = averaged_points.size();

      for (const auto& point : averaged_points) {
          double x = std::get<0>(point);
          double y = std::get<1>(point);
          sum_x += x;
          sum_y += y;
          sum_x2 += x * x;
          sum_xy += x * y;
      }

      double denom = n * sum_x2 - sum_x * sum_x;
      if (denom == 0) {
        sharedMemory->allocateBufferSpaceSimple(proberid,1500);
      } else {
        m = (n * sum_xy - sum_x * sum_y) / denom;
        b = (sum_y * sum_x2 - sum_x * sum_xy) / denom;
        if (m<=0 || b<=0) {
          data_points.clear();
          nonfull_thpt_buffer.clear();
          full_thpt_buffer.clear();
          move_by_thpt = true;
        } else {
          target = (std::floor((totalbw-b)/m/1500.0)+2)*1500;

          if (full_thpt_buffer.size()>0) currmin_full_buffer = *full_thpt_buffer.begin();
          if (nonfull_thpt_buffer.size()>0) currmax_nonfull_buffer = *nonfull_thpt_buffer.rbegin();

          if (currmin_full_buffer!=0 && currmax_nonfull_buffer!=0) {
            if (currmin_full_buffer<=currmax_nonfull_buffer+1500) {
              if (same_thpt_stay_count < 50) {
                sharedMemory->allocateBufferSpaceSimple(proberid,currmin_full_buffer-cmsa);
              } else {
                sharedMemory->allocateBufferSpaceSimple(proberid,currmin_full_buffer-1500-cmsa);
                nonfull_thpt_buffer.erase(currmax_nonfull_buffer);
              }
            } else {
              if (target>=currmin_full_buffer) {
                sharedMemory->allocateBufferSpaceSimple(proberid,currmin_full_buffer-1500-cmsa);
              } else if (target<=currmax_nonfull_buffer) {
                sharedMemory->allocateBufferSpaceSimple(proberid,currmax_nonfull_buffer+1500-cmsa);
              } else {
                if (target>=1500) sharedMemory->allocateBufferSpaceSimple(proberid,target-cmsa);
              }
            } 
          } else {
            if (currmin_full_buffer==0) {
              if (target>=1500 && target>currmax_nonfull_buffer) {
                sharedMemory->allocateBufferSpaceSimple(proberid,target-cmsa);
              } else {
                move_by_thpt = true;
              }
            } else if (currmax_nonfull_buffer==0) {
              move_by_thpt = true;
            }
          }        
        }
      }
    } else {
      move_by_thpt = true;
    }

    if (move_by_thpt) {
      // if (cmsa>1500*2) sharedMemory->allocateBufferSpaceSimple(proberid,-1500);
      if (sent<totalbw-1500*2) {
        sharedMemory->allocateBufferSpaceSimple(proberid,(uint32_t)((totalbw-1-1500-sent)/10));
        std::cout << "test,move_by_thpt," << (uint32_t)((totalbw-1-1500-sent)/10) << std::endl;
      } else {
        if (thpt_stay_count>=3) sharedMemory->allocateBufferSpaceSimple(proberid,-1500);
      }
    }

    if (is_log) {
      std::cout << Simulator::Now() << "," << cmsa << "," << sent << "," << totalbw << "," << data_points.size() << "," << m << "," << b << "," << target;
      // std::cout << "," << minbuffer << "," << data_points_lat.size() << "," << m_lat << "," << b_lat << "," << target_lat << "," << target_overall;
      std::cout << "," << currmin_full_buffer << "," << currmax_nonfull_buffer;
      std::cout << std::endl;
    }
  }

  if (thptlat_sent_minqlen_linear_naive) {
    double totalbw = portBW*1000000000/8.0/(nPrior-1)*monitorlongms/1000.0;
    if (sent<totalbw-1500*2) {
      // non-full throughput
      data_points.push_back(std::make_tuple(cmsa,sent));
      nonfull_thpt_buffer.insert(cmsa);
      auto it = full_thpt_buffer.lower_bound(cmsa+1);
      full_thpt_buffer.erase(full_thpt_buffer.begin(),it);
      // for (auto it=full_thpt_buffer.begin(); it!=full_thpt_buffer.end(); ) {
      //   if (*it<=cmsa) {
      //     full_thpt_buffer.erase(*it);
      //   } else {
      //     it++;
      //   }
      // }
      data_points_lat.erase(std::remove_if(data_points_lat.begin(), data_points_lat.end(), [cmsa](const auto& point) {
        int x = std::get<0>(point);
        return x <= cmsa;
      }), data_points_lat.end());
      // for (auto it = data_points_lat.begin(); it != data_points_lat.end(); ) {
      //     if (std::get<0>(*it)<=cmsa) {
      //       it = data_points_lat.erase(it);
      //     } else {
      //       it++;
      //     }
      // }
    } else {
      // full throughput
      data_points_lat.push_back(std::make_tuple(cmsa,minbuffer));
      bool should_add = true;
      for (auto it=nonfull_thpt_buffer.begin(); it!=nonfull_thpt_buffer.end(); it++) {
        if (*it>=cmsa) {
          should_add = false;
          break;
        }
      }
      if (should_add) full_thpt_buffer.insert(cmsa);
    }

    std::map<uint32_t, std::vector<uint32_t>> grouped_by_x;
    for (const auto& point : data_points) {
        int x = std::get<0>(point);
        int y = std::get<1>(point);
        grouped_by_x[x].push_back(y);
    }

    std::deque<std::tuple<uint32_t,uint32_t>> averaged_points;
    for (const auto& entry : grouped_by_x) {
        uint32_t x = entry.first;
        const std::vector<uint32_t>& y_values = entry.second;
        
        int sum_y = 0;
        for (int y : y_values) {
            sum_y += y;
        }
        uint32_t avg_y = sum_y / y_values.size();

        averaged_points.push_back(std::make_tuple(x, avg_y));
    }

    double m=0, b=0, target=0, target_overall=0;
    double m_lat=0, b_lat=0, target_lat=0;
    uint32_t currmin_full_buffer=0, currmax_nonfull_buffer=0;
    if (grouped_by_x.size()>=2) {
      // linear regression
      double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
      int n = averaged_points.size();

      for (const auto& point : averaged_points) {
          double x = std::get<0>(point);
          double y = std::get<1>(point);
          sum_x += x;
          sum_y += y;
          sum_x2 += x * x;
          sum_xy += x * y;
      }

      double denom = n * sum_x2 - sum_x * sum_x;
      if (denom == 0) {
        sharedMemory->allocateBufferSpaceSimple(proberid,1500);
      } else {
        m = (n * sum_xy - sum_x * sum_y) / denom;
        b = (sum_y * sum_x2 - sum_x * sum_xy) / denom;
        if (m<=0 || b<=0) {
          data_points.clear();
          nonfull_thpt_buffer.clear();
          full_thpt_buffer.clear();
        } else {
          target = (std::floor((totalbw-b)/m/1500.0)+2)*1500;

          // lat,minqlen
          currmin_full_buffer = *full_thpt_buffer.begin();
          currmax_nonfull_buffer = *nonfull_thpt_buffer.rbegin();

          std::map<uint32_t, std::vector<uint32_t>> grouped_by_x_lat;
          for (const auto& point : data_points_lat) {
              int x = std::get<0>(point);
              int y = std::get<1>(point);
              grouped_by_x_lat[x].push_back(y);
          }

          std::deque<std::tuple<uint32_t,uint32_t>> averaged_points_lat;
          for (const auto& entry : grouped_by_x_lat) {
              uint32_t x = entry.first;
              const std::vector<uint32_t>& y_values = entry.second;
              
              int sum_y = 0;
              for (int y : y_values) {
                  sum_y += y;
              }
              uint32_t avg_y = sum_y / y_values.size();

              averaged_points_lat.push_back(std::make_tuple(x, avg_y));
          }

          if (grouped_by_x_lat.size()>=2) {
            // linear regression
            double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
            int n = averaged_points_lat.size();

            for (const auto& point : averaged_points_lat) {
              double x = std::get<0>(point);
              double y = std::get<1>(point);
              sum_x += x;
              sum_y += y;
              sum_x2 += x * x;
              sum_xy += x * y;
            }

            double denom = n * sum_x2 - sum_x * sum_x;
            if (denom!=0) {
              m_lat = (n * sum_xy - sum_x * sum_y) / denom;
              b_lat = (sum_y * sum_x2 - sum_x * sum_xy) / denom;
              if (m_lat<=0 || b_lat>=0) {
                data_points_lat.clear();
              } else {
                target_lat = (std::floor((-1500*2-b_lat)/m_lat/1500.0)+2)*1500;
                if (target_lat<=currmax_nonfull_buffer) data_points_lat.clear();
              }
            }
          } else {
            target_lat = target-1500;
          }

          if (target>=currmin_full_buffer && target_lat>currmax_nonfull_buffer) {
            target_overall = std::min(target,target_lat);
          } else {
            target_overall = target;
          }
          if (target_overall>=1500) sharedMemory->allocateBufferSpaceSimple(proberid,target_overall-cmsa);
        }
      }
    } else {
      if (cmsa>1500*2) sharedMemory->allocateBufferSpaceSimple(proberid,-1500);
    }

    if (is_log) {
      std::cout << Simulator::Now() << "," << cmsa << "," << sent << "," << totalbw << "," << data_points.size() << "," << m << "," << b << "," << target;
      std::cout << "," << minbuffer << "," << data_points_lat.size() << "," << m_lat << "," << b_lat << "," << target_lat << "," << target_overall;
      std::cout << "," << currmin_full_buffer << "," << currmax_nonfull_buffer;
      std::cout << std::endl;
    }
  }

  if (thptonly_sent_linear_regression_naive) {
    // uint16_t timeoutwindowms = 10000;
    // if (data_points.size()>=timeoutwindowms/monitorlongms) data_points.pop_front();
    double totalbw = portBW*1000000000/8.0/(nPrior-1)*monitorlongms/1000.0;
    if (sent<totalbw-1500*2) data_points.push_back(std::make_tuple(cmsa,sent));

    std::map<uint32_t, std::vector<uint32_t>> grouped_by_x;
    for (const auto& point : data_points) {
        int x = std::get<0>(point);
        int y = std::get<1>(point);
        grouped_by_x[x].push_back(y);
    }

    std::deque<std::tuple<uint32_t,uint32_t>> averaged_points;
    for (const auto& entry : grouped_by_x) {
        uint32_t x = entry.first;
        const std::vector<uint32_t>& y_values = entry.second;
        
        int sum_y = 0;
        for (int y : y_values) {
            sum_y += y;
        }
        uint32_t avg_y = sum_y / y_values.size();

        averaged_points.push_back(std::make_tuple(x, avg_y));
    }

    double m=0, b=0, target=0;
    if (grouped_by_x.size()>=2) {
      // linear regression
      double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
      int n = averaged_points.size();

      for (const auto& point : averaged_points) {
          double x = std::get<0>(point);
          double y = std::get<1>(point);
          sum_x += x;
          sum_y += y;
          sum_x2 += x * x;
          sum_xy += x * y;
      }

      double denom = n * sum_x2 - sum_x * sum_x;
      if (denom == 0) {
        sharedMemory->allocateBufferSpaceSimple(proberid,1500);
      } else {
        m = (n * sum_xy - sum_x * sum_y) / denom;
        b = (sum_y * sum_x2 - sum_x * sum_xy) / denom;
        if (m<=0 || b<=0) {
          data_points.clear();
        } else {
          target = (std::floor((totalbw-b)/m/1500.0)+2)*1500;
          if (target>=1500) sharedMemory->allocateBufferSpaceSimple(proberid,target-cmsa);
        }
      }
    } else {
      if (cmsa>1500*2) sharedMemory->allocateBufferSpaceSimple(proberid,-1500);
    }

    if (is_log) std::cout << Simulator::Now() << "," << cmsa << "," << sent << "," << totalbw << "," << data_points.size() << "," << m << "," << b << "," << target << std::endl;
  }

  if (isDropInclusive) {
    // std::cout << Simulator::Now() << ": dropInclusive, minbuffer=" << minbuffer << ", maxbuffer=" << maxbuffer << ", droprate=" << droprate << ", drop=" << drop << std::endl;
    // std::cout << "LOG " << Simulator::Now() << " " << minbuffer << std::endl;
    if ((minbuffer>=absoluteMinBuffer) && (droprate<=dropRateThreshold)) {
      if (minbuffer>absoluteMinBuffer) {
        if (adaptiveDecreaseParameter==0) {
          sharedMemory->allocateBufferSpaceSimple(proberid, -1500);
        } else {
          sharedMemory->allocateBufferSpaceSimple(proberid, std::min(static_cast<int32_t>(std::round(-(minbuffer/adaptiveDecreaseParameter))),static_cast<int32_t>(-1500)));
        }
      } else {
        // pass
      }
    } else {
      // if (drop==0) {
      //   sharedMemory->allocateBufferSpaceSimple(proberid, maxbuffer-cmsa);
      // } else {
      // }
      if (adaptiveIncreaseParameter==0) {
        sharedMemory->allocateBufferSpaceSimple(proberid, 1500);
      } else {
        sharedMemory->allocateBufferSpaceSimple(proberid, std::min(static_cast<int32_t>(std::round(drop/adaptiveIncreaseParameter)),static_cast<int32_t>(cmsa)));
      }
    }
  }

  if (isAdaptive) {
    if (minbuffer > absoluteMinBuffer) {
      sharedMemory->allocateBufferSpaceSimple(proberid, static_cast<int32_t>(std::round(-(minbuffer/adaptiveDecreaseParameter))));
    } else if (minbuffer < absoluteMinBuffer) {
      // if (drop==0) {
      //   sharedMemory->allocateBufferSpaceSimple(proberid, maxbuffer-cmsa);
      // } else {
      //   sharedMemory->allocateBufferSpaceSimple(proberid, std::min(static_cast<int32_t>(std::round(drop/4.0)),static_cast<int32_t>(cmsa)));
      // }
      sharedMemory->allocateBufferSpaceSimple(proberid, std::min(static_cast<int32_t>(std::round(drop/adaptiveIncreaseParameter)),static_cast<int32_t>(cmsa)));
    } else {
      // pass
    }
  }

  if (isGeneric) {
    if (droprate > 0) {
      sharedMemory->allocateBufferSpaceSimple(proberid, 1500);
    } else {
      if (cmsa < minQlenPossible) {
        minQlenPossible = cmsa;
        sharedMemory->allocateBufferSpaceSimple(proberid, -1500);
      } else if (cmsa > minQlenPossible) {
        sharedMemory->allocateBufferSpaceSimple(proberid, -1500);
      } else {
        // pass
      }
    }
  }

  if (isCubic) {
    if (minbuffer > absoluteMinBuffer) {
      sharedMemory->allocateBufferSpaceSimple(proberid, -1500);
    } else if (minbuffer < absoluteMinBuffer) {
      sharedMemory->allocateBufferSpaceSimple(proberid, 1500);
    } else {
      // pass
    }
  }

  if (isBBRCopa) {
    if (droprate > 0) {
      sharedMemory->allocateBufferSpaceSimple(proberid, 1500);
    } else {
      uint32_t mymaxbuffer = std::max(maxbuffer,prevmaxbuffer);
      if (mymaxbuffer<1500) mymaxbuffer = 1500;
      sharedMemory->setCurrMaxSizeAllowed(proberid, mymaxbuffer);
      sharedMemory->checkChangeInCurrMaxSizeAllowed(proberid,cmsa,mymaxbuffer);
    }
  }

  // if (isPrevious) {
  //   if (not sharedMemory->getDoMonitorDrop(proberid)) {
  //     if (prevdroprate==0 && drop==0) {
  //       uint32_t mymaxbuffer = std::max(maxbuffer,prevmaxbuffer);
  //       if (mymaxbuffer<1500) mymaxbuffer = 1500;
  //       sharedMemory->setMinBufferThreshold(proberid,mymaxbuffer);
  //       sharedMemory->setCurrMaxSizeAllowed(proberid, mymaxbuffer);
  //       sharedMemory->checkChangeInCurrMaxSizeAllowed(proberid,cmsa,mymaxbuffer);
  //     } else {
  //       if (minbuffer < absoluteMinBuffer) {
  //         if (currMinBufferThreshold <= maxbuffer) sharedMemory->setMinBufferThreshold(proberid,1);
  //         // if (drop > 0 and maxbuffer+1500 > cmsa) sharedMemory->allocateBufferSpaceSimple(proberid, 1500);
  //         if (drop > 0) sharedMemory->allocateBufferSpaceSimple(proberid, 1500);
  //       } else if (minbuffer > absoluteMinBuffer) {
  //         sharedMemory->allocateBufferSpaceSimple(proberid, -1500);
  //       } else {
  //         sharedMemory->setMinBufferThreshold(proberid,maxbuffer);
  //         sharedMemory->setCurrMaxSizeAllowed(proberid, maxbuffer);
  //         sharedMemory->checkChangeInCurrMaxSizeAllowed(proberid,cmsa,maxbuffer);
  //       } 
  //     }

  //     // if (prevdroprate==0 && drop==0) {
  //     //   uint32_t mymaxbuffer = std::max(maxbuffer,prevmaxbuffer);
  //     //   if (mymaxbuffer<1500) mymaxbuffer = 1500;
  //     //   sharedMemory->setMinBufferThreshold(proberid,mymaxbuffer);
  //     //   sharedMemory->setCurrMaxSizeAllowed(proberid, mymaxbuffer);
  //     //   sharedMemory->checkChangeInCurrMaxSizeAllowed(proberid,cmsa,mymaxbuffer);
  //     // }

  //     // if (droprate < goodDroprate) {
  //     //   sharedMemory->allocateBufferSpaceSimple(proberid,1500);
  //     // } else {
  //     //   if (minbuffer > absoluteMinBuffer) {
  //     //     sharedMemory->allocateBufferSpaceSimple(proberid,-1500);
  //     //   }
  //     // }
  //   }
  // }

  /**************************** 
   * For COS597K final project
  *****************************/
  if (bmType == LLM_TTR_COT_0) {
    uint32_t packetSize = 1500;
    // double totalbw = portBW * 1000000000 / 8.0 / (nPrior - 1) * monitorlongms / 1000.0;
    double totalbw = 6243001;

    // Add data point
    data_points.push_back(std::make_tuple(cmsa, sent));

    if (sent < totalbw - packetSize * 2) {
        nonfull_thpt_buffer.insert(cmsa);
        auto it = full_thpt_buffer.lower_bound(cmsa + 1);
        full_thpt_buffer.erase(full_thpt_buffer.begin(), it);
    } else {
        bool should_add = true;
        for (auto it = nonfull_thpt_buffer.begin(); it != nonfull_thpt_buffer.end(); ++it) {
            if (*it >= cmsa) {
                should_add = false;
                break;
            }
        }
        if (should_add) full_thpt_buffer.insert(cmsa);
    }

    // Group by cmsa and calculate averages
    std::map<uint32_t, std::vector<uint32_t>> grouped_by_x;
    for (const auto& point : data_points) {
        grouped_by_x[std::get<0>(point)].push_back(std::get<1>(point));
    }

    std::deque<std::tuple<uint32_t, uint32_t>> averaged_points;
    for (const auto& entry : grouped_by_x) {
        uint32_t x = entry.first;
        const std::vector<uint32_t>& y_values = entry.second;
        uint32_t avg_y = std::accumulate(y_values.begin(), y_values.end(), 0) / y_values.size();
        if (avg_y < totalbw - packetSize * 2) averaged_points.push_back(std::make_tuple(x, avg_y));
    }

    static double smoothed_target = 0;
    if (averaged_points.size() >= 2) {
        // Weighted linear regression with smarter weighting
        double sum_wx = 0, sum_wy = 0, sum_wx2 = 0, sum_wxy = 0, sum_w = 0;
        int n = averaged_points.size();
        double weight_factor = 0.9; // Exponential weight decay

        for (const auto& point : averaged_points) {
            double x = std::get<0>(point);
            double y = std::get<1>(point);
            double weight = std::pow(weight_factor, n--) * std::abs(y - totalbw); // Weight by recency and throughput deviation
            sum_wx += weight * x;
            sum_wy += weight * y;
            sum_wx2 += weight * x * x;
            sum_wxy += weight * x * y;
            sum_w += weight;
        }

        double denom = sum_w * sum_wx2 - sum_wx * sum_wx;
        if (denom != 0) {
            double m = (sum_w * sum_wxy - sum_wx * sum_wy) / denom;
            double b = (sum_wy * sum_wx2 - sum_wx * sum_wxy) / denom;

            if (m > 0 && b > 0) {
                double target = (std::floor((totalbw - b) / m / packetSize) + 2) * packetSize;
                smoothed_target = smoothed_target == 0 
                                    ? target 
                                    : smoothed_target * 0.9 + target * 0.1; // Proper smoothing
            } else {
                // Graceful fallback: Use backup heuristic
                smoothed_target = cmsa + packetSize * 2;
            }
        }
    } else {
        // Improved insufficient data handling
        if (sent < totalbw - packetSize * 2) {
            smoothed_target = cmsa + packetSize;
        } else if (cmsa > packetSize * 2) {
            smoothed_target = cmsa - packetSize;
        }
    }

    // Use smoothed target
    if (smoothed_target >= packetSize) {
        sharedMemory->allocateBufferSpaceSimple(proberid, smoothed_target - cmsa);
    }
  }

  if (bmType == LLM_TTR_COT_1) {
    uint32_t packetSize = 1500;
    // double totalbw = portBW * 1000000000 / 8.0 / (nPrior - 1) * monitorlongms / 1000.0;
    double totalbw = 6243001;
    data_points.push_back(std::make_tuple(cmsa, sent));

    if (sent < totalbw - packetSize * 2) {
        nonfull_thpt_buffer.insert(cmsa);
        auto it = full_thpt_buffer.lower_bound(cmsa + 1);
        full_thpt_buffer.erase(full_thpt_buffer.begin(), it);
    } else {
        bool should_add = true;
        for (auto it = nonfull_thpt_buffer.begin(); it != nonfull_thpt_buffer.end(); it++) {
            if (*it >= cmsa) {
                should_add = false;
                break;
            }
        }
        if (should_add) full_thpt_buffer.insert(cmsa);
    }

    std::map<uint32_t, std::vector<uint32_t>> grouped_by_x;
    for (const auto& point : data_points) {
        int x = std::get<0>(point);
        int y = std::get<1>(point);
        grouped_by_x[x].push_back(y);
    }

    std::deque<std::tuple<uint32_t, uint32_t>> averaged_points;
    for (const auto& entry : grouped_by_x) {
        uint32_t x = entry.first;
        const std::vector<uint32_t>& y_values = entry.second;

        int sum_y = 0;
        for (int y : y_values) {
            sum_y += y;
        }
        uint32_t avg_y = sum_y / y_values.size();

        if (avg_y < totalbw - packetSize * 2) averaged_points.push_back(std::make_tuple(x, avg_y));
    }

    double m = 0, b = 0, target = 0;
    uint32_t currmin_full_buffer = 0, currmax_nonfull_buffer = 0;

    if (averaged_points.size() >= 2) {
        double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
        int n = averaged_points.size();

        for (const auto& point : averaged_points) {
            double x = std::get<0>(point);
            double y = std::get<1>(point);
            sum_x += x;
            sum_y += y;
            sum_x2 += x * x;
            sum_xy += x * y;
        }

        double denom = n * sum_x2 - sum_x * sum_x;
        if (denom != 0) {
            m = (n * sum_xy - sum_x * sum_y) / denom;
            b = (sum_y * sum_x2 - sum_x * sum_xy) / denom;

            if (m > 0 && b > 0) {
                target = (std::floor((totalbw - b) / m / (double)packetSize) + 2) * packetSize;

                if (full_thpt_buffer.size() > 0) currmin_full_buffer = *full_thpt_buffer.begin();
                if (nonfull_thpt_buffer.size() > 0) currmax_nonfull_buffer = *nonfull_thpt_buffer.rbegin();

                if (currmin_full_buffer != 0 && currmax_nonfull_buffer != 0) {
                    double mid_target = (currmin_full_buffer + currmax_nonfull_buffer) / 2;

                    if (currmin_full_buffer == currmax_nonfull_buffer + packetSize) {
                        if (stay_count < 100) {
                            sharedMemory->allocateBufferSpaceSimple(proberid, currmin_full_buffer - cmsa);
                            stay_count++;
                        } else {
                            sharedMemory->allocateBufferSpaceSimple(proberid, currmin_full_buffer - packetSize - cmsa);
                            nonfull_thpt_buffer.erase(currmax_nonfull_buffer);
                            stay_count = 0;
                        }
                    } else {
                        stay_count = 0;
                        if (target >= currmin_full_buffer) {
                            sharedMemory->allocateBufferSpaceSimple(proberid, currmin_full_buffer - packetSize - cmsa);
                        } else if (target <= currmax_nonfull_buffer) {
                            sharedMemory->allocateBufferSpaceSimple(proberid, currmax_nonfull_buffer + packetSize - cmsa);
                        } else {
                            sharedMemory->allocateBufferSpaceSimple(proberid, target - cmsa);
                        }
                    }
                } else {
                    sharedMemory->allocateBufferSpaceSimple(proberid, target - cmsa);
                }
            } else {
                // Handle regression issues: Remove outliers selectively
                auto it = std::max_element(data_points.begin(), data_points.end(), 
                    [](const auto& a, const auto& b) { 
                        return std::get<1>(a) < std::get<1>(b); 
                    });
                if (it != data_points.end()) data_points.erase(it);
                nonfull_thpt_buffer.clear();
                full_thpt_buffer.clear();
                stay_count = 0;
            }
        } else {
            sharedMemory->allocateBufferSpaceSimple(proberid, packetSize);
        }
    } else {
        if (sent < totalbw - packetSize * 2) {
            sharedMemory->allocateBufferSpaceSimple(proberid, packetSize);
        } else {
            if (cmsa > packetSize * 2) sharedMemory->allocateBufferSpaceSimple(proberid, -packetSize);
        }
    }
  }

  if (bmType == LLM_TTR_COT_2) {
    uint32_t packetSize = 1500;  // Packet size in bytes
    // double totalbw = portBW * 1000000000 / 8.0 / (nPrior - 1) * monitorlongms / 1000.0;  // Total bandwidth
    double totalbw = 6243001;
    data_points.push_back(std::make_tuple(cmsa, sent));

    if (sent < totalbw - packetSize * 2) {
        nonfull_thpt_buffer.insert(cmsa);
        auto it = full_thpt_buffer.lower_bound(cmsa + 1);
        full_thpt_buffer.erase(full_thpt_buffer.begin(), it);
    } else {
        bool should_add = true;
        for (auto it = nonfull_thpt_buffer.begin(); it != nonfull_thpt_buffer.end(); it++) {
            if (*it >= cmsa) {
                should_add = false;
                break;
            }
        }
        if (should_add) full_thpt_buffer.insert(cmsa);
    }

    // Group data points by cmsa and calculate smoothed averages
    std::map<uint32_t, std::vector<uint32_t>> grouped_by_x;
    for (const auto& point : data_points) {
        int x = std::get<0>(point);
        int y = std::get<1>(point);
        grouped_by_x[x].push_back(y);
    }

    std::deque<std::tuple<uint32_t, uint32_t>> averaged_points;
    for (const auto& entry : grouped_by_x) {
        uint32_t x = entry.first;
        const std::vector<uint32_t>& y_values = entry.second;

        // Exponential smoothing for averaging
        double smoothed_y = 0;
        double alpha = 0.7;  // Smoothing factor
        for (int y : y_values) {
            smoothed_y = alpha * y + (1 - alpha) * smoothed_y;
        }
        uint32_t avg_y = static_cast<uint32_t>(smoothed_y);

        if (avg_y < totalbw - 1500 * 2) {
            averaged_points.push_back(std::make_tuple(x, avg_y));
        }
    }

    double m = 0, b = 0, target = 0;
    uint32_t currmin_full_buffer = 0, currmax_nonfull_buffer = 0;

    if (averaged_points.size() >= 2) {
        double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
        int n = averaged_points.size();

        for (const auto& point : averaged_points) {
            double x = std::get<0>(point);
            double y = std::get<1>(point);
            sum_x += x;
            sum_y += y;
            sum_x2 += x * x;
            sum_xy += x * y;
        }

        double denom = n * sum_x2 - sum_x * sum_x;
        if (denom != 0) {
            m = (n * sum_xy - sum_x * sum_y) / denom;
            b = (sum_y * sum_x2 - sum_x * sum_xy) / denom;

            if (m > 0 && b > 0) {
                target = (std::floor((totalbw - b) / m / (double)packetSize) + 2) * packetSize;

                if (full_thpt_buffer.size() > 0) currmin_full_buffer = *full_thpt_buffer.begin();
                if (nonfull_thpt_buffer.size() > 0) currmax_nonfull_buffer = *nonfull_thpt_buffer.rbegin();

                if (currmin_full_buffer != 0 && currmax_nonfull_buffer != 0) {
                    if (currmin_full_buffer == currmax_nonfull_buffer + packetSize) {
                        if (stay_count < 100) {
                            sharedMemory->allocateBufferSpaceSimple(proberid, currmin_full_buffer - cmsa);
                            stay_count++;
                        } else {
                            sharedMemory->allocateBufferSpaceSimple(proberid, currmin_full_buffer - packetSize - cmsa);
                            nonfull_thpt_buffer.erase(currmax_nonfull_buffer);
                            stay_count = 0;
                        }
                    } else {
                        stay_count = 0;
                        if (target >= currmin_full_buffer) {
                            sharedMemory->allocateBufferSpaceSimple(proberid, currmin_full_buffer - packetSize - cmsa);
                        } else if (target <= currmax_nonfull_buffer) {
                            sharedMemory->allocateBufferSpaceSimple(proberid, currmax_nonfull_buffer + packetSize - cmsa);
                        } else if (target >= packetSize) {
                            sharedMemory->allocateBufferSpaceSimple(proberid, target - cmsa);
                        }
                    }
                } else {
                    if (target >= packetSize) sharedMemory->allocateBufferSpaceSimple(proberid, target - cmsa);
                }
            } else {
                data_points.clear();
                nonfull_thpt_buffer.clear();
                full_thpt_buffer.clear();
            }
        } else {
            sharedMemory->allocateBufferSpaceSimple(proberid, 1500);
        }
    } else {
        if (sent < totalbw - packetSize * 2) {
            sharedMemory->allocateBufferSpaceSimple(proberid, packetSize);
        } else {
            if (cmsa > packetSize * 2) sharedMemory->allocateBufferSpaceSimple(proberid, -packetSize);
        }
    }
  }
 
  probeMinMonitorLongInvoke(proberid,bmType);
}

void GenQueueDisc::probeMinMonitorLongInvoke(uint32_t proberid, uint32_t bmType) {
  uint32_t queueid = proberid % nPrior;
  if (verbose) std::cout << Simulator::Now() << ": probeMinMonitorLongInvoke, proberId=" << proberid << ", queueid=" << queueid << std::endl;
  sharedMemory->probeMinMaxBufferUsed[proberid] = 0;
  sharedMemory->probeMinTotalDropBytes[proberid] = 0;
  sharedMemory->probeMinAverageThroughput[proberid] = 0;
  sharedMemory->probeMinMinBufferUsed[proberid] = 100000000;
  sharedMemory->probeMinDurationZeroQueue[proberid] = 0;
  sharedMemory->probeMinLastTimestampNonZeroQueue[proberid] = -1;
  
  // std::cout << "**DEBUG: 1" << std::endl;
  int64_t now = Simulator::Now().GetMicroSeconds();
  // std::cout << "**DEBUG: 2" << std::endl;
  latestLongCollect[queueid] = now;
  // std::cout << "**DEBUG: 3" << std::endl;

  // sharedMemory->probeMinMaxBufferUsedMonitorMap[proberid].insert( std::pair<int64_t,uint32_t>(now,0) );
  // sharedMemory->probeMinTotalDropBytesMonitorMap[proberid].insert( std::pair<int64_t,uint32_t>(now,0) );

  // std::cout << "****Debug: RTTms*monitorlongNRTT=" << RTTms*monitorlongNRTT << std::endl;
  // Simulator::Schedule(MilliSeconds(sharedMemory->smallestRTTms*monitorlongNRTT), &GenQueueDisc::probeMinMonitorLongCollectSimple, this, proberid);
  Simulator::Schedule(MilliSeconds(monitorlongms), &GenQueueDisc::probeMinMonitorLongCollectSimple, this, proberid, bmType);
}

void GenQueueDisc::startProbing(uint32_t proberid, uint32_t flowid, uint32_t bmType) {
  if (verbose) std::cout << Simulator::Now() << ": startProbing, proberId=" << proberid << ", flowid=" << flowid;
  // if (sharedMemory->getStatus(proberid,flowid)==MRFlowEnd or sharedMemory->getStatus(proberid,flowid)==MRClearPackets) {
  //   if (verbose) std::cout << ", flow has ended" << std::endl;
  //   return;
  // }
  if (verbose) std::cout << std::endl;

  // std::cout << "****Debug: probers in HeadRoom: ";
  // for (int i=0; i<sharedMemory->getTotalProbers(); i++) {
  //   if (sharedMemory->isProberInHeadRoom(i)) std::cout << i << ",";
  // }
  // std::cout << std::endl;
  // std::cout << "*****Debug: probers in WaitRoom: ";
  // for (int i=0; i<sharedMemory->getTotalProbers(); i++) {
  //   if (sharedMemory->isProberInWaitRoom(i)) std::cout << i << ",";
  // }
  // std::cout << std::endl;

  // bool isProbingPreviously = not sharedMemory->isProberInWaitRoom(proberid);
  // sharedMemory->setStatus(proberid,flowid,MRProbing);
  // if (not isProbingPreviously) {
  // if (sharedMemory->isProberInWaitRoom(proberid)) sharedMemory->removeFromProberInWaitRoom(proberid);
  sharedMemory->allocateBufferSpaceSimple(proberid, startProbeBuffer);
  if (pawMode.compare("paw")==0) {
    same_thpt_minbuffer_mp[proberid] = configbuffer;
    // thpt_minbuffer_mp[proberid] = configbuffer;
    can_remove_start_mp[proberid] = true; // other initializations can just take the default values
    design_min_minq_mp[proberid] = configbuffer;
    // probeMinMonitorLongInvoke(proberid, bmType);
    probeMinMonitorLongInvoke2(proberid, 1);
  } else if (pawMode.compare("pa")==0) {
    // probeMinMonitorLongInvoke(proberid, bmType);
    probeMinMonitorLongInvoke2(proberid, 1);
  } else if (pawMode.compare("aw")==0) {
    
  } else if (pawMode.compare("fixed")==0) {
    
  } else if (pawMode.compare("p")==0) {
    same_thpt_minbuffer_mp[proberid] = configbuffer;
    // thpt_minbuffer_mp[proberid] = configbuffer;
    can_remove_start_mp[proberid] = true; // other initializations can just take the default values
    design_min_minq_mp[proberid] = configbuffer;
    // probeMinMonitorLongInvoke(proberid, bmType);
    probeMinMonitorLongInvoke2(proberid, 1);
  }
  // }

  // bool shouldStartProbing = true;
  // std::map<uint32_t,uint32_t>::iterator it;
  // for (it=sharedMemory->statusTracker[proberid].begin(); it!=sharedMemory->statusTracker[proberid].end(); it++) {
  //   if (it->second==HREntering or it->second==HRFlowEnd) std::cout << "ERROR: startProbing, flow status is either HREntering or HRFlowEnd" << std::endl;
  //   if (it->second == MRWaitRoom) {
  //     shouldStartProbing = false;
  //     break;
  //   }
  // }
  // if (shouldStartProbing) {
  //   // sharedMemory->removeFromProberInWaitRoom(proberid);
  //   probeMinMonitorLongInvoke(proberid);
  // }
}

void GenQueueDisc::checkBurstOrLong(uint32_t priority, uint32_t flowid, uint32_t bmType) {
  uint32_t proberId = sharedMemory->getProberId(portId, priority);
  if (verbose) std::cout << Simulator::Now() << ": checkBurstOrLong, proberId=" << proberId << ", flowid=" << flowid;
  // if ((not sharedMemory->isProberInHeadRoom(proberId)) or (sharedMemory->isFlowIdByProberIdEnded(flowid, proberId))) {
  // if (sharedMemory->getStatus(proberId,flowid)==HRFlowEnd or sharedMemory->getStatus(proberId,flowid)==HRClearPackets) {
  //   if (verbose) std::cout << ", flow has ended" << std::endl;
  //   return;
  // }
  if (verbose) std::cout << std::endl;

  // sharedMemory->addToFlowIdByProberIdEnded(flowid,proberId); // AnnC: after this, the next packet should be able to automatically correct the queueid tag itself in DoEnqueue()
  // sharedMemory->setStatus(proberId,flowid,HRFlowEnd);
  // sharedMemory->adjustHeadRoomForFlowIdEnded(proberId); // AnnC: do this explicitly here so that the allocateBufferSpace this round can already recognize
  sharedMemory->removeFromProberInHeadRoom(proberId);
  // sharedMemory->setMinBufferThreshold(proberId, 0); 
  // uint32_t MRqueueid = flowidMRqueueidMapping[flowid]; 
  // uint32_t MRproberid = sharedMemory->getProberId(portId, MRqueueid);
  sharedMemory->addToProberInWaitRoom(proberId);
  // uint32_t BDP = uint32_t(RTTms * portBW/mainRoomNumQueues/ 8.0 *1000000); // ignore the queue & bandwidth taken by short control packets of priority 0
  // if (verbose) std::cout << Simulator::Now() << ": portid=" << portId << ", proberid=" << proberId << ", BDP=" << BDP << std::endl;
  // setUpMainRoomProber(proberId, flowid, BDP);
  // if (verbose) std::cout << Simulator::Now() << ": MRqueueid=" << MRqueueid << ", MRproberid=" << MRproberid << ", BDP=" << BDP << std::endl;
  // setUpMainRoomProber(MRproberid, flowid, BDP);
  // sharedMemory->allocateBufferSpace(newProberId, 0.5*BDP);
  // probeMinBufferSetUp(priority);
  // probeMinMonitorLongInvoke(proberId);
  // Simulator::Schedule(MilliSeconds(waitNRTT*RTTms), &GenQueueDisc::startProbing, this, proberId, flowid);
  // Simulator::Schedule(MilliSeconds(temporaryWaitMs), &GenQueueDisc::startProbing, this, proberId, flowid);
  // Simulator::Schedule(MilliSeconds(waitNRTT*RTTms), &GenQueueDisc::startProbing, this, MRproberid, flowid);
  startProbing(proberId,flowid, bmType);
}

void GenQueueDisc::setUpHeadRoomNonProber(uint32_t priority, uint32_t flowid, uint32_t bmType) {
  uint32_t proberId = sharedMemory->getProberId(portId, priority);
  if (verbose) std::cout << Simulator::Now() << ": setUpHeadRoomNonProber, proberId=" << proberId << std::endl;
  // sharedMemory->setMinBufferThreshold(proberId, 1); // AnnC: MBT for HRprobers == 0 or 1
  sharedMemory->addToProberInHeadRoom(proberId);
  // sharedMemory->setCurrMaxSizeAllowed(proberId, 1);
  if (sharedMemory->getCurrMaxSizeAllowed(proberId) <= 1500) sharedMemory->allocateBufferSpaceSimple(proberId, startProbeBuffer);

  // Simulator::Schedule(MilliSeconds(burstNRTT*RTTms), &GenQueueDisc::checkBurstOrLong, this, priority, flowid);
  checkBurstOrLong(priority,flowid, bmType);
}

void GenQueueDisc::setUpMainRoomProber(uint32_t proberId, uint32_t flowid, uint32_t BDP) {
  uint32_t queueid = proberId % nPrior;
  if (verbose) std::cout << Simulator::Now() << ": setUpMainRoomProber, proberId=" << proberId << ", queueid=" << queueid << ", flowid=" << flowid << std::endl;

  // sharedMemory->setMinBufferThreshold(proberId, 1);
  // if (sharedMemory->isFlowIdByProberIdEnded(flowid,proberId)) sharedMemory->removeFromFlowIdByProberIdEnded(flowid,proberId); // AnnC: in case of HR-FQ MR-FQ
  // sharedMemory->setStatus(proberId,flowid,MRWaitRoom);
  // if (sharedMemory->getCurrMaxSizeAllowed(proberId) == 0) sharedMemory->allocateBufferSpaceSimple(proberId, 0.5*BDP);
  // sharedMemory->allocateBufferSpaceSimple(proberId, 0.5*BDP);
  // if (sharedMemory->getCurrMaxSizeAllowed(proberId) <= 1500) sharedMemory->allocateBufferSpaceSimple(proberId, startProbeBuffer-1500);

  // bool shouldResetProbingStats = true;
  // std::map<uint32_t,uint32_t>::iterator it;
  // for (it=sharedMemory->statusTracker[proberId].begin(); it!=sharedMemory->statusTracker[proberId].end(); it++) {
  //   if (it->second==HREntering or it->second==HRFlowEnd) std::cout << "ERROR: setUpMainRoomProber, flow status is either HREntering or HRFlowEnd" << std::endl;
  //   if (it->second == MRProbing) {
  //     shouldResetProbingStats = false;
  //     break;
  //   }
  // }

  // if (shouldResetProbingStats) {
  //   idealMinBufferProbeCount[queueid] = 0;
  //   maxBufferUnchangingCount[queueid] = 0;
  //   probeMinBufferLastChecked[queueid] = 0;
  //   sharedMemory->probeMinAverageThroughput[proberId] = 0;
  //   sharedMemory->probeMinTotalDropBytes[proberId] = 0;
  //   sharedMemory->probeMinPacketCountZeroQueue[proberId] = 0;
  //   sharedMemory->probeMinPacketCountTotal[proberId] = 0;
  //   sharedMemory->probeMinDurationZeroQueue[proberId] = 0;
  //   std::map<int64_t,uint32_t>::iterator it1;
  //   for (it1=sharedMemory->probeMinTotalDropBytesMonitorMap[proberId].begin(); it1!=sharedMemory->probeMinTotalDropBytesMonitorMap[proberId].end(); it1++) {
  //       it1->second = 0;
  //   }
  //   for (it1=sharedMemory->probeMinMaxBufferUsedMonitorMap[proberId].begin(); it1!=sharedMemory->probeMinMaxBufferUsedMonitorMap[proberId].end(); it1++) {
  //       it1->second = 0;
  //   }
  //   for (it1=sharedMemory->probeMinMinBufferUsedMonitorMap[proberId].begin(); it1!=sharedMemory->probeMinMinBufferUsedMonitorMap[proberId].end(); it1++) {
  //       it1->second = 0;
  //   }
  //   std::map<int64_t,int64_t>::iterator it2;
  //   for (it2=sharedMemory->probeMinDurationZeroQueueMonitorMap[proberId].begin(); it2!=sharedMemory->probeMinDurationZeroQueueMonitorMap[proberId].end(); it2++) {
  //       it2->second = 0;
  //   }
  // }
}

void GenQueueDisc::setProbingStats(uint32_t _startProbeBuffer, uint16_t _monitorLongMs, double _dropRateThreshold, double _adaptiveIncreaseParameter, double _adaptiveDecreaseParameter, uint32_t _smoothQlenCollectionByUs, uint32_t _smoothWindowByNumData, uint32_t _smoothOutlierThresholdByMultiple, std::string _pawMode) {
  startProbeBuffer = _startProbeBuffer;
  // ssthreshBuffer = _startProbeBuffer;
  monitorlongms = _monitorLongMs;
  dropRateThreshold = _dropRateThreshold;
  adaptiveIncreaseParameter = _adaptiveIncreaseParameter;
  adaptiveDecreaseParameter = _adaptiveDecreaseParameter;
  smoothQlenCollectionByUs = _smoothQlenCollectionByUs;
  smoothWindowByNumData = _smoothWindowByNumData;
  smoothOutlierThresholdByMultiple = _smoothOutlierThresholdByMultiple;
  pawMode = _pawMode;
}

void GenQueueDisc::setUpTrackingStats(uint32_t numqueues) {
  for (uint32_t i=0; i<numqueues; i++) {
    // idealMinBufferProbeCount.push_back(0);
    // maxBufferUnchangingCount.push_back(0);
    prevMaxBufferUsed.push_back(0);
    prevDropRate.push_back(1);
    // probeMinBufferLastChecked.push_back(0);
    // zeroDropCount.push_back(0);
    latestLongCollect.push_back(0);
    std::deque<uint32_t> record;
    smoothQlenRecord.push_back(record);
    isWindowOn.push_back(false);
  }
  // nextAvailableHRqueueid = mainRoomNumQueues+1;
  smoothStartMonitoring(numqueues);
}

void GenQueueDisc::smoothStartMonitoring(uint32_t numqueues) {
  // monitor all queues besides the priority 0 queue
  for (uint32_t p=1; p<numqueues; p++) {
    uint32_t qlen = GetQueueDiscClass (p)->GetQueueDisc ()->GetNBytes();
    smoothQlenRecord[p].push_back(qlen);
    if (smoothQlenRecord[p].size()>smoothWindowByNumData) {
      smoothQlenRecord[p].pop_front();
    }
  }
  Simulator::Schedule(MicroSeconds(smoothQlenCollectionByUs), &GenQueueDisc::smoothStartMonitoring, this, numqueues);
}

double GenQueueDisc::smoothGetAverageQlen(uint32_t p) {
  uint64_t sum = 0;
  for (uint32_t i=0; i<smoothQlenRecord[p].size(); i++) {
    sum += smoothQlenRecord[p][i];
  }
  double average = sum/(double)smoothQlenRecord[p].size();
  uint64_t mysum = sum;
  sum = 0;
  uint32_t count = 0;
  for (uint32_t i=0; i<smoothQlenRecord[p].size(); i++) {
    uint32_t qlen = smoothQlenRecord[p][i];
    // if (qlen<=average*smoothOutlierThresholdByMultiple) {
    // if (qlen<=average+smoothOutlierThresholdByMultiple) {
    if (qlen<=average) {
      sum += qlen;
      count += 1;
    }
  }
  double weighted_average = sum/(double)count;
  // int64_t micronow = Simulator::Now().GetMicroSeconds();
  // if (65000000 < micronow && micronow < 75000000) std::cout << "SMOOTH," << micronow << "," << p << "," << average << "," << weighted_average << "," << smoothQlenRecord[p].size() << "," << mysum << std::endl;
  if (pawMode.compare("paw")==0) {
    return weighted_average;
  } else if (pawMode.compare("pa")==0) {
    return average;
  } else if (pawMode.compare("aw")==0) {
    return weighted_average;
  } else if (pawMode.compare("fixed")==0) {
    return average;
  } else if (pawMode.compare("p")==0) {
    return average;
  }
  return weighted_average;
}

void GenQueueDisc::setParameters(
  uint16_t ParHistLen,
  uint16_t ParRemoveStartLen,
  uint16_t ParRemoveStartThres,
  uint16_t ParExploreThres,
  uint16_t ParSafeThres,
  uint16_t ParConsecIncreaseThres,
  uint16_t ParStepIncreaseCap,
  uint16_t ParIncreaseRatio,
  uint16_t ParConsecDecreaseThres,
  uint16_t ParStepDecreaseCap,
  uint16_t ParDecreaseRatio,
  uint16_t ParMinQOutlier,
  uint16_t ParMinQHold
) {
  HistLen = ParHistLen;
  RemoveStartLen = ParRemoveStartLen;
  RemoveStartThres = ParRemoveStartThres;
  ExploreThres = ParExploreThres;
  SafeThres = ParSafeThres;
  ConsecIncreaseThres = ParConsecIncreaseThres;
  StepIncreaseCap = ParStepIncreaseCap;
  IncreaseRatio = ParIncreaseRatio;
  ConsecDecreaseThres = ParConsecDecreaseThres;
  StepDecreaseCap = ParStepDecreaseCap;
  DecreaseRatio = ParDecreaseRatio;
  MinQOutlier = ParMinQOutlier;
  MinQHold = ParMinQHold;
}

void GenQueueDisc::insertIntoFixedVaryThresVec(uint32_t key, uint32_t value) {
  fixedVaryThresVec.emplace_back(key,value);
}

void GenQueueDisc::startWindowAfterDrop(uint32_t queueid) {
  uint32_t proberid = sharedMemory->getProberId(portId, queueid);
  // uint32_t queueid = proberid % nPrior;
  if (verbose) std::cout << Simulator::Now() << ": startWindowAfterDrop, proberid=" << proberid << ", queueid=" << queueid << std::endl;
  
  uint16_t WINDOW_MS = monitorlongms;
  if (!isWindowOn[queueid]) {
    if (verbose) std::cout << Simulator::Now() << ",start window" << std::endl;
    isWindowOn[queueid] = true;

    sharedMemory->probeMinMaxBufferUsed[proberid] = 0;
    sharedMemory->probeMinTotalDropBytes[proberid] = 0;
    sharedMemory->probeMinAverageThroughput[proberid] = 0;
    sharedMemory->probeMinMinBufferUsed[proberid] = 1000000000;
    sharedMemory->designZeroWindowSum[proberid] = 0;
    int64_t now = Simulator::Now().GetMicroSeconds();
    sharedMemory->designZeroWindowStart[proberid] = now;

    Simulator::Schedule(MilliSeconds(WINDOW_MS), &GenQueueDisc::endWindowAfterDrop, this, queueid, WINDOW_MS, 1);
  } else {
    // pass
  }
}

void GenQueueDisc::endWindowAfterDrop(uint32_t queueid, uint32_t window, uint8_t count) {
  uint32_t proberid = sharedMemory->getProberId(portId, queueid);
  uint32_t maxbuffer = sharedMemory->probeMinMaxBufferUsed[proberid];
  uint32_t drop = sharedMemory->probeMinTotalDropBytes[proberid];
  uint32_t sent = sharedMemory->probeMinAverageThroughput[proberid];
  uint32_t minbuffer = sharedMemory->probeMinMinBufferUsed[proberid];
  int64_t zeroqueueduration = sharedMemory->designZeroWindowSum[proberid]; // in ns
  int64_t zerostart = sharedMemory->designZeroStart[proberid];
  uint32_t cmsa = sharedMemory->getCurrMaxSizeAllowed(proberid);
  double droprate = drop/(double)sent;
  if (verbose) std::cout << Simulator::Now() << ": endWindowAfterDrop, proberId=" << proberid << ", queueuid=" << queueid;
  if (verbose) std::cout << ", drop=" << drop << ", maxbuffer=" << maxbuffer << ", sent=" << sent << ", minbuffer=" << minbuffer << ", cmsa=" << cmsa << ", zeroqueueduration=" << zeroqueueduration << std::endl;

  bool design_mar2625_v0 = false;
  bool design_mar2625_v1 = false;
  bool design_mar2625_v2 = false;
  bool is_log = false;

  if (mainRoomNumQueues==10) {
    design_mar2625_v0 = true;
  } else if (mainRoomNumQueues==11) {
    design_mar2625_v1 = true;
  } else if (mainRoomNumQueues==12) {
    design_mar2625_v2 = true;
  }

  double MTU = 1500;
  uint8_t SAFE_THRES = 2;
  uint8_t SSTHRESH_MULTIPLIER = ExploreThres;
  uint8_t DEC_RATIO = DecreaseRatio;
  double INC_RATIO = IncreaseRatio/10.0;
  uint8_t COUNT_THRES = 2;
  if (design_mar2625_v2) {
    if (zeroqueueduration > 0) {
      ssthreshBuffer = cmsa * SSTHRESH_MULTIPLIER;
      if (is_log) std::cout << Simulator::Now() << ",thres+=" << (uint32_t)(((double)INC_RATIO*window*1000000/(window*1000000-zeroqueueduration))*cmsa-cmsa) << ",ssthreshBuffer=" << ssthreshBuffer << std::endl;
      sharedMemory->allocateBufferSpaceSimple(proberid,(uint32_t)(((double)INC_RATIO*window*1000000/(window*1000000-zeroqueueduration))*cmsa-cmsa));
    } else {
      if (minbuffer > SAFE_THRES*MTU) {
        if (count < COUNT_THRES) {
          if (is_log) std::cout << Simulator::Now() << ",one more monitoring interval" << std::endl;
          Simulator::Schedule(MilliSeconds(window), &GenQueueDisc::endWindowAfterDrop, this, queueid, window, count+1);
          return;
        } else {
          if (cmsa > ssthreshBuffer) {
            uint32_t thres_change = (uint32_t)(cmsa-(cmsa+ssthreshBuffer)/2.0);
            thres_change = (uint32_t)((thres_change-1)/(double)MTU + 1)*MTU;
            if (is_log) std::cout << Simulator::Now() << ",thres-=" << thres_change << ",ssthreshBuffer=" << ssthreshBuffer << std::endl;
            if (cmsa-thres_change > SAFE_THRES*MTU) sharedMemory->allocateBufferSpaceSimple(proberid,-thres_change);
          } else {
            if (is_log) std::cout << Simulator::Now() << ",thres-=" << DEC_RATIO*MTU*count << ",ssthreshBuffer=" << ssthreshBuffer << std::endl;
            if (cmsa-MTU*count > SAFE_THRES*MTU) sharedMemory->allocateBufferSpaceSimple(proberid,-DEC_RATIO*MTU*count);
          }
        }
      }
    }
  }

  if (design_mar2625_v1) {
    if (zeroqueueduration > 0) {
      if (is_log) std::cout << "thres += " << (((double)window*count*1000000/(window*count*1000000-zeroqueueduration))*cmsa-cmsa) << std::endl;
      sharedMemory->allocateBufferSpaceSimple(proberid,(uint32_t)(((double)window*count*1000000/(window*count*1000000-zeroqueueduration))*cmsa-cmsa));
    } else {
      if (minbuffer > SAFE_THRES*MTU) {
        if (count < COUNT_THRES) {
          Simulator::Schedule(MilliSeconds(window), &GenQueueDisc::endWindowAfterDrop, this, queueid, window, count+1);
          return;
        } else {
          if (is_log) std::cout << "thres -= " << MTU*count << std::endl;
          if (cmsa-MTU*count > SAFE_THRES*MTU) sharedMemory->allocateBufferSpaceSimple(proberid,-MTU*count);
        }
      }
    }
  }

  if (design_mar2625_v0) {
    if (zeroqueueduration > 0) {
      if (is_log) std::cout << "thres += " << (((double)window*1000000/(window*1000000-zeroqueueduration))*cmsa-cmsa) << std::endl;
      sharedMemory->allocateBufferSpaceSimple(proberid,(uint32_t)(((double)window*1000000/(window*1000000-zeroqueueduration))*cmsa-cmsa));
    } else {
      if (minbuffer > SAFE_THRES*MTU) {
        if (is_log) std::cout << "thres -= " << MTU << std::endl;
        if (cmsa-MTU > SAFE_THRES*MTU) sharedMemory->allocateBufferSpaceSimple(proberid,-MTU);
      }
    }
  }

  isWindowOn[queueid] = false;
}

// void GenQueueDisc::probeMinMonitorDropCollect(uint32_t proberid, int64_t key, uint32_t qSize) {
//   if (verbose) std::cout << Simulator::Now() << ", probeMinMonitorDropCollect, proberId=" << proberid << ", key=" << key << ", qSize=" << qSize << std::endl;
//   // int64_t duration = sharedMemory->probeMinDurationZeroQueue[proberid];
//   // uint32_t minbuffer = sharedMemory->probeMinMinBufferUsed[proberid];
//   int64_t duration = sharedMemory->probeMinDurationZeroQueueMonitorMap[proberid][key];
//   uint32_t minbuffer = sharedMemory->probeMinMinBufferUsedMonitorMap[proberid][key];
//   sharedMemory->probeMinDurationZeroQueueMonitorMap[proberid].erase(key);
//   sharedMemory->probeMinMinBufferUsedMonitorMap[proberid].erase(key);

//   int64_t now = Simulator::Now().GetMicroSeconds();
//   int64_t CMSALastChanged = sharedMemory->getCurrMaxSizeAllowedLastChanged(proberid);
//   if (now - CMSALastChanged < RTTms*monitordropNRTT*1000) return; // larger is ok, means cmsa unchanged before and after the drop
//   if (sharedMemory->hasInstanceInBetweenProbeMinDurationZeroQueueMonitorMap(proberid,key,now)) return;
//   if (minbuffer == 100000000) return;
//   if (sharedMemory->isProberInWaitRoom(proberid)) return;
//   // if (not sharedMemory->isProberActiveInMainRoom(proberid)) return;
//   // if (sharedMemory->isProberStaticInMainRoom(proberid)) return;
  
//   sharedMemory->setDropDurationZeroQueueData(proberid, qSize,duration/(double)(now-key));
  
//   uint32_t absoluteMinBuffer = sharedMemory->getAbsoluteMinBuffer();
//   uint32_t currMinBufferThreshold = sharedMemory->getMinBufferThreshold(proberid);
//   uint32_t cmsa = sharedMemory->getCurrMaxSizeAllowed(proberid);
//   if (verbose) std::cout << Simulator::Now() << ", probeMinMonitorDropCollect, valid instance, proberId=" << proberid << ", duration=" << duration << ", minbuffer=" << minbuffer << ", cmsa=" << cmsa << std::endl;
//   if (minbuffer < absoluteMinBuffer) {
//     if (currMinBufferThreshold <= qSize) sharedMemory->setMinBufferThreshold(proberid,1);
//     if (qSize+1500 > cmsa) sharedMemory->allocateBufferSpaceSimple(proberid, 1500);
//   } else if (minbuffer > absoluteMinBuffer) {
//     if (now-CMSALastChanged < monitorlongNRTT*sharedMemory->smallestRTTms) sharedMemory->allocateBufferSpaceSimple(proberid, -1500); // AnnC: do not move too fast
//   } else {
//     sharedMemory->setMinBufferThreshold(proberid,qSize);
//   }

//   // if (minbuffer == absoluteMinBuffer) {
//   //   sharedMemory->updateIdealMinBufferUsedData(proberid,minbuffer,now,cmsa);
//   // } else if (minbuffer < absoluteMinBuffer) {
//   //   sharedMemory->updateIdealMinBufferUsedData(proberid,minbuffer,now,cmsa);
//   //   sharedMemory->allocateBufferSpace(proberid, 1500);
//   //   probeMinMonitorLongInvoke(proberid);
//   // } else { // minbuffer > absoluteMinBuffer
//   //   sharedMemory->allocateBufferSpace(proberid, -1500);
//   //   probeMinMonitorLongInvoke(proberid);
//   // }

//   // if (minbuffer == sharedMemory->getAbsoluteMinBuffer()) {
//   //   if (duration > 0) std::cout << "ERROR: " << Simulator::Now() << ", proberid=" << proberid << ", minbuffer=" << minbuffer << ", durationzeroqueue=" << duration << std::endl;
//   //   sharedMemory->setMinBufferThreshold(proberid,cmsa);
//   //   if (verbose) std::cout << Simulator::Now() << ": setMinBufferThreshold, proberId=" << proberid << ", minBufferThreshold=" << cmsa << std::endl;
//   // } else {
//   //   if (duration == 0) { // i.e. minbuffer > absoluteMinBuffer
//   //     sharedMemory->allocateBufferSpace(proberid, -1500);
//   //   } else { // i.e. minbuffer < absoluteMinBuffer
//   //     sharedMemory->allocateBufferSpace(proberid, 1500);
//   //   }
//   //   probeMinMonitorLongInvoke(proberid);
//   // }
// }

// void GenQueueDisc::probeMinMonitorDropInvoke(uint32_t proberid, uint32_t qSize) {
//   int64_t now = Simulator::Now().GetMicroSeconds();
//   if (verbose) std::cout << Simulator::Now() << ", probeMinMonitorDropInvoke, proberId=" << proberid << ", key=" << now << ", qSize=" << qSize << std::endl;
//   // sharedMemory->probeMinDurationZeroQueue[proberid] = 0;
//   // sharedMemory->probeMinMinBufferUsed[proberid] = 100000000;

//   sharedMemory->probeMinDurationZeroQueueMonitorMap[proberid].insert( std::pair<int64_t,uint32_t>(now,0) );
//   sharedMemory->probeMinMinBufferUsedMonitorMap[proberid].insert( std::pair<int64_t,int64_t>(now,100000000) );

//   Simulator::Schedule(MilliSeconds(RTTms*monitordropNRTT), &GenQueueDisc::probeMinMonitorDropCollect, this, proberid, now, qSize);
// }

// void GenQueueDisc::probeMinMonitorLongCollect(uint32_t proberid) {
//   uint32_t queueid = proberid % nPrior;
//   if (verbose) std::cout << Simulator::Now() << ": probeMinMonitorLongCollect, proberId=" << proberid << ", queueuid=" << queueid << std::endl;
//   int64_t now = Simulator::Now().GetMicroSeconds();
//   int64_t CMSALastChanged = sharedMemory->getCurrMaxSizeAllowedLastChanged(proberid);
//   // if ((now - CMSALastChanged < RTTms*monitorlongNRTT*1000) or (sharedMemory->hasInstanceInBetweenProbeMinDurationZeroQueueMonitorMap(proberid,now-RTTms*monitorlongNRTT*1000,now))) {
//   if (now - CMSALastChanged < sharedMemory->smallestRTTms*monitorlongNRTT*1000) {
//     probeMinMonitorLongInvoke(proberid);
//     return;
//   }

//   uint32_t drop = sharedMemory->probeMinTotalDropBytes[proberid];
//   uint32_t maxbuffer = sharedMemory->probeMinMaxBufferUsed[proberid];
//   uint32_t sent = sharedMemory->probeMinAverageThroughput[proberid];
//   uint32_t minbuffer = sharedMemory->probeMinMinBufferUsed[proberid];
//   int64_t duration = sharedMemory->probeMinDurationZeroQueue[proberid];
//   if (minbuffer == 100000000) {
//     probeMinMonitorLongInvoke(proberid);
//     return;
//   }

//   if (sharedMemory->isProberInWaitRoom(proberid)) return;
//   // if (not sharedMemory->isProberActiveInMainRoom(proberid)) return;
//   // if (sharedMemory->isProberStaticInMainRoom(proberid)) return;

//   // uint32_t drop = sharedMemory->probeMinTotalDropBytesMonitorMap[proberid][key];
//   // uint32_t maxbuffer = sharedMemory->probeMinMaxBufferUsedMonitorMap[proberid][key];
//   // sharedMemory->probeMinTotalDropBytesMonitorMap[proberid].erase(key);
//   // sharedMemory->probeMinMaxBufferUsedMonitorMap[proberid].erase(key);
//   uint32_t cmsa = sharedMemory->getCurrMaxSizeAllowed(proberid);
//   uint32_t prevmaxbuffer = prevMaxBufferUsed[queueid];
//   prevMaxBufferUsed[queueid] = maxbuffer;
//   if (verbose) std::cout << Simulator::Now() << ": probeMinMonitorLongCollect, valid instance, proberId=" << proberid << ", drop=" << drop << ", maxbuffer=" << maxbuffer << ", sent=" << sent << ", minbuffer=" << minbuffer << ", duration=" << duration << ", cmsa=" << cmsa << ", prevmaxbuffer=" << prevmaxbuffer << std::endl;
  
//   double value = (double)duration/(sharedMemory->smallestRTTms*monitorlongNRTT*1000);
//   sharedMemory->setLongDurationZeroQueueData(proberid,maxbuffer,value);

//   if (drop == 0) {
//     if (prevmaxbuffer>=maxbuffer-1500 and prevmaxbuffer<=maxbuffer+1500) {
//       zeroDropCount[queueid]++;
//       if (zeroDropCount[queueid]>=4) sharedMemory->setDoMonitorDrop(proberid, false);
//     } else {
//       zeroDropCount[queueid] = 0;
//     }
//   } else {
//     zeroDropCount[queueid] = 0;
//     double droprate = (double)drop / sent;
//     if (verbose) std::cout << Simulator::Now() << ": proberid=" << proberid << ", droprate=" << droprate << std::endl;
//     if (droprate > goodDroprate) sharedMemory->setDoMonitorDrop(proberid, false);
//   }
//   if (verbose) std::cout << Simulator::Now() << ": proberid=" << proberid << ", zeroDropCount=" << zeroDropCount[queueid] << std::endl;

//   if (maxbuffer+1500<=cmsa and value>= 0.9) {
//     if (verbose) std::cout << Simulator::Now() << ": sending rate too slow, ask for more buffer" << std::endl;
//     sharedMemory->allocateBufferSpaceSimple(proberid, 3000); // AnnC: sender is sending tooo slow
//   }

//   if (not sharedMemory->getDoMonitorDrop(proberid)) {
//     uint32_t absoluteMinBuffer = sharedMemory->getAbsoluteMinBuffer();
//     uint32_t currMinBufferThreshold = sharedMemory->getMinBufferThreshold(proberid);
//     if (minbuffer < absoluteMinBuffer) {
//       if (currMinBufferThreshold <= maxbuffer) sharedMemory->setMinBufferThreshold(proberid,1);
//       if (drop > 0 and maxbuffer+1500 > cmsa) sharedMemory->allocateBufferSpace(proberid, 1500);
//     } else if (minbuffer > absoluteMinBuffer) {
//       sharedMemory->allocateBufferSpaceSimple(proberid, -1500);
//     } else {
//       sharedMemory->setMinBufferThreshold(proberid,maxbuffer);
//       sharedMemory->setCurrMaxSizeAllowed(proberid, maxbuffer);
//       sharedMemory->checkChangeInCurrMaxSizeAllowed(proberid,cmsa,maxbuffer);
//     } 
//   }

//   // if (duration == 0 and drop == 0) {
//   //   zeroDropCount[queueid]++;
//   //   if (zeroDropCount[queueid] == 5) {
//   //     sharedMemory->setMinBufferThreshold(proberid,maxbuffer);
//   //     sharedMemory->setCurrMaxSizeAllowed(proberid, maxbuffer);
//   //     sharedMemory->checkChangeInCurrMaxSizeAllowed(proberid,cmsa,maxbuffer);
//   //   }
//   // } else {
//   //   zeroDropCount[queueid] = 0;
//   //   double droprate = (double)drop / sent;
//   //   if (verbose) std::cout << Simulator::Now() << ": proberid=" << proberid << ", droprate=" << droprate << std::endl;
//   //   if (droprate > goodDroprate) sharedMemory->setDoMonitorDrop(proberid, false);
//   // }

//   // if (not sharedMemory->getDoMonitorDrop(proberid)) {
//   //   uint32_t absoluteMinBuffer = sharedMemory->getAbsoluteMinBuffer();
//   //   uint32_t currMinBufferThreshold = sharedMemory->getMinBufferThreshold(proberid);
//   //   if (minbuffer < absoluteMinBuffer) {
//   //     if (currMinBufferThreshold <= maxbuffer) sharedMemory->setMinBufferThreshold(proberid,1);
//   //     if (drop > 0) sharedMemory->allocateBufferSpace(proberid, 1500);
//   //   } else if (minbuffer > absoluteMinBuffer) {
//   //     sharedMemory->allocateBufferSpace(proberid, -1500);
//   //   } else {
//   //     sharedMemory->setMinBufferThreshold(proberid,maxbuffer);
//   //     sharedMemory->setCurrMaxSizeAllowed(proberid, maxbuffer);
//   //     sharedMemory->checkChangeInCurrMaxSizeAllowed(proberid,cmsa,maxbuffer);
//   //   } 
//   // }

//   probeMinMonitorLongInvoke(proberid);
// }

// bool GenQueueDisc::MyBM(uint32_t priority, Ptr<Packet> packet){
//   // handle control packets (priority 0)
//   if (priority == 0) { 
//     if (sharedMemory->GetRemainingBuffer() < packet->GetSize()){
//       return false;
//     }
//     return true;
//   }

//   /* Find flow-id if exists */
//   bool found;
//   uint32_t flowId = 0;
//   FlowIdTag tag;
//   found = packet->PeekPacketTag (tag);
//   if(found){
//     flowId=tag.GetFlowId();
//   } else {
//     std::cout << "flowId not found" << std::endl;
//   }

//   uint32_t proberId = sharedMemory->getProberId(portId, priority);
//   if (sharedMemory->isNewFlow(flowId)) {
//     setUpNewFlow(priority);
//   }
//   uint32_t maxSize = sharedMemory->getCurrMaxSizeAllowed(portId, priority);

//   int64_t now = Simulator::Now().GetNanoSeconds();
//   if (!sharedMemory->getBufferSizeLock(portId, priority)) {
//     if (sharedMemory->isProberInProberDoneMinBuffer(proberId) and !sharedMemory->isKeyInThroughputData(proberId,maxSize)) {
//       sharedMemory->setBufferSizeLock(proberId, true);
//       sharedMemory->setBufferSizeLockStart(proberId, uint64_t(now));
//       sharedMemory->probeMinAverageThroughput[proberId] = 0;
//       sharedMemory->probeMinTotalDropBytes[proberId] = 0;
//     }
//   } else {
//     uint64_t lockStart = sharedMemory->getBufferSizeLockStart(proberId);
//     uint64_t duration = now - lockStart;
//     // std::cout << "debug: now=" << now << ", lockStart=" << sharedMemory->getBufferSizeLockStart(proberId) << ", duration=" << duration << std::endl;
//     if (lockStart != 0 and duration >= sharedMemory->aggregateNRTT*RTTns) {
//       uint64_t totaldropbytes = sharedMemory->probeMinTotalDropBytes[proberId];
//       uint64_t totalsentbytes = sharedMemory->probeMinAverageThroughput[proberId];
//       double droprate = totaldropbytes / (double)totalsentbytes;
//       double throughput = 8.0*totalsentbytes/duration/(portBW/(nPrior-1));
//       sharedMemory->probeMinTotalDropBytes[proberId] = 0;
//       sharedMemory->probeMinAverageThroughput[proberId] = 0;
//       sharedMemory->setThroughputData(proberId,maxSize,throughput);
//       sharedMemory->setBufferSizeLock(proberId, false);
//       sharedMemory->setBufferSizeLockStart(proberId, 0);
//     }
//   }

//   uint32_t qSize = GetQueueDiscClass (priority)->GetQueueDisc ()->GetNBytes();
//   if ( ((qSize + packet->GetSize()) >  maxSize) || (sharedMemory->GetRemainingBuffer() < packet->GetSize())  ){
//     return false; // drop
//   }
//   else{
//     return true;
//   }

// }

// void GenQueueDisc::checkBurstOrLong(uint32_t priority) {
//   uint32_t proberId = sharedMemory->getProberId(portId, priority);
//   sharedMemory->removeProberToProberNewerFlows(proberId); // this needs to be before allocatedBasedOnMinBufferThreshold
//   // sharedMemory->setBufferSizeLock(proberId, false); // this needs to be before allocatedBasedOnMinBufferThreshold
//   sharedMemory->allocateBasedOnMinBufferThreshold();
//   sharedMemory->addProberToProberAwaitingMinBuffer(proberId);
// }

// void GenQueueDisc::setUpNewFlow(uint32_t priority) {
//   uint32_t proberId = sharedMemory->getProberId(portId, priority);
//   if (verbose) std::cout << Simulator::Now() << ": setUpNewFlow, proberId=" << proberId << std::endl;
//   sharedMemory->probeMinMaxBufferUsedRecord[proberId] = 0;
//   sharedMemory->probeMinMaxBufferTimes[proberId] = 0;

//   // AnnC: the BDP calculation here is not accurate; need to fix later
//   uint32_t BDP = uint32_t(RTTns * portBW/(nPrior-1) / 8.0); // ignore the queue & bandwidth taken by short control packets of priority 0
//   sharedMemory->setMinBufferThreshold(portId, priority, BDP);
//   sharedMemory->setBurstToleranceThreshold(portId, priority, 0.8*BDP);
//   // sharedMemory->setCurrMaxSizeAllowed(portId, priority, std::min(sharedMemory->getMyRemainingBuffer(), sharedMemory->getMinBufferThreshold(portId, priority)));
//   sharedMemory->setNormalizedPortBW(proberId, portBW/(nPrior-1));
//   // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
//   sharedMemory->setAverageQueueRTT(proberId, RTTns);
//   sharedMemory->addProberToProberNewerFlows(proberId); // this needs to be before allocatedBasedOnMinBufferThreshold
//   // sharedMemory->setBufferSizeLock(proberId, true); // this needs to be before allocatedBasedOnMinBufferThreshold
//   sharedMemory->allocateBasedOnMinBufferThreshold();
//   Simulator::Schedule(NanoSeconds(sharedMemory->getStartupNRTT()*RTTns), &GenQueueDisc::checkBurstOrLong, this, priority);
// }

/**************************** 
 * For COS597K final project
*****************************/

uint32_t dropAggregate = 0;
uint32_t sentAggregate = 0;
void GenQueueDisc::LLM_collect_stats(uint32_t priority) {
  uint32_t proberid = sharedMemory->getProberId(portId, priority);

  dropAggregate = sharedMemory->probeMinTotalDropBytes[proberid];
  sentAggregate = sharedMemory->probeMinAverageThroughput[proberid];

  sharedMemory->probeMinTotalDropBytes[proberid] = 0;
  sharedMemory->probeMinAverageThroughput[proberid] = 0;

  Simulator::Schedule(MilliSeconds(monitorlongms), &GenQueueDisc::LLM_collect_stats, this, priority);
}

bool GenQueueDisc::LLM_DT_COT_0_BM(uint32_t priority, Ptr<Packet> packet) {
  // Get the total remaining buffer on the router
  double remaining = sharedMemory->GetRemainingBuffer();
  // Get the current queue size in bytes of this queue
  uint32_t qSize = GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes();
  // Get the packet size
  uint32_t packetSize = packet->GetSize();

  // Retrieve historical metrics (sent and drop) for the queue over the last interval
  uint64_t sentBytes = sentAggregate; // Aggregated sent bytes
  uint64_t dropBytes = dropAggregate; // Aggregated dropped bytes

  // Calculate a weighted dynamic threshold using alpha, sent, and drop
  double sentWeight = 0.8; // Weight for sent data
  double dropWeight = 1.2; // Weight for drop data
  double stabilityFactor = 0.1; // Prevent rapid oscillations

  // Adjust alpha dynamically based on historical traffic
  double adjustedAlpha = alphas[priority] + (sentBytes * sentWeight - dropBytes * dropWeight) / (sentBytes + dropBytes + 1); 
  adjustedAlpha = std::max(0.1, std::min(adjustedAlpha, 10.0)); // Clamp alpha to a reasonable range

  // Calculate the dynamic threshold
  uint64_t maxSize = adjustedAlpha * remaining * (1.0 - stabilityFactor * (qSize / (remaining + 1.0)));

  int64_t now = Simulator::Now().GetMicroSeconds();
  if (now%1000==0) std::cout << "LLM," << Simulator::Now() << "," << maxSize << std::endl;

  // Decision logic: enqueue or drop
  if (((qSize + packetSize) > maxSize) || (remaining < packetSize)) {
      // Update drop statistics if dropping
      // GetQueueDiscClass(queueid)->IncrementDropBytes(packetSize);
      return false; // Drop
  } else {
      // Update sent statistics if enqueuing
      // GetQueueDiscClass(queueid)->IncrementSentBytes(packetSize);
      return true; // Enqueue
  }
}

bool GenQueueDisc::LLM_DT_COT_1_BM(uint32_t priority, Ptr<Packet> packet) {
  // Retrieve shared buffer state
  double remaining = sharedMemory->GetRemainingBuffer();
  uint32_t qSize = GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes();
  uint32_t packetSize = packet->GetSize();

  // Retrieve raw historical metrics for the queue
  uint64_t sentBytes = sentAggregate; // Total sent bytes in the interval
  uint64_t dropBytes = dropAggregate; // Total dropped bytes in the interval

  // Adjust alpha based on traffic behavior
  double dropImpact = (dropBytes > 0) ? (1.0 + (double)dropBytes / (sentBytes + 1.0)) : 1.0;
  double adjustedAlpha = alphas[priority] / dropImpact; // Penalize higher drop rates
  adjustedAlpha = std::clamp(adjustedAlpha, 0.5, 10.0); // Clamp alpha to reasonable limits

  // Calculate the dynamic threshold
  uint64_t maxSize = adjustedAlpha * remaining;

  int64_t now = Simulator::Now().GetMicroSeconds();
  if (now%1000==0) std::cout << "LLM," << Simulator::Now() << "," << maxSize << std::endl;

  // Decision logic
  if (((qSize + packetSize) > maxSize) || (remaining < packetSize)) {
      // Update drop statistics
      // GetQueueDiscClass(queueid)->IncrementDropBytes(packetSize);
      return false; // Drop
  } else {
      // Update sent statistics
      // GetQueueDiscClass(queueid)->IncrementSentBytes(packetSize);
      return true; // Enqueue
  }
}

bool GenQueueDisc::LLM_DT_COT_2_BM(uint32_t priority, Ptr<Packet> packet) {
  // Get the total remaining buffer on the router
  double remaining = sharedMemory->GetRemainingBuffer();
  uint32_t packetSize = packet->GetSize();

  // Retrieve aggregated traffic statistics
  uint32_t sentBytes = sentAggregate; // Sent bytes in the last interval
  uint32_t droppedBytes = dropAggregate; // Dropped bytes in the last interval
  uint32_t qSize = GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes(); // Current queue size

  // Calculate traffic trend factor: ratio of sent to total (sent + drop)
  double trafficTrend = (sentBytes + droppedBytes > 0) 
      ? static_cast<double>(sentBytes) / (sentBytes + droppedBytes) 
      : 1.0;

  // Dynamically adjust alpha based on traffic trends
  double dynamicAlpha = alphas[priority] * trafficTrend;

  // Calculate the maximum size for this queue
  uint64_t maxSize = dynamicAlpha * remaining;
  int64_t now = Simulator::Now().GetMicroSeconds();
  if (now%1000==0) std::cout << "LLM," << Simulator::Now() << "," << maxSize << std::endl;

  // If current queue size + packet size exceeds threshold, or no space for packet, drop it
  if ((qSize + packetSize > maxSize) || (remaining < packetSize)) {
      return false; // Drop the packet
  } else {
      return true; // Enqueue the packet
  }
}

bool GenQueueDisc::LLM_DT_BRDG_0_BM(uint32_t priority, Ptr<Packet> packet) {
  // Get the remaining buffer
  double remaining = sharedMemory->GetRemainingBuffer();

  // Calculate dynamic threshold adjustment based on recent drop-to-sent ratio
  double dropSentRatio = (sentAggregate > 0) ? (double)dropAggregate / sentAggregate : 0.0;

  // Adjust alpha dynamically to penalize buffer-hungry queues
  double adjustedAlpha = alphas[priority] * (1.0 - dropSentRatio);

  // Calculate queue threshold
  uint64_t maxSize = adjustedAlpha * remaining;
  int64_t now = Simulator::Now().GetMicroSeconds();
  if (now%1000==0) std::cout << "LLM," << Simulator::Now() << "," << maxSize << std::endl;

  // Get current queue size
  uint32_t qSize = GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes();

  // Check if enqueue is possible
  uint32_t packetSize = packet->GetSize();
  if ((qSize + packetSize > maxSize) || (remaining < packetSize)) {
      return false; // Drop
  } else {
      return true; // Enqueue
  }
}

bool GenQueueDisc::LLM_DT_BRDG_1_BM(uint32_t priority, Ptr<Packet> packet) {
  // Retrieve the remaining buffer capacity
  double remaining = sharedMemory->GetRemainingBuffer();

  // Calculate recent drop-to-sent ratio for the queue
  double dropSentRatio = (sentAggregate > 0) ? (double)dropAggregate / sentAggregate : 0.0;

  // Dynamically adjust alpha based on drop-to-sent ratio
  double dynamicAlpha = alphas[priority] * (1.0 - dropSentRatio);

  // Reserve some buffer for burst handling
  double burstFactor = (sentAggregate > dropAggregate) ? 0.1 : 0.05; // More reserved for active queues
  double reservedBuffer = remaining * burstFactor;

  // Calculate the queue threshold dynamically
  uint64_t maxThreshold = (dynamicAlpha * (remaining - reservedBuffer));
  int64_t now = Simulator::Now().GetMicroSeconds();
  if (now%1000==0) std::cout << "LLM," << Simulator::Now() << "," << maxThreshold << std::endl;

  // Retrieve the current queue size and packet size
  uint32_t currentQueueSize = GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes();
  uint32_t packetSize = packet->GetSize();

  // Make the enqueue/drop decision
  if ((currentQueueSize + packetSize > maxThreshold) || (remaining < packetSize)) {
      return false; // Drop the packet
  } else {
      return true; // Enqueue the packet
  }
}

bool GenQueueDisc::LLM_DT_BRDG_2_BM(uint32_t priority, Ptr<Packet> packet) {
  // Get the total remaining buffer on the router
  double remaining = sharedMemory->GetRemainingBuffer();
  uint64_t reservedBuffer = 0.2 * remaining; // Reserve 20% for bursts
  double availableBuffer = remaining - reservedBuffer;

  // Fetch aggregated statistics
  uint64_t sentBytes = sentAggregate;
  uint64_t droppedBytes = dropAggregate;
  
  // Calculate efficiency and penalize high drop-to-sent ratio
  double efficiency = sentBytes > 0 ? (1.0 - static_cast<double>(droppedBytes) / sentBytes) : 1.0;
  efficiency = std::max(0.1, efficiency); // Cap efficiency to avoid division by zero

  // Calculate dynamic threshold
  uint64_t maxSize = static_cast<uint64_t>(alphas[priority] * availableBuffer * efficiency);
  int64_t now = Simulator::Now().GetMicroSeconds();
  if (now%1000==0) std::cout << "LLM," << Simulator::Now() << "," << maxSize << std::endl;

  // Get the current queue size
  uint32_t qSize = GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes();
  uint32_t packetSize = packet->GetSize();

  // Enqueue or drop decision
  if ((qSize + packetSize > maxSize) || (remaining < packetSize)) {
      return false; // Drop
  } else {
      return true; // Enqueue
  }
}

// std::vector<uint32_t> list_queue_length;
// std::vector<uint32_t> list_sent;
// std::vector<uint32_t> list_drop;
// void GenQueueDisc::LLM_collect_stats(uint32_t priority) {
//   uint32_t proberid = sharedMemory->getProberId(portId, priority);
//   // uint32_t queueid = proberid % nPrior;
//   // if (verbose) std::cout << std::endl;

//   uint32_t drop = sharedMemory->probeMinTotalDropBytes[proberid];
//   uint32_t sent = sharedMemory->probeMinAverageThroughput[proberid];
//   uint32_t qlen = GetQueueDiscClass (priority)->GetQueueDisc ()->GetNBytes(); // instantaneous queue length
//   list_queue_length.push_back(qlen);
//   list_sent.push_back(sent);
//   list_drop.push_back(drop);

//   sharedMemory->probeMinTotalDropBytes[proberid] = 0;
//   sharedMemory->probeMinAverageThroughput[proberid] = 0;

//   Simulator::Schedule(MilliSeconds(monitorlongms), &GenQueueDisc::LLM_collect_stats, this, priority);
// }

// bool GenQueueDisc::LLM_DT_COT_0_BM(uint32_t priority, Ptr<Packet> packet){
//   // Calculate historical statistics
//   // if (list_queue_length.size()==0) return true;

//   double avg_queue_length = std::accumulate(list_queue_length.begin(), list_queue_length.end(), 0.0) / list_queue_length.size();
//   double avg_drop = std::accumulate(list_drop.begin(), list_drop.end(), 0.0) / list_drop.size();
//   double avg_sent = std::accumulate(list_sent.begin(), list_sent.end(), 0.0) / list_sent.size();
//   double drop_rate = (avg_sent > 0) ? (avg_drop / avg_sent) : 0.0;

//   // Get current buffer status
//   double remaining = sharedMemory->GetRemainingBuffer();
//   uint64_t maxSize = alphas[priority] * remaining;

//   // Penalize high-drop-rate queues by adjusting the threshold
//   if (drop_rate > 0.1) { // Example: Penalize queues with drop rate > 10%
//       maxSize *= 0.8;    // Reduce threshold by 20%
//   }
//   // std::cout << "DEBUG," << avg_queue_length << "," << avg_drop << "," << avg_sent << "," << drop_rate << "," << priority << "," << alphas[priority] << "," << remaining << "," << maxSize << std::endl;

//   // Calculate moving average for queue length to stabilize thresholds
//   double smoothed_threshold = (0.7 * maxSize) + (0.3 * avg_queue_length);
//   int64_t now = Simulator::Now().GetMicroSeconds();
//   if (now%1000==0) std::cout << "LLM," << Simulator::Now() << "," << smoothed_threshold << std::endl;

//   // Get current queue size and packet size
//   uint32_t qSize = GetQueueDiscClass(priority)->GetQueueDisc()->GetNBytes();
//   uint32_t packetSize = packet->GetSize();

//   // Drop logic: check against smoothed threshold and remaining buffer
//   if ((qSize + packetSize > smoothed_threshold) || (remaining < packetSize)) {
//       return false; // drop
//   } else {
//       return true; // enqueue
//   }
// }

} // namespace ns3
