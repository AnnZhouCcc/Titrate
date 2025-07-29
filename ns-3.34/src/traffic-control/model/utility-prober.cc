// /*
// AnnC:
// Author: Ann Zhou
// */

// #include "ns3/log.h"
// #include "ns3/object-factory.h"
// #include "utility-prober.h"

// namespace ns3 {

// NS_LOG_COMPONENT_DEFINE ("UtilityProber");

// NS_OBJECT_ENSURE_REGISTERED (UtilityProber);

// TypeId UtilityProber::GetTypeId(void) {
//     static TypeId tid = TypeId ("ns3::UtilityProber")
//     .SetParent<Object> ()
//     .SetGroupName ("TrafficControl")
//     .AddConstructor<UtilityProber> ()
//     // .AddAttribute ("nPrior","number of queues", UintegerValue (5),
//     //                                  MakeUintegerAccessor (&GenQueueDisc::nPrior),
//     //                                     MakeUintegerChecker<uint32_t> ())
//   ;
//   return tid;
// }

// UtilityProber::UtilityProber() {
//     NS_LOG_FUNCTION (this);
//     currMaxSize = 0;
//     minBuffer = 0;
//     std::cout << "utility prober constructor";
//     std::cout << "; hello world" << std::endl;

// }

// UtilityProber::~UtilityProber() {
//     NS_LOG_FUNCTION (this);
//     std::cout << "utility prober destructor" << std::endl;
// }

// } // namespace ns3