// /*
// AnnC:
// Author: Ann Zhou
// */

// #ifndef UTILITY_WAREHOUSE_H
// #define UTILITY_WAREHOUSE_H

// #include "utility-prober.h"
// #include <vector>
// #include <set>

// namespace ns3 {

// class UtilityWarehouse : public Object {
// public:
// static TypeId GetTypeId (void); 
// UtilityWarehouse();
// ~UtilityWarehouse();
// void createUtilityProbers(uint64_t numPorts, uint64_t numQueuesPerPort);
// bool isNewFlow(uint64_t flowId);
// void resetProber(uint64_t portId, uint64_t queueId, uint32_t RTTns, double portbwGbps);

// private:
// std::vector<Ptr<UtilityProber>> probers;
// std::set<uint64_t> flowIdSeen;
// uint64_t numPorts;
// uint64_t numQueuesPerPort;

// uint64_t getProberId(uint64_t portId, uint64_t queueId);
// };

// } // namespace ns3

// #endif /* UTILITY_WAREHOUSE_H */