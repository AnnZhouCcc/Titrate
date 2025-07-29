/*
* Ann Zhou, copied this file entirely from Vamsi (I think). It looks like
* this file is adapted from prio-queue-disc.h
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

#ifndef GEN_QUEUE_DISC_H
#define GEN_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include <array>

#include "unordered_map"
#include "ns3/simulator.h"
#include "shared-memory.h"
// #include "utility-warehouse.h"

namespace ns3 {

/**
 * \ingroup traffic-control
 *
 * The Prio qdisc is a simple classful queueing discipline that contains an
 * arbitrary number of classes of differing priority. The classes are dequeued
 * in numerical descending order of priority. By default, three Fifo queue
 * discs are created, unless the user provides (at least two) child queue
 * discs.
 *
 * If no packet filter is installed or able to classify a packet, then the
 * packet is assigned a priority band based on its priority (modulo 16), which
 * is used as an index into an array called priomap. If a packet is classified
 * by a packet filter and the returned value is non-negative and less than the
 * number of priority bands, then the packet is assigned the priority band
 * corresponding to the value returned by the packet filter. Otherwise, the
 * packet is assigned the priority band specified by the first element of the
 * priomap array.
 */
class GenQueueDisc : public QueueDisc {
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief GenQueueDisc constructor
   */
  GenQueueDisc ();

  virtual ~GenQueueDisc();

  // Reasons for dropping packets
  static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";  //!< Packet dropped due to queue disc limit exceeded


  void setStrictPriority() {
    strict_priority = 1;
    round_robin=0;//Just to avoid clash
  }

  void setRoundRobin() {
    round_robin = 1;
    strict_priority = 0;//Just to avoid clash
  }

  // std::pair<double,double> 
  std::vector<double>
  GetThroughputQueue(uint32_t p,double nanodelay);

  double GetThroughputPort(double nanodelay);
  double GetThroughputEnQueue(uint32_t p,double nanodelay);

  uint64_t GetBuffersize(uint32_t p);

  uint64_t GetDroppedBytes(uint32_t p);
  double GetAlpha(uint32_t p);
  double GetRemainingBuffer();

  void setNPrior(uint32_t np) {
     nPrior = np;
     alphas = new double[nPrior];
  }
  double *alphas;


  uint32_t getNPrior() {
    return nPrior;
  }

  void setUpTrackingStats(uint32_t numqueues);

  void setPortBw(double bw){portBW = bw;}
  // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
  void setRTT(double rtt) { RTTms = rtt; }

  // void setTargetBw(uint32_t bw) {targetBW = bw;}
  void setTargetBw(uint32_t bw) {ssthreshBuffer = bw;}

  void SetSharedMemory(Ptr<SharedMemoryBuffer> sm){sharedMemory=sm;}
  // void setUtilityWarehouse(Ptr<UtilityWarehouse> warehouse) {utilityWarehouse=warehouse;}

  void SetBufferAlgorithm(uint32_t alg){
    bufferalg=alg;
  }

  void SetPortId(uint32_t port){portId=port;}
  uint32_t getPortId(){return portId;}

  // void setHeadRoomQueueScheme(uint32_t scheme) { headRoomQueueScheme = scheme; }
  void setMainRoomQueueScheme(uint32_t scheme) { mainRoomQueueScheme = scheme; }
  // void setHeadRoomNumQueues(uint32_t numq) { headRoomNumQueues = numq; }
  void setMainRoomNumQueues(uint32_t numq) { mainRoomNumQueues = numq; }

  void setProbingStats(uint32_t _startProbeBuffer, uint16_t _monitorLongMs, double _dropRateThreshold, double _adaptiveIncreaseParameter, double _adaptiveDecreaseParameter, uint32_t _smoothQlenCollectionByUs, uint32_t _smoothWindowByNumData, uint32_t _smoothOutlierThresholdByMultiple, std::string _pawMode);

  // void setStartProbeBuffer(uint32_t buffer) { startProbeBuffer = buffer; }
  // void setMonitorLongMs(uint16_t value) { monitorlongms = value; }
  // void setDropRateThreshold(double value) { dropRateThreshold = value; }

  void SetFabWindow(Time t){FabWindow=t;}
  void SetFabThreshold(uint32_t n){FabThreshold=n;}

  void SetName(std::string name){switchname=name;}

  bool DynamicThresholds(uint32_t priority, Ptr<Packet> packet);

  void UpdateDequeueRate(double nanodelay);
  void UpdateNofP();
  void InvokeUpdates(double nanodelay);
  bool ActiveBufferManagement(uint32_t priority, Ptr<Packet> packet);

  bool FlowAwareBuffer(uint32_t priority, Ptr<Packet> packet);

  bool CompleteSharing(uint32_t priority, Ptr<Packet> packet);

  int DropAfd(double prob,uint32_t priority);
  void SetAfdWindow(Time t){AfdWindow=t;}
  void SetQrefAfd(uint32_t p, uint32_t ref);//{QRefAfd[p]=ref;}
  uint32_t GetQrefAfd(uint32_t p);//{return QRefAfd[p];}
  void SetDppWindow(Time t){DppWindow=t;}
  void SetDppThreshold(uint32_t n){DppThreshold=n;}
  bool IntelligentBuffer(uint32_t priority, Ptr<Packet> packet);

  bool AcceptPacket(uint32_t priority, Ptr<Packet> packet);

  void TrimPacket(Ptr<Packet> packetCopy);

  bool MyBM(uint32_t priority, Ptr<Packet> packet, uint32_t bmType=0);
  void setUpHeadRoomNonProber(uint32_t priority, uint32_t flowid, uint32_t bmType);
  void setUpMainRoomProber(uint32_t priority, uint32_t flowid, uint32_t BDP);
  void checkBurstOrLong(uint32_t priority, uint32_t flowid, uint32_t bmType);
  void startProbing(uint32_t proberid, uint32_t flowid, uint32_t bmType);
  void probeMinBufferForMaxUtility(uint32_t priority);
  void probeMinBufferSetUp(uint32_t priority);
  void probeMinBuffer(uint32_t priority);
  bool isNewFlow(uint32_t flowId);
  bool isNewProber(uint32_t proberId);
  void probeMinMonitorLongInvoke(uint32_t proberid, uint32_t bmType);
  void probeMinMonitorLongInvoke2(uint32_t proberid, uint64_t nextmoveus);
  void probeMinMonitorLongCollect(uint32_t proberid);
  void probeMinMonitorLongCollectSimple(uint32_t proberid, uint32_t bmType);
  void probeMinMonitorLongCollectSimple2(uint32_t proberid, int64_t intstartns, double mean, double variance, uint64_t count, double margin_error_prev);
  void probeMinMonitorDropInvoke(uint32_t proberid, uint32_t qSize);
  void probeMinMonitorDropCollect(uint32_t proberid, int64_t key, uint32_t qSize);
  void removeFromFlowIdSeen(uint32_t flowid) { flowIdSeen.erase(flowid); }
  void startWindowAfterDrop(uint32_t queueid);
  void endWindowAfterDrop(uint32_t queueid, uint32_t window, uint8_t count);

  void setParameters(
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
  );

  uint16_t burstNRTT = 0;
  uint16_t waitNRTT = 20;
  uint32_t temporaryWaitMs = 10;
	uint16_t aggregateNRTT = 50;
  uint16_t monitorlongNRTT = 50;
  uint16_t monitordropNRTT = 2;
  double goodDroprate = 0.0015;
  // double goodDroprate = 0.005;
  bool isMyBM = false;
  uint32_t minQlenPossible = 1000000;

  uint32_t smoothQlenCollectionByUs;
  double smoothOutlierThresholdByMultiple;
  uint32_t smoothWindowByNumData;
  std::vector<std::deque<uint32_t>> smoothQlenRecord;

  void smoothStartMonitoring(uint32_t numqueues);
  double smoothGetAverageQlen(uint32_t p);

  std::string pawMode;
  void insertIntoFixedVaryThresVec(uint32_t key, uint32_t value);

  /**************************** 
   * For COS597K final project
  *****************************/
  void LLM_collect_stats(uint32_t priority);
  bool LLM_DT_COT_0_BM(uint32_t priority, Ptr<Packet> packet);
  bool LLM_DT_COT_1_BM(uint32_t priority, Ptr<Packet> packet);
  bool LLM_DT_COT_2_BM(uint32_t priority, Ptr<Packet> packet);
  bool LLM_DT_BRDG_0_BM(uint32_t priority, Ptr<Packet> packet);
  bool LLM_DT_BRDG_1_BM(uint32_t priority, Ptr<Packet> packet);
  bool LLM_DT_BRDG_2_BM(uint32_t priority, Ptr<Packet> packet);

private:
  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual Ptr<const QueueDiscItem> DoPeek (void);
  virtual bool CheckConfig (void);
  virtual void InitializeParams (void);

  uint64_t droppedBytes[1008]; // AnnC: isn't this per priority?

  /*at enqueue*/
  Time firstSeen[1008];
  Time lastAccepted[1008];
  double numBytesSent[1008];

  /*at dequeue*/
  Time firstSeenQueue[1008];
  Time lastAcceptedQueue[1008];
  double numBytesSentQueue[1008];

  double DeqRate[1008];
  double Deq[1008];

  uint32_t nPrior;
  std::unordered_map<uint32_t,uint32_t> flowPrior;
  Time window;
  Time timeSinceLastChange;
  uint32_t new_index = 0;

  uint32_t dequeueIndex=0;
  uint32_t strict_priority;
  uint32_t round_robin;

  Ptr<SharedMemoryBuffer> sharedMemory;
  uint32_t bufferalg;
  uint32_t portId;
  uint32_t sat;
  std::string switchname; //optional

  // uint32_t headRoomQueueScheme;
  uint32_t mainRoomQueueScheme;
  // uint32_t headRoomNumQueues;
  uint32_t mainRoomNumQueues;

  uint32_t startProbeBuffer;
  uint32_t ssthreshBuffer;
  uint16_t monitorlongms;
  double dropRateThreshold;
  double adaptiveIncreaseParameter;
  double adaptiveDecreaseParameter;
  uint32_t targetBW;
  std::vector<std::pair<uint32_t,uint32_t>> fixedVaryThresVec;

  // Ptr<UtilityWarehouse> utilityWarehouse;

  std::unordered_map<uint32_t,std::pair<uint32_t,Time>> FlowCount; // FlowId --> <packetcounter, LastSeen>

  uint64_t bufferMax[1008]={0};

  uint64_t updateInterval;
  bool firstTimeUpdate = true;

  uint64_t staticBuffer;
  uint64_t staticOccupancy=0;

  double alphaUnsched;

  Time FabWindow; // Needs to be set in experiment code.
  uint32_t FabThreshold; // Needs to be set in experiment code.
  Time AfdWindow; // Needs to be set in experiment code.
  uint32_t QRefAfd[1008]; // Needs to be set in experiment code.
  Time DppWindow; // Needs to be set in experiment code.
  uint32_t DppThreshold; // Needs to be set in experiment code.

  Time timeSinceLastChangeAdf=Seconds(0);
  std::unordered_map<uint32_t,std::pair<uint32_t,uint32_t>> M; // FlowId --> <counterNow, Total count in last window>
  double MFair[1008]={0};
  uint32_t Qold[1008]={0};
  uint32_t DPPQueue=1;

  double a1=1.8;
  double a2=1.7;

  double portBW;// Needs to be set in experiment code via attribute.
  // AnnC: [WrongRTTns] temporarily set RTTns as an attribute of the port
  double RTTms;

  double nofP[1008];

  bool is_homa;

  uint64_t txBytesInt=0;
  bool enableDPPQueue;

  std::set<uint32_t> flowIdSeen;
  std::set<uint32_t> proberStarted;
  std::vector<uint16_t> idealMinBufferProbeCount;
  std::vector<uint16_t> maxBufferUnchangingCount;
  std::vector<uint32_t> prevMaxBufferUsed;
  std::vector<double> prevDropRate;
  std::vector<int64_t> probeMinBufferLastChecked;
  std::vector<uint16_t> zeroDropCount;
  std::vector<int64_t> latestLongCollect;
  std::vector<bool> isWindowOn;

  std::map<uint32_t, uint32_t> flowidHRqueueidMapping;
  std::map<uint32_t, uint32_t> flowidMRqueueidMapping;
  // uint32_t nextAvailableHRqueueid;
  std::map<uint32_t,uint32_t> ccaHRqueueidMapping;

  uint16_t HistLen;
  uint16_t RemoveStartLen;
  uint16_t RemoveStartThres;
  uint16_t ExploreThres;
  uint16_t SafeThres;
  uint16_t ConsecIncreaseThres;
  uint16_t StepIncreaseCap;
  uint16_t IncreaseRatio;
  uint16_t ConsecDecreaseThres;
  uint16_t StepDecreaseCap;
  uint16_t DecreaseRatio;
  uint16_t MinQOutlier;
  uint16_t MinQHold;
};

} // namespace ns3

#endif /* GEN_QUEUE_DISC_H */
