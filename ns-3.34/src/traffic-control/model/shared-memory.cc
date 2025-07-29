/*
 * Ann Zhou, copied from:
 *
 * shared-memory.cc
 *
 *  Created on: May 13, 2020
 *      Author: vamsi
 */



#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/uinteger.h"
#include "ns3/pointer.h"
#include "ns3/object-vector.h"
#include "ns3/packet.h"
#include "ns3/socket.h"
#include "ns3/unused.h"
#include "ns3/simulator.h"
#include "shared-memory.h"
#include <unistd.h>
#include "ns3/simulator.h"
#include <algorithm>
#include <vector>

# define verbose false
# define debug false

/*StatusTracker*/
# define HREntering 301
# define HRFlowEnd 302
# define HRClearPackets 303
# define MRWaitRoom 304
# define MRProbing 305
# define MRFlowEnd 306
# define MRClearPackets 307
# define StatusNone 308

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("SharedMemoryBuffer");
NS_OBJECT_ENSURE_REGISTERED (SharedMemoryBuffer);


TypeId SharedMemoryBuffer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SharedMemoryBuffer")
    .SetParent<Object> ()
   	.AddConstructor<SharedMemoryBuffer> ()
		.AddAttribute ("BufferSize",
	                   "TotalBufferSize",
	                   UintegerValue (1000*1000),
	                   MakeUintegerAccessor (&SharedMemoryBuffer::TotalBuffer),
	                   MakeUintegerChecker <uint32_t> ())
		.AddAttribute ("BurstReserve",
	                   "The amount of buffer, in bytes, reserved for burst (when all existing flows are continuous)",
	                   UintegerValue (1000*1000),
	                   MakeUintegerAccessor (&SharedMemoryBuffer::burstReserve),
	                   MakeUintegerChecker <uint32_t> ())
		;
  return tid;
}

SharedMemoryBuffer::SharedMemoryBuffer(){
	for (int i=0;i<1008;i++){
		N[i]=0;
	}
	for (int i=0;i<99;i++){ // AnnC: should this be 100?
		for (int j=0;j<1008;j++){
			saturated[i][j]=0;
			sumBytes[i][j]=1500;
			tDiff[i][j]=Seconds(0);
		}
	}

	OccupiedBuffer=0;
}

SharedMemoryBuffer::~SharedMemoryBuffer ()
{
  NS_LOG_FUNCTION (this);
}


void
SharedMemoryBuffer::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  for (int i=0;i<1008;i++){
  		N[i]=0;
  	}
  	for (int i=0;i<99;i++){ // AnnC: 100?
  		for (int j=0;j<1008;j++){
  			saturated[i][j]=0;
  			timestamp[i][j]=Seconds(0);
  		}
  	}
  	TotalBuffer=0;
  	OccupiedBuffer=0;
  	RemainingBuffer=0;
  	maxports=0;
  	maxpriority=0;

  Object::DoDispose ();
}

void
SharedMemoryBuffer::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  for (int i=0;i<1008;i++){
  		N[i]=1;
  	}
  	for (int i=0;i<99;i++){ // AnnC: 100?
  		for (int j=0;j<1008;j++)
  			saturated[i][j]=0;
  	}

  	OccupiedBuffer=0;
  Object::DoInitialize ();
}


void SharedMemoryBuffer::SetSharedBufferSize(uint32_t size){
	TotalBuffer = size;
	RemainingBuffer = size-OccupiedBuffer;
}
uint32_t SharedMemoryBuffer::GetSharedBufferSize(){
	return TotalBuffer;
}

void SharedMemoryBuffer::PerPriorityStatEnq(uint32_t size, uint32_t priority){
	OccupiedBufferPriority[priority] += size;
}

void SharedMemoryBuffer::PerPriorityStatDeq(uint32_t size, uint32_t priority){
	OccupiedBufferPriority[priority] -= size;
}

bool SharedMemoryBuffer::EnqueueBuffer(uint32_t size){
	if(RemainingBuffer>size){
		RemainingBuffer-=size;
		OccupiedBuffer+=size;
		return true;
	}
	else{
		return false;
	}
}

void SharedMemoryBuffer::DequeueBuffer(uint32_t size){
	if(OccupiedBuffer>size){
		OccupiedBuffer-=size;
		RemainingBuffer+=size;
	}
}

double SharedMemoryBuffer::GetNofP(uint32_t priority){
	if(N[priority]>=1)
		return N[priority];
	else
		return 1;
}


// # define 	DT 		101
// # define 	FAB 	102
// # define 	AFD 	103
// # define 	ABM 	110

// uint64_t SharedMemoryBuffer::getThreshold(uint32_t priority, uint32_t unsched){
// 	uint64_t threshold = TotalBuffer;
// 	switch(algorithm){
// 		case DT:
// 			threshold = alpha[priority]*RemainingBuffer;
// 			break;
// 		case FAB:
			

// 	}
// 	if (unsched){
// 		return (alphaUnsched*(RemainingBuffer)/(GetNofP(priority)));
// 	}
// }


void SharedMemoryBuffer::setSaturated(uint32_t port,uint32_t priority, double satLevel){
	N[priority] += satLevel - saturated[port][priority];
	saturated[port][priority]=satLevel;
}

void SharedMemoryBuffer::addDeq(uint32_t bytes,uint32_t prio, uint32_t port){
	if(Deq[port][prio].size() > 100){
		sumBytes[port][prio]-= Deq[port][prio][0].first;
		Deq[port][prio].erase(Deq[port][prio].begin());
	}

	std::pair<uint32_t,Time> temp;
	temp.first=bytes;
	temp.second=Simulator::Now();
	Deq[port][prio].push_back(temp);
	sumBytes[port][prio]+=bytes;
}

double SharedMemoryBuffer::getDeq(uint32_t prio,uint32_t port){
	Time t = Seconds(0);
//	std::cout << "Size " << Deq[port][prio].size() << " SumBytes " << sumBytes[port][prio] << std::endl;
	if(Deq[port][prio].size()>1){
//		std::cout << "tEnd " << (Deq[port][prio].end()-1)->second.GetSeconds() << " tBegin " << Deq[port][prio].begin()->second.GetSeconds() << std::endl;
		t = (Deq[port][prio].end()-1)->second - Deq[port][prio].begin()->second;
	}
	else
		return 1;
	double deq = 8*sumBytes[port][prio]/t.GetSeconds()/MaxRate;
//	std::cout << "Size " << Deq[port][prio].size() << " SumBytes " << sumBytes[port][prio] << " Deq " << deq << " t " << t.GetSeconds()<< std::endl;
//	std::cout << "Deq " << deq << std::endl;
	if (deq>1 || deq<0) // sanity check
		return 1;
	else
		return deq;
}

// void SharedMemoryBuffer::collectDebugStats() {
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		uint64_t totaldropbytes = debugTotalDropBytes[i];
// 		uint64_t totalsentbytes = debugAverageThroughput[i];
// 		uint64_t maxbuffer = debugMaxBufferUsed[i];
// 		uint64_t minbuffer = debugMinBufferUsed[i];
// 		double droprate = totaldropbytes / (double)totalsentbytes;
// 		// double throughput = 8.0*totalsentbytes/(debugStatsMs*1000000)/normalizedPortBW[i];
// 		double throughput = 8.0*totalsentbytes/(debugStatsMs*1000000)/0.01;
// 		debugTotalDropBytes[i] = 0;
// 		debugAverageThroughput[i] = 0;
// 		debugMaxBufferUsed[i] = 0;
// 		debugMinBufferUsed[i] = TotalBuffer;
// 		if (throughput>0) std::cout << "***debug stats: " << Simulator::Now() << ", prober " << i << ", throughput=" << throughput << ", droprate=" << droprate << ", maxbufferused=" << maxbuffer << ", minbufferused=" << minbuffer << std::endl;
// 	}
// 	Simulator::Schedule(NanoSeconds(debugStatsMs*1000000),&SharedMemoryBuffer::collectDebugStats,this);
// }

void SharedMemoryBuffer::setUp(uint32_t numports, uint32_t numqueues, uint32_t randomseed, double sRTTms, uint32_t mrnumqueues) {
	numPorts = numports;
	numQueues = numqueues;
	smallestRTTms = sRTTms;
	prevMainRoomBelief = 0;
	mainRoomNumQueues = mrnumqueues;
	invadedHeadRoom = false;
	std::cout << "smallest RTT on device in ms = " << smallestRTTms << std::endl;
	for (uint64_t i=0; i<getTotalProbers(); i++) {
		// bufferSizeLock.push_back(false);
		// bufferSizeLockStart.push_back(0);
		currMaxSizeAllowed.push_back(0);
		// minBufferThreshold.push_back(0);
		// burstToleranceThreshold.push_back(0);
		// normalizedPortBW.push_back(0);
		// averageQueueRTT.push_back(0);
		currMaxSizeAllowedLastChanged.push_back(0);
		// doMonitorDrop.push_back(true);
		// doMonitorDrop.push_back(false);
		// qSize.push_back(0);
		// effectiveCurrMaxSizeAllowed.push_back(1); // AnnC: hopefully this is a weird enough number
		// dropsDueToRemainingBuffer.push_back(0);

		probeMinAverageThroughput.push_back(0);
		probeMinTotalDropBytes.push_back(0);
		probeMinMaxBufferUsed.push_back(0);
		probeMinMinBufferUsed.push_back(1000000000);
		probeMinPacketCountZeroQueue.push_back(0);
		probeMinPacketCountTotal.push_back(0);
		probeMinLastTimestampNonZeroQueue.push_back(-1);
		probeMinDurationZeroQueue.push_back(0);
		designZeroStart.push_back(-1);
		std::vector<uint64_t> zerovec;
		designZeroVec.push_back(zerovec);
		std::vector<int64_t> zerots;
		designZeroTimestamp.push_back(zerots);
		designZeroWindowSum.push_back(0);
		designZeroWindowStart.push_back(0);

		// probeMinMaxBufferUsedRecord.push_back(0);
		// probeMinMaxBufferTimes.push_back(0);

		// std::map<int64_t,uint32_t> map1;
		// std::map<int64_t,uint32_t> map2;
		// std::map<int64_t,uint32_t> map3;
		// std::map<int64_t,int64_t> map4;
		// probeMinTotalDropBytesMonitorMap.push_back(map1);
		// probeMinMaxBufferUsedMonitorMap.push_back(map2);
		// probeMinMinBufferUsedMonitorMap.push_back(map3);
		// probeMinDurationZeroQueueMonitorMap.push_back(map4);

		// std::map<uint32_t, int64_t> map6;
		// idealMinBufferUsedData.push_back(map6);

		// std::map<uint32_t, double> m;
		// m.insert( std::pair<uint32_t, double>(0,0) );
		// throughputData.push_back(m);
		// std::map<uint32_t, double> m2;
		// m2.insert( std::pair<uint32_t, double>(0,0) );
		// droprateData.push_back(m2);
		// std::map<uint32_t, double> m3;
		// packetratezeroqueueData.push_back(m3);
		// std::map<uint32_t, uint32_t> m4;
		// packetcountzeroqueueData.push_back(m4);
		// std::map<uint32_t, uint32_t> m5;
		// packetcounttotalData.push_back(m5);
		// std::map<uint32_t, double> m6;
		// dropDurationZeroQueueData.push_back(m6);
		// std::map<uint32_t, double> m7;
		// longDurationZeroQueueData.push_back(m7);
	}

	dre.seed(randomseed);

	// Simulator::Schedule(NanoSeconds(1),&SharedMemoryBuffer::setUpSchedule,this);

	// averageQSize = 0;
	// averageQSizeCounter = 0;
	// averageQSizeSum = 0;
	// averageEffectiveCMSA = 0;
	// averageEffectiveCMSASum = 0;
	// mainRoomBeliefScaling = defaultMainRoomBeliefScaling;
	// Simulator::Schedule(MilliSeconds(averageQSizeGranularityMS), &SharedMemoryBuffer::updateAverageQSize,this);
	// Simulator::Schedule(MilliSeconds(averageQSizeGranularityMS), &SharedMemoryBuffer::beliefScalingPeriodic, this);
	// updateQSizeQueue();

	// if (debug) {
	// 	for (uint64_t i=0; i<getTotalProbers(); i++) {
	// 		debugAverageThroughput.push_back(0);
	// 		debugTotalDropBytes.push_back(0);
	// 		debugMaxBufferUsed.push_back(0);
	// 		debugMinBufferUsed.push_back(TotalBuffer);
	// 	}
	// 	Simulator::Schedule(NanoSeconds(debugStatsMs*1000000),&SharedMemoryBuffer::collectDebugStats,this);
	// }
}

// bool SharedMemoryBuffer::isKeyInThroughputData(uint32_t proberId, uint32_t bufferSize) { 
// 	return throughputData[proberId].count(bufferSize) == 1;
// }

// void SharedMemoryBuffer::setThroughputData(uint32_t proberId, uint32_t bufferSize, double value) {
// 	if (verbose) std::cout << Simulator::Now() << ": logging throughputData, proberId=" << proberId << ", bufferSize=" << bufferSize << ", throughput=" << value << std::endl; 
// 	throughputData[proberId].insert( std::pair<uint32_t, double>(bufferSize, value) );
// }

// void SharedMemoryBuffer::setDroprateData(uint32_t proberId, uint32_t bufferSize, double value) {
// 	if (verbose) std::cout << Simulator::Now() << ": logging droprateData, proberId=" << proberId << ", bufferSize=" << bufferSize << ", droprate=" << value << std::endl; 
// 	droprateData[proberId].insert( std::pair<uint32_t, double>(bufferSize, value) );
// }

// void SharedMemoryBuffer::setPacketRateZeroQueueData(uint32_t proberId, uint32_t bufferSize, double value) {
// 	if (verbose) std::cout << Simulator::Now() << ": logging packetRateZeroQueueData, proberId=" << proberId << ", bufferSize=" << bufferSize << ", packetratezeroqueue=" << value << std::endl; 
// 	packetratezeroqueueData[proberId].insert( std::pair<uint32_t, double>(bufferSize, value) );
// }

// void SharedMemoryBuffer::setPacketCountZeroQueueData(uint32_t proberId, uint32_t bufferSize, uint32_t value) {
// 	if (verbose) std::cout << Simulator::Now() << ": logging packetCountZeroQueueData, proberId=" << proberId << ", bufferSize=" << bufferSize << ", packetcountzeroqueue=" << value << std::endl; 
// 	packetcountzeroqueueData[proberId].insert( std::pair<uint32_t, uint32_t>(bufferSize, value) );
// }

// void SharedMemoryBuffer::setPacketCountTotalData(uint32_t proberId, uint32_t bufferSize, uint32_t value) {
// 	if (verbose) std::cout << Simulator::Now() << ": logging packetCountTotalData, proberId=" << proberId << ", bufferSize=" << bufferSize << ", packetcounttotal=" << value << std::endl; 
// 	packetcounttotalData[proberId].insert( std::pair<uint32_t, uint32_t>(bufferSize, value) );
// }

// void SharedMemoryBuffer::setDropDurationZeroQueueData(uint32_t proberId, uint32_t bufferSize, double value) {
// 	if (verbose) std::cout << Simulator::Now() << ": logging dropDurationZeroQueueData, proberId=" << proberId << ", bufferSize=" << bufferSize << ", dropDurationZeroQueue=" << value; 
// 	std::map<uint32_t, double>::iterator it = dropDurationZeroQueueData[proberId].find(bufferSize);
// 	if (it != dropDurationZeroQueueData[proberId].end()) {
// 		double prevValue = -it->second;
// 		it->second = -std::max(prevValue,value);
// 		if (verbose) std::cout << ", currValue=" << it->second << std::endl;
// 	} else {
// 		dropDurationZeroQueueData[proberId].insert( std::pair<uint32_t, double>(bufferSize, -value) );
// 		if (verbose) std::cout << ", currValue=" << -value << std::endl;
// 	}
// }

// void SharedMemoryBuffer::setLongDurationZeroQueueData(uint32_t proberId, uint32_t bufferSize, double value) {
// 	if (verbose) std::cout << Simulator::Now() << ": logging longDurationZeroQueueData, proberId=" << proberId << ", bufferSize=" << bufferSize << ", longDurationZeroQueue=" << value; 
// 	std::map<uint32_t, double>::iterator it = longDurationZeroQueueData[proberId].find(bufferSize);
// 	if (it != longDurationZeroQueueData[proberId].end()) {
// 		double prevValue = -it->second;
// 		it->second = -std::max(prevValue,value);
// 		if (verbose) std::cout << ", currValue=" << it->second << std::endl;
// 	} else {
// 		longDurationZeroQueueData[proberId].insert( std::pair<uint32_t, double>(bufferSize, -value) );
// 		if (verbose) std::cout << ", currValue=" << -value << std::endl;
// 	}
// }

// void SharedMemoryBuffer::updateIdealMinBufferUsedData(uint32_t proberId, uint32_t minBufferUsed, int64_t timestamp, uint32_t currMaxSizeAllowed) {
// 	// only call updateIdealMinBufferUsedData if minBufferUsed <= 1500
// 	if (verbose) std::cout << Simulator::Now() << ": updateIdealMinBufferUsedData, proberId=" << proberId << ", minBufferUsed=" << minBufferUsed << ", timestamp=" << timestamp << ", currMaxSizeAllowed=" << currMaxSizeAllowed << std::endl;
// 	std::map<uint32_t,int64_t> myMap = idealMinBufferUsedData[proberId];
// 	if (minBufferUsed == absoluteMinBuffer) {
// 		if (myMap.find(currMaxSizeAllowed) != myMap.end()) {
// 			// pass -- respsect the earlier timestamp
// 		} else {
// 			myMap.insert( std::pair<uint32_t,int64_t>(currMaxSizeAllowed,timestamp) );
// 		}
// 	} else {
// 		if (minBufferUsed != 0) std::cout << "ERROR: minBufferUsed=" << minBufferUsed << ", but it should be 0" << std::endl;
// 		std::vector<uint32_t> eraseSet;
// 		std::map<uint32_t,int64_t>::iterator it;
// 		for (it=myMap.begin(); it!=myMap.end(); it++) {
// 			if (it->first <= currMaxSizeAllowed) eraseSet.push_back(it->first);
// 		}
// 		for (uint32_t i=0; i<eraseSet.size(); i++) {
// 			myMap.erase(eraseSet[i]);
// 		}
// 	}
// }

bool SharedMemoryBuffer::isProberInHeadRoom(uint32_t proberId) {
	if (proberInHeadRoom.find(proberId) == proberInHeadRoom.end()) return false;
	if (proberInHeadRoom.at(proberId) > 0) return true;
	return false;
}

void SharedMemoryBuffer::addToProberInHeadRoom(uint32_t proberId) { 
	if (proberInHeadRoom.find(proberId) == proberInHeadRoom.end()) {
		proberInHeadRoom.insert( std::pair<uint32_t,uint32_t>(proberId,1) );
	} else {
		proberInHeadRoom[proberId] = proberInHeadRoom.at(proberId)+1;
	}
}

void SharedMemoryBuffer::removeFromProberInHeadRoom(uint32_t proberId) { 
	if (proberInHeadRoom.find(proberId) != proberInHeadRoom.end()) {
		proberInHeadRoom[proberId] = std::min(0, (int32_t)proberInHeadRoom.at(proberId)-1);
	}
}

bool SharedMemoryBuffer::isProberInWaitRoom(uint32_t proberId) {
	if (proberInWaitRoom.find(proberId) != proberInWaitRoom.end()) return false;
	if (proberInWaitRoom.at(proberId) > 0) return true;
	return false;
}

void SharedMemoryBuffer::addToProberInWaitRoom(uint32_t proberId) { 
	if (proberInWaitRoom.find(proberId) == proberInWaitRoom.end()) {
		proberInWaitRoom.insert( std::pair<uint32_t,uint32_t>(proberId,1) );
	} else {
		proberInWaitRoom[proberId] = proberInWaitRoom.at(proberId)+1;
	}
}

void SharedMemoryBuffer::removeFromProberInWaitRoom(uint32_t proberId) { 
	if (proberInWaitRoom.find(proberId) != proberInWaitRoom.end()) {
		proberInWaitRoom[proberId] = std::min(0, (int32_t)proberInWaitRoom.at(proberId)-1);
	}
}

void SharedMemoryBuffer::checkChangeInCurrMaxSizeAllowed(uint32_t proberId, uint32_t prevCMSA, uint32_t thisCMSA) {
	if (prevCMSA == thisCMSA) return;
	int64_t now = Simulator::Now().GetMicroSeconds();
	if (verbose) std::cout << Simulator::Now() << ": checkChangeInCMSA, proberId=" << proberId << ", prev=" << prevCMSA << ", this=" << thisCMSA << ", now=" << now << std::endl;
	currMaxSizeAllowedLastChanged[proberId] = now;
	probeMinAverageThroughput[proberId] = 0;
	probeMinTotalDropBytes[proberId] = 0;
	probeMinMaxBufferUsed[proberId] = 0;
	probeMinMinBufferUsed[proberId] = 1000000000;
	probeMinPacketCountZeroQueue[proberId] = 0;
	probeMinPacketCountTotal[proberId] = 0;
	probeMinDurationZeroQueue[proberId] = 0;

	// std::map<int64_t,uint32_t>::iterator it1;
	// for (it1=probeMinTotalDropBytesMonitorMap[proberId].begin(); it1!=probeMinTotalDropBytesMonitorMap[proberId].end(); it1++) {
	// 	it1->second = 0;
	// }

	// for (it1=probeMinMaxBufferUsedMonitorMap[proberId].begin(); it1!=probeMinMaxBufferUsedMonitorMap[proberId].end(); it1++) {
	// 	it1->second = 0;
	// }

	// for (it1=probeMinMinBufferUsedMonitorMap[proberId].begin(); it1!=probeMinMinBufferUsedMonitorMap[proberId].end(); it1++) {
	// 	it1->second = 0;
	// }

	// std::map<int64_t,int64_t>::iterator it2;
	// for (it2=probeMinDurationZeroQueueMonitorMap[proberId].begin(); it2!=probeMinDurationZeroQueueMonitorMap[proberId].end(); it2++) {
	// 	it2->second = 0;
	// }
}

// void SharedMemoryBuffer::allocateHeadRoomPartitioningDeprecated(uint32_t totalHeadRoom) {
// 	uint32_t numProbersInHeadRoom = proberInHeadRoom.size();
// 	if (verbose) std::cout << Simulator::Now() << ": allocateHeadRoomPartitioning, totalHeadRoom=" << totalHeadRoom << ", numProbersInHeadRoom=" << numProbersInHeadRoom << std::endl;
// 	std::set<uint32_t>::iterator it;
// 	if (verbose) std::cout << "currMaxSizeAllowed: ";
// 	for (it=proberInHeadRoom.begin(); it!=proberInHeadRoom.end(); ++it) {
// 		uint32_t i = *it;
// 		if (verbose) std::cout << i << ":" << currMaxSizeAllowed[i] << "->";
// 		currMaxSizeAllowed[i] = totalHeadRoom/numProbersInHeadRoom;
// 		if (verbose) std::cout << currMaxSizeAllowed[i] << ", ";
// 	}
// 	if (verbose) std::cout << std::endl;
// }

// void SharedMemoryBuffer::allocateHeadRoomPartitioning(uint32_t totalHeadRoom) {
// 	if (verbose) std::cout << Simulator::Now() << ": allocateHeadRoomPartitioning, totalHeadRoom=" << totalHeadRoom;
// 	std::map<uint32_t, uint32_t> numQueuesPerPort;
// 	std::set<uint32_t>::iterator it;
// 	for (it=proberInHeadRoom.begin(); it!=proberInHeadRoom.end(); ++it) {
// 		uint32_t proberid = *it;
// 		uint32_t portid = getPortId(proberid);
// 		// if (currMaxSizeAllowed[proberid] > 0) {
// 		// if (minBufferThreshold[proberid] >= 1) {
// 		// 	if (numQueuesPerPort.find(portid) != numQueuesPerPort.end()) {
// 		// 		uint32_t prevNumQueues = numQueuesPerPort.find(portid)->second;
// 		// 		numQueuesPerPort.find(portid)->second = prevNumQueues+1;
// 		// 	} else {
// 		// 		numQueuesPerPort.insert( std::pair<uint32_t, uint32_t>(portid, 1) );
// 		// 	}
// 		// }
// 		if (not isProberStaticInHeadRoom(proberid)) {
// 			if (numQueuesPerPort.find(portid) != numQueuesPerPort.end()) {
// 				uint32_t prevNumQueues = numQueuesPerPort.find(portid)->second;
// 				numQueuesPerPort.find(portid)->second = prevNumQueues+1;
// 			} else {
// 				numQueuesPerPort.insert( std::pair<uint32_t, uint32_t>(portid, 1) );
// 			}
// 		}
// 	}
// 	if (numQueuesPerPort.size() == 0) {
// 		if (verbose) std::cout << ", no prober to be allocated" << std::endl;
// 		return;
// 	}
// 	uint32_t headRoomPerPort = totalHeadRoom / numQueuesPerPort.size();
// 	if (verbose) std::cout << ", headRoomPerPort=" << headRoomPerPort << ", numPortsInvolved=" << numQueuesPerPort.size() << std::endl;

// 	if (verbose) std::cout << "currMaxSizeAllowed: ";
// 	for (it=proberInHeadRoom.begin(); it!=proberInHeadRoom.end(); ++it) {
// 		uint32_t proberid = *it;
// 		uint32_t portid = getPortId(proberid);
// 		if (verbose) std::cout << proberid << "(" << portid << "):" << currMaxSizeAllowed[proberid] << "->";
// 		// if (currMaxSizeAllowed[proberid] > 0) currMaxSizeAllowed[proberid] = headRoomPerPort / numQueuesPerPort[portid];
// 		// if (minBufferThreshold[proberid] >= 1) currMaxSizeAllowed[proberid] = headRoomPerPort / numQueuesPerPort[portid];
// 		if (not isProberStaticInHeadRoom(proberid)) currMaxSizeAllowed[proberid] = ((((headRoomPerPort / numQueuesPerPort[portid])-1)/1500)+1)*1500;
// 		if (verbose) std::cout << currMaxSizeAllowed[proberid] << ", ";
// 	}
// 	if (verbose) std::cout << std::endl;
// }

// void SharedMemoryBuffer::adjustHeadRoomForFlowIdEnded(uint32_t proberid) {
// 	if (verbose) std::cout << Simulator::Now() << ": adjustHeadRoomForFlowIdEnded, proberid=" << proberid;
// 	currMaxSizeAllowed[proberid] = 0;
// 	minBufferThreshold[proberid] = 0;
// 	uint32_t mainRoomTotalQSize = 0;
// 	uint32_t flowEndTotalQSize = 0;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		// if ((not isProberInHeadRoom(i)) and minBufferThreshold[i]>=1) {
// 		// 	if (i == proberid) std::cout << "ERROR: adjustHeadRoomForFlowIdEnded, proberid=" << proberid << " should still be in HeadRoom at this point" << std::endl;
// 		// 	mainRoomTotalQSize += qSize[i];
// 		// }
// 		// if (isProberInHeadRoom(i) and minBufferThreshold[i] == 0) flowEndTotalQSize += qSize[i];
// 		if (isProberActiveInMainRoom(i)) {
// 			mainRoomTotalQSize += qSize[i];
// 		} 
// 		if (isProberActiveInHeadRoom(i) and isProberStaticInHeadRoom(i)) {
// 			flowEndTotalQSize += qSize[i];
// 		}
// 	}
// 	uint32_t headRoomBelief = TotalBuffer - mainRoomTotalQSize - flowEndTotalQSize;
// 	if (verbose) std::cout << ", headRoomBelief=" << headRoomBelief << ", mainRoomTotalQsize=" << mainRoomTotalQSize << ", flowEndTotalQSize=" << flowEndTotalQSize << std::endl;
// 	allocateHeadRoomPartitioning(headRoomBelief);
// }

// uint32_t SharedMemoryBuffer::countOccurrencesInVector(std::vector<uint32_t> vec, uint32_t target) {
// 	uint32_t count = 0;
// 	for (int i=0; i<vec.size(); i++) {
// 		if (vec.at(i) == target) count++;
// 	}
// 	return count;
// }

// double SharedMemoryBuffer::intrapolateDeltaY(uint32_t proberid, uint32_t x1, uint32_t x2) {
// 	// std::map<uint32_t, double> data = throughputData[proberid];
// 	std::map<uint32_t, double> data;
// 	if (doMonitorDrop[proberid]) {
// 		data = dropDurationZeroQueueData[proberid];
// 	} else {
// 		data = longDurationZeroQueueData[proberid];
// 	}
// 	std::map<uint32_t, double>::iterator it;

// 	uint32_t globalMinX = 100000000;
// 	uint32_t globalMaxX = 0;
// 	for (it=data.begin(); it!=data.end(); it++) {
// 		if (it->first > globalMaxX) {
// 			globalMaxX = it->first;
// 		}
// 		if (it->first < globalMinX) {
// 			globalMinX = it->first;
// 		}
// 	}

// 	uint32_t currMaxSmallerEqualX1 = 0;
// 	double corrY1 = 0;
// 	uint32_t currMinLargerEqualX2 = 100000000;
// 	double corrY2 = 0;

// 	if (x2 <= globalMinX) {
// 		currMaxSmallerEqualX1 = globalMinX;
// 		for (it=data.begin(); it!=data.end(); it++) {
// 			if (it->first == globalMinX) corrY1 = it->second;

// 			if (it->first > globalMinX) {
// 				if (it->first < currMinLargerEqualX2) {
// 					currMinLargerEqualX2 = it->first;
// 					corrY2 = it->second;
// 				}
// 			}
			
// 		}

// 	} else if (globalMaxX <= x1) {
// 		currMinLargerEqualX2 = globalMaxX;
// 		for (it=data.begin(); it!=data.end(); it++) {
// 			if (it->first == globalMaxX) corrY2 = it->second;

// 			if (it->first < globalMaxX) {
// 				if (it->first > currMaxSmallerEqualX1) {
// 					currMaxSmallerEqualX1 = it->first;
// 					corrY1 = it->second;
// 				}
// 			}
			
// 		}

// 	} else {
// 		for (it=data.begin(); it!=data.end(); it++) {
// 			if (it->first <= x1) {
// 				if (it->first > currMaxSmallerEqualX1) { // equal should not happen here
// 					currMaxSmallerEqualX1 = it->first;
// 					corrY1 = it->second;
// 				}
// 			}
// 		}
// 		if (currMaxSmallerEqualX1 == 0) {
// 			currMaxSmallerEqualX1 = 100000000; // looking for currMinLargerEqualX1
// 			for (it=data.begin(); it!=data.end(); it++) {
// 				if (it->first >= x1) {
// 					if (it->first < currMaxSmallerEqualX1) { // equal should not happen here
// 						currMaxSmallerEqualX1 = it->first;
// 						corrY1 = it->second;
// 					}
// 				}
// 			}
// 		}
		
// 		for (it=data.begin(); it!=data.end(); it++) {
// 			if (it->first >= x2) {
// 				if (it->first < currMinLargerEqualX2) {
// 					currMinLargerEqualX2 = it->first;
// 					corrY2 = it->second;
// 				}
// 			}
// 		}
// 		if (currMinLargerEqualX2 == 100000000) {
// 			currMinLargerEqualX2 = 0; // looking for currMaxSmallerEqualX2
// 			for (it=data.begin(); it!=data.end(); it++) {
// 				if (it->first <= x2) {
// 					if (it->first > currMinLargerEqualX2) {
// 						currMinLargerEqualX2 = it->first;
// 						corrY2 = it->second;
// 					}
// 				}
// 			}
// 		}

// 	}

// 	if (currMaxSmallerEqualX1==0 or currMaxSmallerEqualX1==100000000 or currMinLargerEqualX2==0 or currMinLargerEqualX2==100000000) {
// 		std::cout << "ERROR: currMaxSmallerEqualX1=" << currMaxSmallerEqualX1 << ", currMinLargerEqualX2=" << currMinLargerEqualX2 << std::endl;
// 	}

// 	double m = (corrY2-corrY1)/(currMinLargerEqualX2-currMaxSmallerEqualX1);
// 	double b = corrY1-m*currMaxSmallerEqualX1;

// 	double y1 = m*x1+b;
// 	double y2 = m*x2+b;

// 	if (verbose) {
// 		std::cout << "intrapolateDeltaY, proberid=" << proberid << ", x1=" << x1 << ", x2=" << x2;
// 		std::cout << ", currMaxSmallerEqualX1=" << currMaxSmallerEqualX1 << ", corrY1=" << corrY1 << ", currMinLargerEqualX2=" << currMinLargerEqualX2 << ", corrY2=" << corrY2;
// 		std::cout << ", m=" << m << ", b=" << b << ", y1=" << y1 << ", y2=" << y2;
// 		std::cout << std::endl;
// 	}

// 	return y2-y1;
// }

// bool SharedMemoryBuffer::ifSetContains(std::set<uint32_t> set, uint32_t target) {
// 	return std::find(set.begin(), set.end(), target) != set.end();
// }

// void SharedMemoryBuffer::allocateMainRoomUtility(uint32_t demand, uint32_t supply, uint32_t thisProberId, int32_t marginalRequest) {
// 	std::set<uint32_t>::iterator it;
// 	uint32_t delta = demand - supply; // delta >= 0
// 	if (verbose) std::cout << Simulator::Now() << ": allocateMainRoomUtility, demand=" << demand << ", supply=" << supply << ", delta=" << delta << std::endl;
// 	if (delta == 0) return;

// 	std::vector<uint32_t> potentialCurrMaxSizeAllowed;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		// if (not isProberInHeadRoom(i)) {
// 		// 	if (minBufferThreshold[i]>1) {
// 		// 		potentialCurrMaxSizeAllowed.push_back(minBufferThreshold[i]);
// 		// 	} else if (minBufferThreshold[i]==1) {
// 		// 		potentialCurrMaxSizeAllowed.push_back(currMaxSizeAllowed[i]);
// 		// 	} else { // minBufferThreshold[i]==0
// 		// 		potentialCurrMaxSizeAllowed.push_back(0);
// 		// 	}
// 		// } else {
// 		// 	potentialCurrMaxSizeAllowed.push_back(0);
// 		// }
// 		if (isProberActiveInMainRoom(i)) {
// 			if (not isProberStaticInMainRoom(i)) {
// 				if (minBufferThreshold[i]>1) {
// 					potentialCurrMaxSizeAllowed.push_back(minBufferThreshold[i]);
// 				} else {
// 					potentialCurrMaxSizeAllowed.push_back(currMaxSizeAllowed[i]);
// 				} 
// 			} else {
// 				potentialCurrMaxSizeAllowed.push_back(0);
// 			}
// 		} else {
// 			potentialCurrMaxSizeAllowed.push_back(0);
// 		}
// 	}

// 	if (verbose) {
// 		std::cout << "potentialCurrMaxSizeAllowed: ";
// 		for (uint32_t i=0; i<getTotalProbers(); i++) {
// 			std::cout << i << "->" << potentialCurrMaxSizeAllowed[i] << ",";
// 		}
// 		std::cout << std::endl;
// 	}

// 	std::set<uint32_t> probersLessThanTwoData;
// 	std::set<uint32_t> probersMoreThanOneData;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		// if (not isProberInHeadRoom(i) and minBufferThreshold[i]>0) {
// 		// 	// if (throughputData[i].size() < 2) {
// 		// 	if (doMonitorDrop[i]) {
// 		// 		if (dropDurationZeroQueueData[i].size() < 2) {
// 		// 			probersLessThanTwoData.insert(i);
// 		// 		} else {
// 		// 			probersMoreThanOneData.insert(i);
// 		// 		}
// 		// 	} else {
// 		// 		if (longDurationZeroQueueData[i].size() < 2) {
// 		// 			probersLessThanTwoData.insert(i);
// 		// 		} else {
// 		// 			probersMoreThanOneData.insert(i);
// 		// 		}
// 		// 	}
// 		// }
// 		if (isProberActiveInMainRoom(i) and (not isProberStaticInMainRoom(i))) {
// 			if (doMonitorDrop[i]) {
// 				if (dropDurationZeroQueueData[i].size() < 2) {
// 					probersLessThanTwoData.insert(i);
// 				} else {
// 					probersMoreThanOneData.insert(i);
// 				}
// 			} else {
// 				if (longDurationZeroQueueData[i].size() < 2) {
// 					probersLessThanTwoData.insert(i);
// 				} else {
// 					probersMoreThanOneData.insert(i);
// 				}
// 			}
// 		}
// 	}

// 	if (verbose) {
// 		std::cout << "probersLessThanTwoData: ";
// 		for (it=probersLessThanTwoData.begin(); it!=probersLessThanTwoData.end(); it++) {
// 			std::cout << *it << ",";
// 		}
// 		std::cout << std::endl;
// 		std::cout << "probersMoreThanOneData: ";
// 		for (it=probersMoreThanOneData.begin(); it!=probersMoreThanOneData.end(); it++) {
// 			std::cout << *it << ",";
// 		}
// 		std::cout << std::endl;
// 	}

// 	// AnnC: may have to use equality by amount of buffer (instead of number of probers)
// 	uint32_t bufferOccupiedByProbersLessThanTwoData = 0;
// 	for (it=probersLessThanTwoData.begin(); it!=probersLessThanTwoData.end(); it++) {
// 		bufferOccupiedByProbersLessThanTwoData += currMaxSizeAllowed[*it]-absoluteMinBuffer;
// 	}
// 	uint32_t bufferOccupiedByProbersMoreThanOneData = 0;
// 	for (it=probersMoreThanOneData.begin(); it!=probersMoreThanOneData.end(); it++) {
// 		bufferOccupiedByProbersMoreThanOneData += currMaxSizeAllowed[*it]-absoluteMinBuffer;
// 	}
// 	if (ifSetContains(probersLessThanTwoData, thisProberId)) {
// 		bufferOccupiedByProbersLessThanTwoData += marginalRequest;
// 	} else if (ifSetContains(probersMoreThanOneData, thisProberId)) {
// 		bufferOccupiedByProbersMoreThanOneData += marginalRequest;
// 	}

// 	uint32_t bufferUnitAway = ((delta-1)/1500)+1;
// 	uint32_t bufferUnitAwayFromProbersLessThanTwoData = round(bufferUnitAway * ((double)bufferOccupiedByProbersLessThanTwoData/(bufferOccupiedByProbersMoreThanOneData+bufferOccupiedByProbersLessThanTwoData)));
// 	uint32_t bufferUnitAwayFromProbersMoreThanOneData = bufferUnitAway - bufferUnitAwayFromProbersLessThanTwoData;
// 	if (verbose) std::cout << "bufferUnitAway=" << bufferUnitAway << ", FromProbersLessThanTwoData=" << bufferUnitAwayFromProbersLessThanTwoData << ", FromProbersMoreThanOneData=" <<bufferUnitAwayFromProbersMoreThanOneData << std::endl;

// 	std::vector<uint32_t> probersToBackoff;
// 	while (probersToBackoff.size() < bufferUnitAwayFromProbersMoreThanOneData) {
// 		double currMinUtilityDecrease = 100000000;
// 		uint32_t currMinUtilityDecreaseProber = 100000000; // AnnC: when tie, always use the one probersMoreThanOneData iterator gives first
// 		for (it=probersMoreThanOneData.begin(); it!=probersMoreThanOneData.end(); it++) {
// 			uint32_t myMinBufferThreshold = minBufferThreshold[*it];
// 			uint32_t currBuffer = 0;
// 			if (myMinBufferThreshold > 1) {
// 				currBuffer = myMinBufferThreshold - countOccurrencesInVector(probersToBackoff,*it)*1500;
// 			} else {
// 				currBuffer = currMaxSizeAllowed[*it] - countOccurrencesInVector(probersToBackoff,*it)*1500;
// 			}
// 			if (thisProberId == *it) currBuffer += marginalRequest;
// 			uint32_t nextBuffer = currBuffer - 1500;
// 			double utilityDecrease = intrapolateDeltaY(*it,nextBuffer,currBuffer);
// 			if (verbose) std::cout << "utilityDecrease=" << utilityDecrease << ", currMinUtilityDecrease=" << currMinUtilityDecrease << ", currMinUtilityDecreaseProber=" << currMinUtilityDecreaseProber << std::endl;
// 			if (utilityDecrease < currMinUtilityDecrease) {
// 				currMinUtilityDecrease = utilityDecrease;
// 				currMinUtilityDecreaseProber = *it;
// 			}
// 		}
// 		if (verbose) std::cout << "currMinUtilityDecrease=" << currMinUtilityDecrease << ", currMinUtilityDecreaseProber=" << currMinUtilityDecreaseProber << std::endl;
// 		if (currMinUtilityDecreaseProber < 100000000) {
// 			if (potentialCurrMaxSizeAllowed[currMinUtilityDecreaseProber] < absoluteMinBuffer + 1500) {
// 				if (verbose) std::cout << "erase, currMinUtilityDecreaseProber=" << currMinUtilityDecreaseProber << ", potentialCurrMaxSizeAllowed=" << potentialCurrMaxSizeAllowed[currMinUtilityDecreaseProber] << std::endl;
// 				probersMoreThanOneData.erase(currMinUtilityDecreaseProber);
// 			} else {
// 				if (verbose) std::cout << "push_back, currMinUtilityDecreaseProber=" << currMinUtilityDecreaseProber << ", potentialCurrMaxSizeAllowed=" << potentialCurrMaxSizeAllowed[currMinUtilityDecreaseProber] << std::endl;
// 				probersToBackoff.push_back(currMinUtilityDecreaseProber);
// 				potentialCurrMaxSizeAllowed[currMinUtilityDecreaseProber] -= 1500;
// 			}
// 		} else {
// 			if (verbose) std::cout << "currMinUtilityDecreaseProber == 100000000" << std::endl;
// 			break;
// 		}
// 	}

// 	if (bufferUnitAwayFromProbersLessThanTwoData > 0) {
// 		// AnnC: may have to use equality by amount of buffer
// 		for (it=probersLessThanTwoData.begin(); it!=probersLessThanTwoData.end(); it++) {
// 			uint32_t thisBufferOccupied = currMaxSizeAllowed[*it] - absoluteMinBuffer; // >=0
// 			if (*it == thisProberId) {
// 				thisBufferOccupied += marginalRequest;
// 			}
// 			uint32_t thisBufferUnit = round(bufferUnitAwayFromProbersLessThanTwoData*((double)thisBufferOccupied/bufferOccupiedByProbersLessThanTwoData));
// 			uint32_t ii = 0;
// 			while (ii < thisBufferUnit) {
// 				probersToBackoff.push_back(*it);
// 				potentialCurrMaxSizeAllowed[*it] -= 1500;
// 				ii++;
// 			}
// 		}

// 		std::vector<uint32_t> shuffledProbersLessThanTwoData;
// 		for (it=probersLessThanTwoData.begin(); it!=probersLessThanTwoData.end(); it++) {
// 			shuffledProbersLessThanTwoData.push_back(*it);
// 		}
// 		shuffle(shuffledProbersLessThanTwoData.begin(), shuffledProbersLessThanTwoData.end(), dre);
// 		for (uint32_t i=0; i<shuffledProbersLessThanTwoData.size(); i++) {
// 			uint32_t prober = shuffledProbersLessThanTwoData[i];
// 			if (potentialCurrMaxSizeAllowed[prober] < absoluteMinBuffer+1500) continue;
// 			if (probersToBackoff.size() < bufferUnitAway) {
// 				probersToBackoff.push_back(prober);
// 			} else {
// 				break;
// 			}
// 		}
// 	}

// 	if (verbose) {
// 		std::cout << "probersToBackOff: ";
// 		for (int i=0; i<probersToBackoff.size(); i++) {
// 			std::cout << probersToBackoff.at(i) << ",";
// 		}
// 		std::cout << std::endl;
// 	}

// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		uint32_t myMinBufferThreshold = minBufferThreshold[i];
// 		// if (not isProberInHeadRoom(i) and myMinBufferThreshold>0) {
// 		// 	uint32_t prevCMSA = currMaxSizeAllowed[i];
// 		// 	if (myMinBufferThreshold > 1) {
// 		// 		currMaxSizeAllowed[i] = minBufferThreshold[i] - countOccurrencesInVector(probersToBackoff,i)*1500;
// 		// 	} else {
// 		// 		currMaxSizeAllowed[i] = prevCMSA - countOccurrencesInVector(probersToBackoff,i)*1500;
// 		// 	}
// 		// 	if (i == thisProberId) {
// 		// 		currMaxSizeAllowed[i] += marginalRequest;
// 		// 	}
// 		// 	checkChangeInCurrMaxSizeAllowed(i,prevCMSA,currMaxSizeAllowed[i]);
// 		// }
// 		if (isProberActiveInMainRoom(i) and (not isProberStaticInMainRoom(i))) {
// 			uint32_t prevCMSA = currMaxSizeAllowed[i];
// 			if (myMinBufferThreshold > 1) {
// 				currMaxSizeAllowed[i] = minBufferThreshold[i] - countOccurrencesInVector(probersToBackoff,i)*1500;
// 			} else {
// 				currMaxSizeAllowed[i] = prevCMSA - countOccurrencesInVector(probersToBackoff,i)*1500;
// 			}
// 			if (i == thisProberId) {
// 				currMaxSizeAllowed[i] += marginalRequest;
// 			}
// 			if (currMaxSizeAllowed[i] > TotalBuffer-burstReserve) currMaxSizeAllowed[i] = TotalBuffer - burstReserve;
// 			checkChangeInCurrMaxSizeAllowed(i,prevCMSA,currMaxSizeAllowed[i]);
// 		}
// 	}
// }

// void SharedMemoryBuffer::allocateMainRoomSpace(uint32_t demand, uint32_t supply, uint32_t thisProberId, int32_t marginalRequest) {
// 	std::set<uint32_t>::iterator it;
// 	uint32_t delta = demand - supply; // delta >= 0
// 	if (verbose) std::cout << Simulator::Now() << ": allocateMainRoomSpace, demand=" << demand << ", supply=" << supply << ", delta=" << delta << std::endl;
// 	if (delta == 0) return;

// 	uint32_t totalExtraBuffer = 0;
// 	std::vector<uint32_t> extraBufferPerMainRoomProber;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		extraBufferPerMainRoomProber.push_back(0);
// 		if (isProberActiveInMainRoom(i) and not isProberStaticInMainRoom(i)) {
// 			if (i == thisProberId) {
// 				extraBufferPerMainRoomProber[i] = currMaxSizeAllowed[i]-absoluteMinBuffer + marginalRequest;
// 			} else {
// 				extraBufferPerMainRoomProber[i] = currMaxSizeAllowed[i]-absoluteMinBuffer;
// 			}
// 			totalExtraBuffer += extraBufferPerMainRoomProber[i];
// 		}
// 	}

// 	uint32_t totalBufferUnitAway = ((delta-1)/1500)+1;
// 	if (verbose) std::cout << Simulator::Now() << ": totalExtraBuffer=" << totalExtraBuffer << ", totalBufferUnitAway=" << totalBufferUnitAway << std::endl;
// 	std::vector<uint32_t> bufferUnitAwayPerMainRoomProber;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		bufferUnitAwayPerMainRoomProber.push_back(0);
// 		if (isProberActiveInMainRoom(i) and not isProberStaticInMainRoom(i)) {
// 			bufferUnitAwayPerMainRoomProber[i] = round(totalBufferUnitAway*(extraBufferPerMainRoomProber[i]/(double)totalExtraBuffer));
// 		}
// 	}

// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		if (isProberActiveInMainRoom(i) and (not isProberStaticInMainRoom(i))) {
// 			uint32_t prevCMSA = currMaxSizeAllowed[i];
// 			if (i == thisProberId) currMaxSizeAllowed[i] += marginalRequest;
// 			currMaxSizeAllowed[i] -= bufferUnitAwayPerMainRoomProber[i]*1500;
// 			if (currMaxSizeAllowed[i] > TotalBuffer-burstReserve) currMaxSizeAllowed[i] = TotalBuffer - burstReserve;
// 			checkChangeInCurrMaxSizeAllowed(i,prevCMSA,currMaxSizeAllowed[i]);
// 		}
// 	}
// }

void SharedMemoryBuffer::allocateBufferSpaceSimple(uint32_t thisProberId, int32_t marginalRequest) {
	// if (marginalRequest>0) marginalRequest = (((marginalRequest-1)/1500)+1)*1500;
	if (verbose) std::cout << Simulator::Now() << ": allocateBufferSpaceSimple, thisProberId=" << thisProberId << ", marginalRequest=" << marginalRequest;

	uint32_t prevCMSA = currMaxSizeAllowed[thisProberId];
	uint32_t totalbufferused = 0;
	for (uint32_t i=0; i<getTotalProbers(); i++) {
		uint32_t cmsa = currMaxSizeAllowed[i];
		// uint32_t maxqlen = probeMinMaxBufferUsed[i];
		// totalbufferused += std::min(cmsa,maxqlen);
		totalbufferused += cmsa;
	}
	if (totalbufferused+marginalRequest > TotalBuffer) {
		uint32_t maxAllowedMarginalRequest = TotalBuffer-totalbufferused;
		if (marginalRequest>0) marginalRequest = std::max((uint32_t)0,maxAllowedMarginalRequest);
	}
	uint32_t thisCMSA = prevCMSA+marginalRequest;
	if (verbose) std::cout << ", totalbufferused=" << totalbufferused << ", thisCMSA=" << thisCMSA << std::endl;
	
	currMaxSizeAllowed[thisProberId]=thisCMSA; 
	checkChangeInCurrMaxSizeAllowed(thisProberId,prevCMSA,thisCMSA);
}

// void SharedMemoryBuffer::allocateBufferSpace(uint32_t thisProberId, int32_t marginalRequest) {
// 	if (marginalRequest>0) marginalRequest = (((marginalRequest-1)/1500)+1)*1500;
// 	if (verbose) std::cout << Simulator::Now() << ": allocateBufferSpace, thisProberId=" << thisProberId << ", marginalRequest=" << marginalRequest;
// 	uint32_t currMainRoomDemand = 0;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		if (isProberActiveInMainRoom(i)) {
// 			if (not isProberStaticInMainRoom(i)) {
// 				currMainRoomDemand += currMaxSizeAllowed[i];
// 			} else {
// 				currMainRoomDemand += qSize[i];
// 			}
// 		}
// 	}
// 	currMainRoomDemand += marginalRequest;
// 	if (verbose) std::cout << ", currMainRoomDemand=" << currMainRoomDemand << std::endl;

// 	uint32_t mainRoomTotalQSize = 0;
// 	uint32_t mainRoomFlowEndTotalQSize = 0;
// 	uint32_t headRoomFlowEndTotalQSize = 0;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		if (isProberActiveInMainRoom(i)) {
// 			if (not isProberStaticInMainRoom(i)) {
// 				mainRoomTotalQSize += qSize[i];
// 			} else {
// 				mainRoomFlowEndTotalQSize += qSize[i];
// 			}
// 		} 
// 		if (isProberActiveInHeadRoom(i) and not isProberStaticInHeadRoom(i)) {
// 			headRoomFlowEndTotalQSize += qSize[i];
// 		}
// 	}

// 	uint32_t maxChangeAllowed = mainRoomNumQueues*2*3000;
// 	uint32_t maxPossibleMainRoomBelief = (TotalBuffer-burstReserve) * mainRoomNumQueues * 2; // each queue believes that it has the whole buffer to itself
// 	double mainRoomBeliefScaling = getMainRoomBeliefScaling();
// 	uint32_t mainRoomBelief = round((TotalBuffer-burstReserve)*mainRoomBeliefScaling);
// 	mainRoomBelief = (((mainRoomBelief-1)/1500)+1)*1500;
// 	if (verbose) std::cout << Simulator::Now() << ": mainRoomBeliefScaling=" << mainRoomBeliefScaling << ", prevMainRoomBelief=" << prevMainRoomBelief << ", mainRoomBelief(before)=" << mainRoomBelief;
// 	if (prevMainRoomBelief > 0) {
// 		if (mainRoomBelief > prevMainRoomBelief+maxChangeAllowed) mainRoomBelief = prevMainRoomBelief+maxChangeAllowed;
// 		if (mainRoomBelief < prevMainRoomBelief-maxChangeAllowed) mainRoomBelief = prevMainRoomBelief-maxChangeAllowed;
// 	}
// 	if (invadedHeadRoom) {
// 		if (verbose) std::cout << ", invadedHeadRoom=true";
// 		invadedHeadRoom = false;
// 		// if (mainRoomBelief>prevMainRoomBelief) {
// 		// 	if (verbose) std::cout << ", MRBelief cannot larger than prev";
// 		// 	mainRoomBelief = prevMainRoomBelief;
// 		// }
// 	}
// 	if (mainRoomBelief > maxPossibleMainRoomBelief) mainRoomBelief = maxPossibleMainRoomBelief;
// 	prevMainRoomBelief = mainRoomBelief;
// 	if (verbose) std::cout << ", mainRoomBelief(after)=" << mainRoomBelief << std::endl;
// 	uint32_t headRoomBelief = TotalBuffer - mainRoomTotalQSize - mainRoomFlowEndTotalQSize - headRoomFlowEndTotalQSize;
// 	if (verbose) std::cout << Simulator::Now() << ": allocateBufferSpace, mainRoomTotalQSize=" << mainRoomTotalQSize << ", mainRoomFlowEndTotalQSize=" << mainRoomFlowEndTotalQSize << ", headRoomFlowEndTotalQSize=" << headRoomFlowEndTotalQSize << ", mainRoomBeliefScaling=" << mainRoomBeliefScaling << ", mainRoomBelief=" << mainRoomBelief << ", headRoomBelief=" << headRoomBelief << std::endl;

// 	allocateHeadRoomPartitioning(headRoomBelief);
// 	if (currMainRoomDemand <= mainRoomBelief) {
// 		uint32_t prevCMSA = currMaxSizeAllowed[thisProberId];
// 		currMaxSizeAllowed[thisProberId] += marginalRequest; 
// 		if (currMaxSizeAllowed[thisProberId] > TotalBuffer-burstReserve) currMaxSizeAllowed[thisProberId] = TotalBuffer-burstReserve;
// 		checkChangeInCurrMaxSizeAllowed(thisProberId,prevCMSA,currMaxSizeAllowed[thisProberId]);
// 	} else {
// 		allocateMainRoomSpace(currMainRoomDemand, mainRoomBelief, thisProberId, marginalRequest);
// 	}
// }

// void SharedMemoryBuffer::allocateBufferSpace2(uint32_t thisProberId, int32_t marginalRequest) {
// 	if (verbose) std::cout << Simulator::Now() << ": allocateBufferSpace, thisProberId=" << thisProberId << ", marginalRequest=" << marginalRequest;
// 	uint32_t currMainRoomDemand = 0;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		// if (not isProberInHeadRoom(i)) {
// 		// 	// std::cout << "****Debug: proberid=" << i << ", MBT=" << minBufferThreshold[i] << ", CMSA=" << currMaxSizeAllowed[i] << std::endl;
// 		// 	uint32_t myMinBufferThreshold = minBufferThreshold[i];
// 		// 	if (myMinBufferThreshold == 1) {
// 		// 		currMainRoomDemand += currMaxSizeAllowed[i];
// 		// 	} else if (myMinBufferThreshold > 1) {
// 		// 		currMainRoomDemand += myMinBufferThreshold; // Always want to give minBufferThreshold, even if their current CMSA may be smaller
// 		// 	}
// 		// }
// 		if (isProberActiveInMainRoom(i)) {
// 			if (not isProberStaticInMainRoom(i)) {
// 				uint32_t myMinBufferThreshold = minBufferThreshold[i];
// 				if (myMinBufferThreshold > 1) {
// 					currMainRoomDemand += myMinBufferThreshold;
// 					// std::cout << "****Debug: MBT[" << i << "]=" <<  myMinBufferThreshold << std::endl;
// 				} else {
// 					currMainRoomDemand += currMaxSizeAllowed[i];
// 					// std::cout << "****Debug: CMSA[" << i << "]=" <<  currMaxSizeAllowed[i] << std::endl;
// 				}
// 			} else {
// 				currMainRoomDemand += qSize[i];
// 				// std::cout << "****Debug: qSize[" << i << "]=" <<  qSize[i] << std::endl;
// 			}
// 		}
// 	}
// 	currMainRoomDemand += marginalRequest;
// 	if (verbose) std::cout << ", currMainRoomDemand=" << currMainRoomDemand << std::endl;

// 	uint32_t mainRoomTotalCMSA = 0;
// 	uint32_t mainRoomTotalQSize = 0;
// 	uint32_t mainRoomFlowEndTotalQSize = 0;
// 	uint32_t headRoomFlowEndTotalQSize = 0;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		// if ((not isProberInHeadRoom(i)) and minBufferThreshold[i]>=1) {
// 		// 	mainRoomTotalCMSA += currMaxSizeAllowed[i];
// 		// 	mainRoomTotalQSize += qSize[i];
// 		// }
// 		// if (isProberInHeadRoom(i) and minBufferThreshold[i]==0) flowEndTotalQSize += qSize[i];
// 		if (isProberActiveInMainRoom(i)) {
// 			if (not isProberStaticInMainRoom(i)) {
// 				if (effectiveCurrMaxSizeAllowed[i] == 1) {
// 					mainRoomTotalCMSA += currMaxSizeAllowed[i];
// 				} else {
// 					mainRoomTotalCMSA += effectiveCurrMaxSizeAllowed[i];
// 				}
// 				mainRoomTotalQSize += qSize[i];
// 			} else {
// 				mainRoomFlowEndTotalQSize += qSize[i];
// 			}
// 		} 
// 		if (isProberActiveInHeadRoom(i)) {
// 			if (not isProberStaticInHeadRoom(i)) {
// 				headRoomFlowEndTotalQSize += qSize[i];
// 			} 
// 			// else {
// 			// 	currMaxSizeAllowed[i] = 0;
// 			// 	minBufferThreshold[i] = 0;
// 			// }
// 		}
// 	}
// 	// double mainRoomBeliefScaling = 3;
// 	// if (mainRoomTotalQSize > 0) {
// 	// 	mainRoomBeliefScaling = mainRoomTotalCMSA/(double)mainRoomTotalQSize;
// 	// // 	// if (setMainRoomBeliefScalingToDefault)  {
// 	// // 	// 	if (mainRoomBeliefScaling < 2) {
// 	// // 	// 		setMainRoomBeliefScalingToDefault = false;
// 	// // 	// 	} else {
// 	// // 	// 		mainRoomBeliefScaling = 1;
// 	// // 	// 	}
// 	// // 	// }
// 	// } else {
// 	// // 	// if (setMainRoomBeliefScalingToDefault) mainRoomBeliefScaling = 1;
// 	// }
// 	// if (averageQSize > 0) {
// 	// 	mainRoomBeliefScaling = averageEffectiveCMSA/(double)averageQSize;
// 	// }
// 	double mainRoomBeliefScaling = getMainRoomBeliefScaling();
// 	uint32_t mainRoomBelief = round((TotalBuffer-burstReserve)*mainRoomBeliefScaling);
// 	uint32_t headRoomBelief = TotalBuffer - mainRoomTotalQSize - mainRoomFlowEndTotalQSize - headRoomFlowEndTotalQSize;
// 	if (verbose) std::cout << Simulator::Now() << ": allocateBufferSpace, mainRoomTotalCMSA=" << mainRoomTotalCMSA << ", mainRoomTotalQSize=" << mainRoomTotalQSize << ", mainRoomFlowEndTotalQSize=" << mainRoomFlowEndTotalQSize << ", headRoomFlowEndTotalQSize=" << headRoomFlowEndTotalQSize << ", mainRoomBeliefScaling=" << mainRoomBeliefScaling << ", mainRoomBelief=" << mainRoomBelief << ", headRoomBelief=" << headRoomBelief << std::endl;

// 	allocateHeadRoomPartitioning(headRoomBelief);
// 	if (currMainRoomDemand <= mainRoomBelief) {
// 		uint32_t prevCMSA = currMaxSizeAllowed[thisProberId];
// 		currMaxSizeAllowed[thisProberId] += marginalRequest; 
// 		if (currMaxSizeAllowed[thisProberId] > TotalBuffer-burstReserve) currMaxSizeAllowed[thisProberId] = TotalBuffer-burstReserve;
// 		checkChangeInCurrMaxSizeAllowed(thisProberId,prevCMSA,currMaxSizeAllowed[thisProberId]);
// 	} else {
// 		allocateMainRoomUtility(currMainRoomDemand, mainRoomBelief, thisProberId, marginalRequest);
// 	}
// }

// void SharedMemoryBuffer::allocateBufferSpaceDeprecated(uint32_t thisProberId, int32_t marginalRequest) {
// 	if (verbose) std::cout << Simulator::Now() << ": allocateBufferSpace, thisProberId=" << thisProberId << ", marginalRequest=" << marginalRequest << std::endl;
// 	uint32_t mainRoom = 0;
// 	uint32_t headRoom = 0;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		if (not isProberInHeadRoom(i)) {
// 			uint32_t myMinBufferThreshold = minBufferThreshold[i];
// 			if (myMinBufferThreshold == 1) {
// 				mainRoom += currMaxSizeAllowed[i];
// 			} else if (myMinBufferThreshold > 1) {
// 				mainRoom += myMinBufferThreshold; // Always want to give minBufferThreshold, even if their current CMSA may be smaller
// 			}
// 		}
// 	}
// 	uint32_t currMainRoomDemand = mainRoom + marginalRequest;
// 	if (verbose) std::cout << "mainRoom=" << mainRoom << ", currMainRoomDemand=" << currMainRoomDemand << std::endl;

// 	if (currMainRoomDemand + burstReserve <= TotalBuffer) {
// 		mainRoom += marginalRequest;
// 		headRoom = TotalBuffer - mainRoom;
// 		if (verbose) std::cout << "TotalBuffer is enough, mainRoom=" << mainRoom << ", headRoom=" << headRoom << std::endl;
// 		allocateHeadRoomPartitioning(headRoom);
// 		if (marginalRequest != 0) {
// 			uint32_t prevCMSA = currMaxSizeAllowed[thisProberId];
// 			currMaxSizeAllowed[thisProberId] += marginalRequest; 
// 			checkChangeInCurrMaxSizeAllowed(thisProberId,prevCMSA,currMaxSizeAllowed[thisProberId]);
// 		}
// 	} else {
// 		headRoom = burstReserve;
// 		mainRoom = TotalBuffer - headRoom;
// 		if (verbose) std::cout << "TotalBuffer is not enough, mainRoom=" << mainRoom << ", headRoom=" << headRoom << std::endl;
// 		allocateHeadRoomPartitioning(headRoom);
// 		// currMaxSizeAllowed[thisProberId] += marginalRequest; // AnnC: temporarily add this for allocateMainRoomUtility to work, hopefully does not hurt anything
// 		allocateMainRoomUtility(currMainRoomDemand, mainRoom, thisProberId, marginalRequest);
// 	}
// }

// void SharedMemoryBuffer::setMinBufferThreshold(uint32_t proberid, uint32_t threshold) { 
// 	if (verbose) std::cout << Simulator::Now() << ": setMinBufferThreshold, proberId=" << proberid << ", minBufferThreshold=" << threshold << std::endl;
// 	minBufferThreshold[proberid] = threshold; 
// }

// void SharedMemoryBuffer::setDoMonitorDrop(uint32_t proberid, bool value) { 
// 	if (verbose) std::cout << Simulator::Now() << ": setDoMonitorDrop, proberId=" << proberid << ", doMonitorDrop=" << value << std::endl;
// 	doMonitorDrop[proberid] = value; 
// }

// bool SharedMemoryBuffer::hasInstanceInBetweenProbeMinDurationZeroQueueMonitorMap(uint32_t proberid, uint32_t startkey, uint32_t endkey) {
// 	bool found = false;
// 	std::map<int64_t,int64_t>::iterator it;
// 	for (it=probeMinDurationZeroQueueMonitorMap[proberid].begin(); it!=probeMinDurationZeroQueueMonitorMap[proberid].end(); it++) {
// 		if (startkey < it->first and it->first < endkey) {
// 			found = true;
// 			break;
// 		}
// 	}
// 	return found;
// }

bool SharedMemoryBuffer::isFlowIdByProberIdEnded(uint32_t flowid, uint32_t proberid) { 
	std::pair<uint32_t,uint32_t> p (flowid,proberid);
	return flowIdByProberIdEnded.find(p) != flowIdByProberIdEnded.end(); 
}

// void SharedMemoryBuffer::setStatus(uint32_t proberid, uint32_t flowid, uint32_t status) {
// 	if (verbose) std::cout << Simulator::Now() << ": setStatus, proberid=" << proberid << ", flowid=" << flowid << ", status=" << status << std::endl;
// 	if (statusTracker.find(proberid) == statusTracker.end()) {
// 		std::map<uint32_t,uint32_t> m;
// 		statusTracker.insert( std::pair<uint32_t, std::map<uint32_t,uint32_t>>(proberid,m) );
// 	}
// 	if (statusTracker[proberid].find(flowid) == statusTracker[proberid].end()) {
// 		if (status!=StatusNone) statusTracker[proberid].insert( std::pair<uint32_t,uint32_t>(flowid,status) );
// 	} else {
// 		if (status!=StatusNone) {
// 			statusTracker[proberid].find(flowid)->second = status;
// 		} else {
// 			statusTracker[proberid].erase(flowid);
// 		}
// 	}

// 	bool isHRprober = false;
// 	if (status==HREntering or status==HRFlowEnd or status==HRClearPackets) isHRprober = true;
// 	checkHeadRoomWaitRoom(proberid, isHRprober);

// 	if (status == HRFlowEnd) {
// 		if (isProberStaticInHeadRoom(proberid)) {
// 			currMaxSizeAllowed[proberid] = 0;
// 			minBufferThreshold[proberid] = 0;
// 		}
// 	}
// }

// uint32_t SharedMemoryBuffer::getStatus(uint32_t proberid, uint32_t flowid) {
// 	if (statusTracker.find(proberid) == statusTracker.end()) return StatusNone;
// 	if (statusTracker[proberid].find(flowid) == statusTracker[proberid].end()) return StatusNone;
// 	return statusTracker[proberid][flowid];
// }

// void SharedMemoryBuffer::checkHeadRoomWaitRoom(uint32_t proberid, bool isHRprober) {
// 	std::map<uint32_t,uint32_t> m = statusTracker[proberid];
// 	std::map<uint32_t,uint32_t>::iterator it;

// 	if (isHRprober) {
// 		bool shouldEnterHeadRoom = false;
// 		bool shouldLeaveHeadRoom = true;
// 		for (it=m.begin(); it!=m.end(); it++) {
// 			if (it->second == HREntering) shouldEnterHeadRoom = true;
// 			if (it->second==HREntering or it->second==HRFlowEnd) shouldLeaveHeadRoom = false;
// 		}
// 		if (shouldEnterHeadRoom) {
// 			addToProberInHeadRoom(proberid);
// 			if (verbose) std::cout << Simulator::Now() << ": addToHeadRoom, proberid=" << proberid << std::endl;
// 		}
// 		if (shouldLeaveHeadRoom) {
// 			removeFromProberInHeadRoom(proberid);
// 			if (verbose) std::cout << Simulator::Now() << ": removeFromHeadRoom, proberid=" << proberid << std::endl;
// 		}
// 	} else {
// 		bool shouldEnterWaitRoom = false;
// 		bool shouldLeaveWaitRoom = false;
// 		for (it=m.begin(); it!=m.end(); it++) {
// 			if (it->second == MRWaitRoom) shouldEnterWaitRoom = true;
// 			if (it->second == MRProbing) shouldLeaveWaitRoom = true;
// 		}
// 		if (shouldLeaveWaitRoom) shouldEnterWaitRoom = false;
// 		if (shouldEnterWaitRoom) {
// 			addToProberInWaitRoom(proberid);
// 			if (verbose) std::cout << Simulator::Now() << ": addToWaitRoom, proberid=" << proberid << std::endl;
// 		}
// 		if (shouldLeaveWaitRoom) {
// 			removeFromProberInWaitRoom(proberid);
// 			if (verbose) std::cout << Simulator::Now() << ": removeFromWaitRoom, proberid=" << proberid << std::endl;
// 		}
// 	}
// }

// // This might be the same as isProberInHeadRoom
// bool SharedMemoryBuffer::isProberActiveInHeadRoom(uint32_t proberid) { 
// 	if (statusTracker.find(proberid) == statusTracker.end()) return false;
// 	std::map<uint32_t,uint32_t> m = statusTracker[proberid];
// 	std::map<uint32_t,uint32_t>::iterator it;
// 	for (it=m.begin(); it!=m.end(); it++) {
// 		if (it->second==HREntering or it->second==HRFlowEnd) return true; // i.e. some HREntering or HRFlowEnd
// 	}
// 	return false; 
// }

// bool SharedMemoryBuffer::isProberStaticInHeadRoom(uint32_t proberid) {
// 	if (statusTracker.find(proberid) == statusTracker.end()) return false;
// 	std::map<uint32_t,uint32_t> m = statusTracker[proberid];
// 	std::map<uint32_t,uint32_t>::iterator it;
// 	for (it=m.begin(); it!=m.end(); it++) {
// 		if (it->second==HREntering) return false;
// 	}
// 	return true; // i.e. all HRClearPackets or HRFlowEnd
// }

// bool SharedMemoryBuffer::isProberActiveInMainRoom(uint32_t proberid) {
// 	if (statusTracker.find(proberid) == statusTracker.end()) return false;
// 	std::map<uint32_t,uint32_t> m = statusTracker[proberid];
// 	std::map<uint32_t,uint32_t>::iterator it;
// 	for (it=m.begin(); it!=m.end(); it++) {
// 		if (it->second==MRWaitRoom or it->second==MRProbing or it->second==MRFlowEnd) return true;
// 	}
// 	return false; // i.e. all MRClearPackets
// }

// bool SharedMemoryBuffer::isProberStaticInMainRoom(uint32_t proberid) {
// 	if (statusTracker.find(proberid) == statusTracker.end()) return false;
// 	std::map<uint32_t,uint32_t> m = statusTracker[proberid];
// 	std::map<uint32_t,uint32_t>::iterator it;
// 	for (it=m.begin(); it!=m.end(); it++) {
// 		if (it->second==MRWaitRoom or it->second==MRProbing) return false;
// 	}
// 	return true; // i.e. all MRClearPackets or MRFlowEnd
// }

// void SharedMemoryBuffer::updateAverageQSize() {
// 	if (verbose) std::cout << Simulator::Now() << ": updateAverageQSize";
// 	uint32_t totalQSize = 0;
// 	uint32_t totalECMSA = 0;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		if (isProberActiveInMainRoom(i) and not isProberStaticInMainRoom(i)) {
// 			totalQSize += qSize[i];
// 			if (effectiveCurrMaxSizeAllowed[i] == 1) {
// 				totalECMSA += currMaxSizeAllowed[i];
// 			} else {
// 				totalECMSA += effectiveCurrMaxSizeAllowed[i];
// 			}
// 		}
// 	}
// 	averageQSizeSum += totalQSize;
// 	averageQSizeCounter++;
// 	averageEffectiveCMSASum += totalECMSA;
// 	if (verbose) std::cout << ", averageQSizeSum=" << averageQSizeSum << ", averageQSizeCounter=" << averageQSizeCounter;
// 	if (averageQSizeCounter == averageQSizeNumSamples) {
// 		averageQSize = averageQSizeSum/averageQSizeCounter;
// 		averageEffectiveCMSA = averageEffectiveCMSASum/averageQSizeCounter;
// 		averageQSizeCounter = 0;
// 		averageQSizeSum = 0;
// 		averageEffectiveCMSASum = 0;
// 		if (verbose) std::cout << ", averageQSize=" << averageQSize << ", averageEffectiveCMSA=" << averageEffectiveCMSA;
// 	}
// 	if (verbose) std::cout << std::endl;
// 	Simulator::Schedule(MilliSeconds(averageQSizeGranularityMS), &SharedMemoryBuffer::updateAverageQSize,this);
// }

// void SharedMemoryBuffer::beliefScalingPeriodic() {
// 	uint32_t totalQSize = 0;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		if (isProberActiveInMainRoom(i)) { // accept static and non-static queues
// 			totalQSize += qSize[i];
// 		}
// 	}
// 	averageQSizeSum += totalQSize;
// 	averageQSizeCounter++;
// 	if (averageQSizeCounter == averageQSizeNumSamples) {
// 		averageQSize = averageQSizeSum/averageQSizeCounter;
// 		averageQSizeCounter = 0;
// 		averageQSizeSum = 0;
// 		double bufferOccupancyRate = averageQSize / (double)(TotalBuffer-burstReserve);
// 		if (bufferOccupancyRate < 0.5) mainRoomBeliefScaling += 0.2;
// 		if (verbose) std::cout << Simulator::Now() << ": beliefScalingPeriodic, averageQSize=" << averageQSize << ", bufferOccupancyRate=" << bufferOccupancyRate << ", mainRoomBeliefScaling=" << mainRoomBeliefScaling << std::endl;
// 	}
// 	Simulator::Schedule(MilliSeconds(averageQSizeGranularityMS), &SharedMemoryBuffer::beliefScalingPeriodic,this);
// }

// void SharedMemoryBuffer::updateDropsDueToRemainingBuffer(uint32_t proberid, bool reset, bool increment) {
// 	if (reset) {
// 		if (verbose)  std::cout << Simulator::Now() << ": updateDropsDueToRemainingBuffer, proberid=" << proberid << ", reset" << std::endl;
// 		dropsDueToRemainingBuffer[proberid] = 0;
// 		return;
// 	}
// 	if (increment) {
// 		dropsDueToRemainingBuffer[proberid]++;
// 		if (verbose)  std::cout << Simulator::Now() << ": updateDropsDueToRemainingBuffer, proberid=" << proberid << ", increment, dropsDueToRemainingBuffer=" << dropsDueToRemainingBuffer[proberid];
// 		if (dropsDueToRemainingBuffer[proberid] == 5) {
// 			mainRoomBeliefScaling = std::max(mainRoomBeliefScaling-0.1,1.0);
// 			if (verbose) std::cout << ", set mainRoomBeliefScaling=" << mainRoomBeliefScaling << std::endl;
// 			dropsDueToRemainingBuffer[proberid] = 0;
// 		}
// 		if (verbose) std::cout << std::endl;
// 	}
// }

// void SharedMemoryBuffer::updateQSizeQueue() {
// 	uint32_t totalQSize = 0;
// 	uint32_t totalECMSA = 0;
// 	for (uint32_t i=0; i<getTotalProbers(); i++) {
// 		if (isProberActiveInMainRoom(i) and not isProberStaticInMainRoom(i)) {
// 			totalQSize += qSize[i];
// 			if (effectiveCurrMaxSizeAllowed[i] == 1) {
// 				totalECMSA += currMaxSizeAllowed[i];
// 			} else {
// 				totalECMSA += effectiveCurrMaxSizeAllowed[i];
// 			}
// 		}
// 	}
// 	qsizeQueue.push_back(totalQSize);
// 	ecmsaQueue.push_back(totalECMSA);
// 	if (qsizeQueue.size()>averageQSizeNumSamples) {
// 		qsizeQueue.pop_front();
// 		ecmsaQueue.pop_front();
// 	}
// 	Simulator::Schedule(MilliSeconds(averageQSizeGranularityMS), &SharedMemoryBuffer::updateQSizeQueue,this);
// }
	
// double SharedMemoryBuffer::getMainRoomBeliefScaling() {
// 	if (verbose) std::cout << Simulator::Now() << ": getMainRoomBeliefScaling";
// 	if (qsizeQueue.size() < averageQSizeNumSamples) {
// 		if (verbose) std::cout << ", *mainRoomBeliefScaling=" << defaultMainRoomBeliefScaling << std::endl;
// 		return defaultMainRoomBeliefScaling;
// 	}
// 	uint32_t totalq = 0;
// 	uint32_t totalt = 0;
// 	for (uint32_t i=0; i<averageQSizeNumSamples; i++) {
// 		totalq += qsizeQueue[i];
// 		totalt += ecmsaQueue[i];
// 	}
// 	double result = defaultMainRoomBeliefScaling;
// 	if (totalq > 0) result = totalt/(double)totalq;
// 	if (verbose) std::cout << ", totalq=" << totalq << ", totalt=" << totalt << ", *mainRoomBeliefScaling=" << result << std::endl;
// 	return result;
// }

// void SharedMemoryBuffer::allocateBasedOnMinBufferThreshold() {
// 	if (verbose) std::cout << Simulator::Now() << ": allocatedBasedOnMinBufferThreshold, ";
// 	uint64_t totalBufferOnDevice = TotalBuffer;
// 	std::set<uint32_t>::iterator it;
// 	if (!proberNewerFlows.empty()) {
// 		if (verbose) std::cout << "proberNewerFlows is not empty";
// 		uint32_t R = 0;
// 		uint32_t D = 0;
// 		for (uint32_t i=0; i<getTotalProbers(); i++) {
// 			if (isProberInProberNewerFlows(i)) {
// 				D += minBufferThreshold[i];
// 			} else {
// 				R += currMaxSizeAllowed[i];
// 			}
// 		}
// 		if (verbose) std::cout << ", totalBufferOnDevice=" << totalBufferOnDevice << ", R=" << R << ", D=" << D << std::endl;
// 		if (D > totalBufferOnDevice - R) {
// 			uint32_t partition = proberNewerFlows.size();
// 			uint32_t share = (totalBufferOnDevice-R)/partition;
// 			if (verbose) std::cout << "Complete partitioning for probers in proberNewerFlows, partition=" << partition << ", share=" << share << std::endl;
// 			if (verbose) std::cout << "currMaxSizeAllowed: ";
// 			for (it=proberNewerFlows.begin(); it!=proberNewerFlows.end(); ++it) {
// 				uint32_t i = *it;
// 				if (verbose) std::cout << i << ":" << currMaxSizeAllowed[i] << "->";
// 				currMaxSizeAllowed[i] = share;
// 				if (verbose) std::cout << currMaxSizeAllowed[i] << ", ";
// 			}
// 			if (verbose) std::cout << std::endl;
// 		} else {
// 			if (verbose) std::cout << "currMaxSizeAllowed: ";
// 			for (it=proberNewerFlows.begin(); it!=proberNewerFlows.end(); ++it) {
// 				uint32_t i = *it;
// 				if (verbose) std::cout << i << ":" << currMaxSizeAllowed[i] << "->";
// 				currMaxSizeAllowed[i] = minBufferThreshold[i];
// 				if (verbose) std::cout << currMaxSizeAllowed[i] << ", ";
// 			}
// 			if (verbose) std::cout << std::endl;
// 		}
// 	} else {
// 		if (verbose) std::cout << "proberNewerFlows is empty";
// 		totalBufferOnDevice = TotalBuffer - burstReserve;
// 		uint64_t lockedBufferSize = 0;
// 		uint64_t requestedBufferSize = 0;
// 		std::set<uint32_t> lockedProbers;
// 		std::set<uint32_t> unlockedProbers;
// 		for (uint32_t i=0; i<getTotalProbers(); i++) {
// 			if (bufferSizeLock[i] == true) {
// 				lockedBufferSize += currMaxSizeAllowed[i];
// 				lockedProbers.insert(i);
// 			} else {
// 				if (minBufferThreshold[i] > 0) {
// 					requestedBufferSize += minBufferThreshold[i];
// 					unlockedProbers.insert(i);
// 				}
// 			}
// 		}
		
// 		uint64_t availableBufferSize = totalBufferOnDevice - lockedBufferSize;
// 		uint64_t totalAbsoluteMinBuffer = unlockedProbers.size() * absoluteMinBuffer;
// 		// AnnC: take min with 1 to avoid assuming that lockedBufferSize + requestedBufferSize > TotalBuffer
// 		double scaling = std::min(1.0,(availableBufferSize-totalAbsoluteMinBuffer)/(double)(requestedBufferSize-totalAbsoluteMinBuffer));
// 		if (verbose) std::cout << ", scaling=" << scaling << ", totalBufferOnDevice=" << totalBufferOnDevice << ", lockedBufferSize=" << lockedBufferSize << ", availableBufferSize=" << availableBufferSize << ", requestedBufferSize=" << requestedBufferSize << ", totalAbsoluteMinBuffer=" << totalAbsoluteMinBuffer << std::endl;
// 		if (verbose) std::cout << "currMaxSizeAllowed: ";
// 		std::set<uint32_t>::iterator it;
// 		for (it=unlockedProbers.begin(); it!=unlockedProbers.end(); ++it) {
// 			uint32_t i = *it;
// 			if (minBufferThreshold[i] > 0) {
// 				if (verbose) std::cout << i << ":" << currMaxSizeAllowed[i] << "->";
// 				currMaxSizeAllowed[i] = absoluteMinBuffer + (minBufferThreshold[i]-absoluteMinBuffer)*scaling;
// 				if (verbose) std::cout << currMaxSizeAllowed[i] << ", ";
// 			}
// 		}
// 		if (verbose) std::cout << std::endl;
// 	}
// }

// void SharedMemoryBuffer::addProberToProberAwaitingMinBuffer(uint32_t proberId) { 
// 	if (verbose) std::cout << Simulator::Now() << ": add prober " << proberId << " into proberAwaitingMinBuffer deque" << std::endl; 
// 	proberAwaitingMinBuffer.push_front(proberId); 
// }

// bool SharedMemoryBuffer::isNewFlow(uint64_t flowId) {
// 	if (flowIdSeen.find(flowId) != flowIdSeen.end()) {
//         return false;
//     }
//     flowIdSeen.insert(flowId);
//     return true;
// }

// void SharedMemoryBuffer::probeMinBufferSchedule9RTT(uint32_t proberId) {
// 	uint64_t maxbuffer = probeMinMaxBufferUsed[proberId];
// 	bool maxBufferReached = false;
// 	if (maxbuffer+1500 >= currMaxSizeAllowed[proberId]) {
// 		if (verbose) std::cout << "probeMin: Cubic detected, proberId=" << proberId << ", maxBufferAllowed=" << currMaxSizeAllowed[proberId] << ", maxBufferUsed=" << maxbuffer << std::endl; 
// 		maxBufferReached = true;
// 	}
// 	Simulator::Schedule(NanoSeconds(cubicDrainNRTT*averageQueueRTT[proberId]),&SharedMemoryBuffer::probeMinBufferSchedule1RTT,this,proberId, maxBufferReached);
// }

// void SharedMemoryBuffer::probeMinBufferSchedule1RTT(uint32_t proberId, bool maxBufferReached) {
// 	if (verbose) std::cout << Simulator::Now() << ": probeMin (recursive), proberid=" << proberId << std::endl;
// 	// Collect stats from the previous probe and reset the probe stats
// 	uint64_t totaldropbytes = probeMinTotalDropBytes[proberId];
// 	uint64_t totalsentbytes = probeMinAverageThroughput[proberId];
// 	uint64_t maxbuffer = probeMinMaxBufferUsed[proberId];
// 	uint64_t minbuffer = probeMinMinBufferUsed[proberId];
// 	double droprate = totaldropbytes / (double)totalsentbytes;
// 	double throughput = 8.0*totalsentbytes/(aggregateNRTT*averageQueueRTT[proberId])/normalizedPortBW[proberId];
// 	probeMinTotalDropBytes[proberId] = 0;
// 	probeMinAverageThroughput[proberId] = 0;
// 	probeMinMaxBufferUsed[proberId] = 0;
// 	probeMinMinBufferUsed[proberId] = TotalBuffer;
// 	if (verbose) std::cout << Simulator::Now() << ": probeMin, proberId=" << proberId << ", throughput=" << throughput << ", droprate=" << droprate << ", currMaxSizeAllowed=" << currMaxSizeAllowed[proberId] << ", maxBufferUsed=" << maxbuffer << std::endl;

// 	if (maxBufferReached) {
// 		if (verbose) std::cout << Simulator::Now() << ": probeMin, Cubic, proberId=" << proberId << ", maxBufferUsed=" << maxbuffer << ", minBufferUsed=" << minbuffer << ", stop probing" << std::endl;
// 		// AnnC: I think this part can be better; the bound can be tighter
// 		currMaxSizeAllowed[proberId] = maxbuffer-minbuffer;
// 		minBufferThreshold[proberId] = currMaxSizeAllowed[proberId];
// 		bufferSizeLock[proberId] = false;
// 		bufferSizeLockStart[proberId] = 0;
// 		setThroughputData(proberId, currMaxSizeAllowed[proberId], throughput);
// 		addProberToProberDoneMinBuffer(proberId);
// 		return;
// 	}

// 	if (maxbuffer == probeMinMaxBufferUsedRecord[proberId]) {
// 		probeMinMaxBufferTimes[proberId] += 1;
// 	} else {
// 		probeMinMaxBufferUsedRecord[proberId] = maxbuffer;
// 		probeMinMaxBufferTimes[proberId] = 1;
// 	}
// 	if (probeMinMaxBufferUsedRecord[proberId]<currMaxSizeAllowed[proberId] and probeMinMaxBufferTimes[proberId]==nonCubicNProbe) {
// 		if (verbose) std::cout << "Exiting probeMin: BBR or Copa, proberId=" << proberId << ", minBuffer=" << maxbuffer << ", stop probing" << std::endl;
// 		currMaxSizeAllowed[proberId] = maxbuffer;
// 		minBufferThreshold[proberId] = currMaxSizeAllowed[proberId];
// 		bufferSizeLock[proberId] = false;
// 		bufferSizeLockStart[proberId] = 0;
// 		setThroughputData(proberId, currMaxSizeAllowed[proberId], throughput);
// 		addProberToProberDoneMinBuffer(proberId);
// 		return;
// 	}

// 	Simulator::Schedule(NanoSeconds((aggregateNRTT-cubicDrainNRTT)*averageQueueRTT[proberId]),&SharedMemoryBuffer::probeMinBufferSchedule9RTT,this,proberId);

// 	// if (maxbuffer == probeMinMaxBufferUsedRecord[proberId]) {
// 	// 	probeMinMaxBufferTimes[proberId] += 1;
// 	// } else {
// 	// 	probeMinMaxBufferUsedRecord[proberId] = maxbuffer;
// 	// 	probeMinMaxBufferTimes[proberId] = 1;
// 	// }
// 	// if (probeMinMaxBufferUsedRecord[proberId]<currMaxSizeAllowed[proberId] and probeMinMaxBufferTimes[proberId]==noncubicNProbe) {
// 	// 	if (verbose) std::cout << "probeMin: BBR or Copa, minBuffer=" << maxbuffer << ", stop probing" << std::endl;
// 	// 	currMaxSizeAllowed[proberId] = maxbuffer;
// 	// 	minBufferThreshold[proberId] = maxbuffer;
// 	// 	bufferSizeLock[proberId] = false;
// 	// 	return;
// 	// }

// 	// if (probeMinIsCubic[proberId] and (throughput<=0.99 or droprate>0.005)) {
// 	// 	if (verbose) std::cout << "probeMin: Cubic, minBuffer=" << maxbuffer+1500 << ", stop probing" << std::endl;
// 	// 	currMaxSizeAllowed[proberId] = maxbuffer + 1500;
// 	// 	minBufferThreshold[proberId] = maxbuffer + 1500;
// 	// 	bufferSizeLock[proberId] = false;
// 	// 	return;
// 	// }

// 	// probeMinSlowStepCounter[proberId] += 1;
// 	// if (probeMinSlowStepCounter[proberId] >= 3 and maxbuffer+1500 >= currMaxSizeAllowed[proberId]) {
// 	// 	if (verbose) std::cout << "probeMin: Cubic" << std::endl;
// 	// 	currMaxSizeAllowed[proberId] -= 1500;
// 	// 	probeMinSlowStepCounter[proberId] = 0;
// 	// 	probeMinIsCubic[proberId] = true;
// 	// }

// 	// Simulator::Schedule(NanoSeconds(aggregateNRTT*RTTns),&SharedMemoryBuffer::probeMinBufferSchedule,this,proberId);
// }

// void SharedMemoryBuffer::probeMinBuffer(uint32_t proberId) {
// 	if (verbose) std::cout << Simulator::Now() << ": probeMin (first), proberid=" << proberId << std::endl;
// 	bufferSizeLock[proberId] = true;
// 	bufferSizeLockStart[proberId] = 0;
// 	currMaxSizeAllowed[proberId] = minBufferThreshold[proberId];
// 	probeMinAverageThroughput[proberId] = 0;
// 	probeMinTotalDropBytes[proberId] = 0;
// 	probeMinMaxBufferUsed[proberId] = 0;
// 	probeMinMinBufferUsed[proberId] = 0;
// 	Simulator::Schedule(NanoSeconds(aggregateNRTT*averageQueueRTT[proberId]),&SharedMemoryBuffer::probeMinBufferSchedule9RTT,this,proberId);
// }

// void SharedMemoryBuffer::setUpSchedule() {
// 	// if (verbose) std::cout << Simulator::Now() << ": myRemainingBuffer=" << getMyRemainingBuffer() << ", burstReserve=" << burstReserve << std::endl;
// 	while (!proberAwaitingMinBuffer.empty()) {
// 		uint32_t head = proberAwaitingMinBuffer.front();
// 		if (getMyRemainingBuffer()-burstReserve+currMaxSizeAllowed[head] >= minBufferThreshold[head]) {
// 			probeMinBuffer(head);
// 			proberAwaitingMinBuffer.pop_front();
// 		} else {
// 			break;
// 		}
// 	}
// 	Simulator::Schedule(MicroSeconds(1),&SharedMemoryBuffer::setUpSchedule,this);
// }

// uint32_t SharedMemoryBuffer::getMyRemainingBuffer() {
// 	uint32_t totalAllowedBuffer = 0;
// 	for (uint64_t i=0; i<getTotalProbers(); i++) totalAllowedBuffer+=currMaxSizeAllowed[i];
// 	return TotalBuffer-totalAllowedBuffer;
// }

// bool SharedMemoryBuffer::isProberInProberAwaitingMinBuffer(uint32_t proberId) { 
// 	return std::find(proberAwaitingMinBuffer.begin(), proberAwaitingMinBuffer.end(), proberId) != proberAwaitingMinBuffer.end(); 
// }

// bool SharedMemoryBuffer::isProberInProberDoneMinBuffer(uint32_t proberId) {
// 	return proberDoneMinBuffer.find(proberId) != proberDoneMinBuffer.end();
// }

// bool SharedMemoryBuffer::isProberInProberNewerFlows(uint32_t proberId) {
// 	return proberNewerFlows.find(proberId) != proberNewerFlows.end();
// }

void SharedMemoryBuffer::printDesignZeroVec(uint32_t proberId) {
	std::cout << "DesignZeroVec," << proberId << std::endl;
	int64_t mycount = 0;
	for (auto interval: designZeroVec[proberId]) {
		std::cout << designZeroTimestamp[proberId][mycount] << "," << interval << std::endl;
		mycount += 1;
	}
}
	
}
