/*
 * Ann Zhou, copied from:
 *
 * shared-memory.h
 *
 *  Created on: May 13, 2020
 *      Author: vamsi
 */

#ifndef SRC_TRAFFIC_CONTROL_MODEL_SHARED_MEMORY_H_BKP_
#define SRC_TRAFFIC_CONTROL_MODEL_SHARED_MEMORY_H_BKP_

#include "ns3/simulator.h"
#include "ns3/object.h"
//#include "ns3/queue-disc.h"
#include "unordered_map"
#include <set>
#include <deque>
#include <map>
#include <random>

namespace ns3 {

class SharedMemoryBuffer : public Object{
public:
	static TypeId GetTypeId (void);
	SharedMemoryBuffer();
	virtual ~SharedMemoryBuffer();

	void SetSharedBufferSize(uint32_t size);
	uint32_t GetSharedBufferSize();

	uint32_t GetOccupiedBuffer(){return OccupiedBuffer;}
	uint32_t GetRemainingBuffer(){return RemainingBuffer;}
	double GetNofP(uint32_t priority);
	bool EnqueueBuffer(uint32_t size);
	void DequeueBuffer(uint32_t size);

	void AddN(uint32_t group){N[group]++;}
	void SubN(uint32_t group){N[group]--;}

	void setSaturated(uint32_t port,uint32_t priority, double satLevel);
	
	uint32_t isSaturated(uint32_t port,uint32_t priority){return saturated[port][priority];}

	void setPriorityToGroup(uint32_t priority,uint32_t group){PriorityToGroupMap[priority]=group;}

	uint32_t getPriorityToGroup(uint32_t priority){return PriorityToGroupMap[priority];}

	// void updateN(void);

	double getDeq(uint32_t prio,uint32_t port);
	void addDeq(uint32_t bytes,uint32_t prio, uint32_t port);
	Time getTimestamp(uint32_t port, uint32_t queue){return timestamp[port][queue];}
	void setTimestamp(Time x,uint32_t port,uint32_t queue){timestamp[port][queue]=x;}

	void PerPriorityStatEnq(uint32_t size, uint32_t priority);
	void PerPriorityStatDeq(uint32_t size, uint32_t priority);

	uint32_t GetPerPriorityOccupied(uint32_t priority){return OccupiedBufferPriority[priority];}

	// bool isNewFlow(uint64_t flowId);
	void setUp(uint32_t numports, uint32_t numqueues, uint32_t randomseed, double sRTTms, uint32_t mrnumqueues);
	// void setUpSchedule();
	// void probeMinBuffer(uint32_t proberId);
	// void probeMinBufferSchedule9RTT(uint32_t proberId);
	// void probeMinBufferSchedule1RTT(uint32_t proberId, bool maxBufferReached);
	uint32_t getProberId(uint32_t portid, uint32_t queueid) { return portid * numQueues + queueid; }
	uint32_t getPortId(uint32_t proberid) { return proberid / numQueues; }
	// uint32_t getQueueId(uint32_t proberid) { return proberid % numQueues; }
	uint32_t getTotalProbers() { return numPorts*numQueues; }
	// uint32_t getMyRemainingBuffer();
	// uint32_t getMinBufferThreshold(uint32_t proberid) { return minBufferThreshold[proberid]; }
	// uint32_t getCurrMaxSizeAllowed(uint32_t portid, uint32_t queueid) { return currMaxSizeAllowed[getProberId(portid,queueid)]; }
	uint32_t getCurrMaxSizeAllowed(uint32_t proberid) { return currMaxSizeAllowed[proberid]; }
	int64_t getCurrMaxSizeAllowedLastChanged(uint32_t proberid) { return currMaxSizeAllowedLastChanged[proberid]; }
	void setCurrMaxSizeAllowedLastChanged(uint32_t proberid, int64_t value) { currMaxSizeAllowedLastChanged[proberid] = value; }
	// uint32_t getBufferSizeLock(uint32_t portid, uint32_t queueid) { return bufferSizeLock[getProberId(portid,queueid)]; }
	// uint64_t getBufferSizeLockStart(uint32_t proberid) { return bufferSizeLockStart[proberid]; }
	// void setBufferSizeLock(uint32_t proberid, bool value) { bufferSizeLock[proberid] = value; }
	// void setBufferSizeLockStart(uint32_t proberid, uint64_t value) { bufferSizeLockStart[proberid] = value; }
	// void setMinBufferThreshold(uint32_t proberid, uint32_t threshold);
	// void setBurstToleranceThreshold(uint32_t portid, uint32_t queueid, uint32_t threshold) { burstToleranceThreshold[getProberId(portid,queueid)] = threshold; }
	void setCurrMaxSizeAllowed(uint32_t proberid, uint32_t threshold) { currMaxSizeAllowed[proberid] = threshold; }
	// bool getDoMonitorDrop(uint32_t proberid) { return doMonitorDrop[proberid]; }
	// void setDoMonitorDrop(uint32_t proberid, bool value);
	// void setNormalizedPortBW(uint32_t proberid, double bw) { normalizedPortBW[proberid] = bw; }
	// void setAverageQueueRTT(uint32_t proberid, uint32_t rtt) { averageQueueRTT[proberid] = rtt; }
	// void addProberToProberAwaitingMinBuffer(uint32_t proberId);
	// bool isProberInProberAwaitingMinBuffer(uint32_t proberId);
	// void addProberToProberDoneMinBuffer(uint32_t proberId) { proberDoneMinBuffer.insert(proberId); }
	// bool isProberInProberDoneMinBuffer(uint32_t proberId);
	// uint32_t getStartupNRTT() { return startupNRTT; }
	// bool isKeyInThroughputData(uint32_t proberId, uint32_t bufferSize);
	// void setThroughputData(uint32_t proberId, uint32_t bufferSize, double value);
	// void setDroprateData(uint32_t proberId, uint32_t bufferSize, double value);
	// void setPacketRateZeroQueueData(uint32_t proberId, uint32_t bufferSize, double value);
	// void setPacketCountZeroQueueData(uint32_t proberId, uint32_t bufferSize, uint32_t value);
	// void setPacketCountTotalData(uint32_t proberId, uint32_t bufferSize, uint32_t value);
	// void setDropDurationZeroQueueData(uint32_t proberId, uint32_t bufferSize, double value);
	// void setLongDurationZeroQueueData(uint32_t proberId, uint32_t bufferSize, double value);
	// void updateIdealMinBufferUsedData(uint32_t proberId, uint32_t minBufferUsed, int64_t timestamp, uint32_t currMaxSizeAllowed);
	// void allocateBasedOnMinBufferThreshold();
	// void addProberToProberNewerFlows(uint32_t proberId) { proberNewerFlows.insert(proberId); }
	// void removeProberToProberNewerFlows(uint32_t proberId) { proberNewerFlows.erase(proberId); }
	// bool isProberInProberNewerFlows(uint32_t proberId);
	bool isProberInHeadRoom(uint32_t proberId);
	bool isProberInWaitRoom(uint32_t proberId);
	// void collectDebugStats();
	void allocateBufferSpaceSimple(uint32_t thisProberId, int32_t marginalRequest);
	// void allocateBufferSpace(uint32_t thisProberId, int32_t marginalRequest);
	// void allocateBufferSpace2(uint32_t thisProberId, int32_t marginalRequest);
	// void allocateBufferSpaceDeprecated(uint32_t thisProberId, int32_t marginalRequest);
	// void allocateHeadRoomPartitioning(uint32_t totalHeadRoom);
	// void allocateHeadRoomPartitioningDeprecated(uint32_t totalHeadRoom);
	// void adjustHeadRoomForFlowIdEnded(uint32_t proberid);
	// void allocateMainRoomUtility(uint32_t demand, uint32_t supply, uint32_t thisProberId, int32_t marginalRequest);
	// void allocateMainRoomSpace(uint32_t demand, uint32_t supply, uint32_t thisProberId, int32_t marginalRequest);
	void addToProberInHeadRoom(uint32_t proberId);
	void removeFromProberInHeadRoom(uint32_t proberId);
	void addToProberInWaitRoom(uint32_t proberId);
	void removeFromProberInWaitRoom(uint32_t proberId);
	void checkChangeInCurrMaxSizeAllowed(uint32_t proberId, uint32_t prevCMSA, uint32_t thisCMSA);
	// uint32_t countOccurrencesInVector(std::vector<uint32_t> vec, uint32_t target);
	// double intrapolateDeltaY(uint32_t proberid, uint32_t x1, uint32_t x2);
	// bool ifSetContains(std::set<uint32_t> set, uint32_t target);
	uint32_t getAbsoluteMinBuffer() { return absoluteMinBuffer; }
	uint32_t getBurstReserve() { return burstReserve; }
	// void increaseQSize(uint32_t proberid, uint32_t size) { qSize[proberid] += size; }
	// void decreaseQSize(uint32_t proberid, uint32_t size) { qSize[proberid] -= size; }
	// void setQSize(uint32_t proberid, uint32_t size) { qSize[proberid] = size; }
	// bool hasInstanceInBetweenProbeMinDurationZeroQueueMonitorMap(uint32_t proberid, uint32_t startkey, uint32_t endkey);
	void addToFlowIdEnded(uint32_t flowid) { flowIdEnded.insert(flowid); }
	bool isFlowEnded(uint32_t flowid) { return flowIdEnded.find(flowid) != flowIdEnded.end(); }
	void addToFlowIdByProberIdEnded(uint32_t flowid, uint32_t proberid) { flowIdByProberIdEnded.insert( std::pair<uint32_t,uint32_t>(flowid,proberid) ); }
	void removeFromFlowIdByProberIdEnded(uint32_t flowid, uint32_t proberid) { flowIdByProberIdEnded.erase( std::pair<uint32_t,uint32_t>(flowid,proberid) ); }
	bool isFlowIdByProberIdEnded(uint32_t flowid, uint32_t proberid);
	// void setStatus(uint32_t proberid, uint32_t flowid, uint32_t status);
	// void checkHeadRoomWaitRoom(uint32_t proberid, bool isHRprober);
	// uint32_t getStatus(uint32_t proberid, uint32_t flowid);
	// bool isProberActiveInHeadRoom(uint32_t proberid);
	// bool isProberStaticInHeadRoom(uint32_t proberid);
	// bool isProberActiveInMainRoom(uint32_t proberid);
	// bool isProberStaticInMainRoom(uint32_t proberid);
	// void setEffectiveCurrMaxSizeAllowed(uint32_t proberid, uint32_t threshold) { effectiveCurrMaxSizeAllowed[proberid] = threshold; }
	// void updateAverageQSize();
	// void beliefScalingPeriodic();
	// void updateDropsDueToRemainingBuffer(uint32_t proberid, bool reset, bool increment);
	// void updateQSizeQueue();
	// double getMainRoomBeliefScaling();
	void printDesignZeroVec(uint32_t proberId);

	// Reset every probe
	std::vector<double> probeMinAverageThroughput;
	std::vector<uint32_t> probeMinTotalDropBytes;
	std::vector<uint32_t> probeMinMaxBufferUsed;
	std::vector<uint32_t> probeMinMinBufferUsed;
	std::vector<uint32_t> probeMinPacketCountZeroQueue;
	std::vector<uint32_t> probeMinPacketCountTotal;
	std::vector<int64_t> probeMinLastTimestampNonZeroQueue;
	std::vector<int64_t> probeMinDurationZeroQueue; // in us
	std::vector<int64_t> designZeroStart; // in ns
	std::vector<std::vector<uint64_t>> designZeroVec; // in ns
	std::vector<std::vector<int64_t>> designZeroTimestamp;
	std::vector<int64_t> designZeroWindowSum; // in ns
	std::vector<int64_t> designZeroWindowStart; // in ns

	// Reset every flow
	// std::vector<uint32_t> probeMinMaxBufferUsedRecord;
	// std::vector<uint16_t> probeMinMaxBufferTimes;

	// std::vector< std::map<int64_t,uint32_t> > probeMinTotalDropBytesMonitorMap;
	// std::vector< std::map<int64_t,uint32_t> > probeMinMaxBufferUsedMonitorMap;
	// std::vector< std::map<int64_t,uint32_t> > probeMinMinBufferUsedMonitorMap;
	// std::vector< std::map<int64_t,int64_t> > probeMinDurationZeroQueueMonitorMap;
	// std::vector<std::map<uint32_t, int64_t>> idealMinBufferUsedData;

	std::set<uint32_t> flowIdEnded;
	std::set< std::pair<uint32_t,uint32_t> > flowIdByProberIdEnded;

	// std::vector<double> debugAverageThroughput;
	// std::vector<uint32_t> debugTotalDropBytes;
	// std::vector<uint32_t> debugMaxBufferUsed;
	// std::vector<uint32_t> debugMinBufferUsed;
	// double debugStatsMs = 1000;

	// uint16_t startupNRTT = 12;
	// uint16_t aggregateNRTT = 10;
	// uint16_t cubicDrainNRTT = 1;
	// uint16_t nonCubicNProbe = 8;
	// AnnC: assumption here is that all flows will at least take absoluteMinBuffer amount of buffer
	uint32_t absoluteMinBuffer = 1500; // 1 packet
	bool setMainRoomBeliefScalingToDefault = false;
	double smallestRTTms;
	uint32_t prevMainRoomBelief;
	uint32_t mainRoomNumQueues;
	bool invadedHeadRoom;

	std::map<uint32_t, std::map<uint32_t,uint32_t>> statusTracker;

	// uint32_t averageQSize;
	// uint32_t averageQSizeCounter;
	// uint32_t averageQSizeSum;
	// uint32_t averageEffectiveCMSA;
	// uint32_t averageEffectiveCMSASum;
	// uint16_t averageQSizeGranularityMS = 1;
	// uint16_t averageQSizeNumSamples = 500;
	// double mainRoomBeliefScaling;
	// double defaultMainRoomBeliefScaling = 3;
	// // std::vector<uint32_t> dropsDueToRemainingBuffer;
	// std::deque<uint32_t> qsizeQueue;
	// std::deque<uint32_t> ecmsaQueue;

protected:
	void DoInitialize (void);

	virtual void DoDispose (void);


private:
	uint32_t TotalBuffer;
	uint32_t OccupiedBuffer;
	uint32_t OccupiedBufferPriority[1008]={0};
	uint32_t RemainingBuffer;
	double N[1008]; // N corresponds to each queue (one-one mapping with priority) at each port. 8 queues exist at each port.
	double saturated[100][1008]; // 100 ports and 8 queues per node which share the buffer, are supported for now. // AnnC: increase to q queues per port, just in case, mostly for the situation with burst under fair queueing // AnnC: try to increase to 1008
	uint32_t maxports=100;
	uint32_t maxpriority=1008;

	std::unordered_map<uint32_t,uint32_t> PriorityToGroupMap;

	std::vector<std::pair<uint32_t,Time>> Deq[100][1008];
	double sumBytes[100][1008];
	Time tDiff[100][1008];
	uint64_t MaxRate;
	Time timestamp[100][1008];

	// std::set<uint64_t> flowIdSeen;
	uint32_t numPorts;
	uint32_t numQueues;
	uint32_t burstReserve;
	// std::deque<uint32_t> proberAwaitingMinBuffer;
	// std::set<uint32_t> proberDoneMinBuffer;
	// std::set<uint32_t> proberNewerFlows;
	// std::vector<std::map<uint32_t, double>> throughputData;
	// std::vector<std::map<uint32_t, double>> droprateData;
	// std::vector<std::map<uint32_t, double>> packetratezeroqueueData;
	// std::vector<std::map<uint32_t, uint32_t>> packetcountzeroqueueData;
	// std::vector<std::map<uint32_t, uint32_t>> packetcounttotalData;
	// std::vector<std::map<uint32_t, double>> dropDurationZeroQueueData;
	// std::vector<std::map<uint32_t, double>> longDurationZeroQueueData;
	// std::set<uint32_t> proberInHeadRoom;
	// std::set<uint32_t> proberInWaitRoom;
	std::map<uint32_t, uint32_t> proberInHeadRoom;
	std::map<uint32_t, uint32_t> proberInWaitRoom;

	// std::vector<bool> bufferSizeLock;
	// std::vector<uint32_t> bufferSizeLockStart;
	std::vector<uint32_t> currMaxSizeAllowed;
	// std::vector<uint32_t> minBufferThreshold;
	// std::vector<uint32_t> burstToleranceThreshold;
	std::vector<int64_t> currMaxSizeAllowedLastChanged;
	// std::vector<double> normalizedPortBW;
	// std::vector<uint32_t> averageQueueRTT;
	// std::vector<bool> doMonitorDrop;
	// std::vector<uint32_t> qSize;
	// std::vector<uint32_t> effectiveCurrMaxSizeAllowed;

	std::default_random_engine dre;
};
}



#endif /* SRC_TRAFFIC_CONTROL_MODEL_SHARED_MEMORY_H_BKP_ */
