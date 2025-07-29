// /*
// AnnC:
// Author: Ann Zhou
// */

// #include "ns3/log.h"
// #include "ns3/object-factory.h"
// #include "utility-warehouse.h"

// namespace ns3 {

// NS_LOG_COMPONENT_DEFINE ("UtilityWarehouse");

// NS_OBJECT_ENSURE_REGISTERED (UtilityWarehouse);

// TypeId UtilityWarehouse::GetTypeId(void) {
//     static TypeId tid = TypeId ("ns3::UtilityWarehouse")
//     .SetParent<Object> ()
//     .SetGroupName ("TrafficControl")
//     .AddConstructor<UtilityWarehouse> ()
//     // .AddAttribute ("nPrior","number of queues", UintegerValue (5),
//     //                                  MakeUintegerAccessor (&GenQueueDisc::nPrior),
//     //                                     MakeUintegerChecker<uint32_t> ())
//   ;
//   return tid;
// }

// UtilityWarehouse::UtilityWarehouse() {
//     NS_LOG_FUNCTION (this);
//     numPorts = 0;
//     numQueuesPerPort = 0;
//     std::cout << "utility warehouse constructor";
//     std::cout << "; helloooo world" << std::endl;

// }

// UtilityWarehouse::~UtilityWarehouse() {
//     NS_LOG_FUNCTION (this);
//     std::cout << "utility warehouse destructor" << std::endl;
// }

// void UtilityWarehouse::createUtilityProbers(uint64_t numPorts, uint64_t numQueuesPerPort) {
//     uint64_t numProbers = numPorts*numQueuesPerPort;
//     for (uint64_t i=0; i<numProbers; i++) {
//         probers.push_back(CreateObject<UtilityProber>());
//     }
//     numPorts = numPorts;
//     numQueuesPerPort = numQueuesPerPort;
// }

// uint64_t UtilityWarehouse::getProberId(uint64_t portId, uint64_t queueId) {
//     return portId * numPorts + queueId;
// }

// bool UtilityWarehouse::isNewFlow(uint64_t flowId) {
//     if (flowIdSeen.find(flowId) != flowIdSeen.end()) {
//         return false;
//     }
//     flowIdSeen.insert(flowId);
//     return true;
// }

// void UtilityWarehouse::resetProber(uint64_t portId, uint64_t queueId, uint32_t RTTns, double portbwGbps) {
//     std::cout << "reset prober with portid=" << portId << ", queueid=" << queueId;
//     uint64_t proberId = getProberId(portId, queueId);
//     Ptr<UtilityProber> prober = probers[proberId];
//     prober->setCurrMaxSize(uint64_t(RTTns * portbwGbps / 8.0 * 3.0/7.0));
//     std::cout << " | currMaxSize=" << prober->getCurrMaxSize() << std::endl;
//     prober->setMinBuffer(0);
//     prober->clearUtlityData();
//     prober->clearThroughputData();
//     prober->clearDropData();
// }

// } // namespace ns3