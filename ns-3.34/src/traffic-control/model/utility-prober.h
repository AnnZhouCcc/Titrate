// /*
// AnnC:
// Author: Ann Zhou
// */

// #ifndef UTILITY_PROBER_H
// #define UTILITY_PROBER_H

// #include <vector>
// #include <utility>

// namespace ns3 {

// class UtilityProber : public Object {
// public:
// static TypeId GetTypeId (void); 
// UtilityProber();
// ~UtilityProber();
// void setCurrMaxSize(uint64_t size) { currMaxSize = size; }
// uint64_t getCurrMaxSize() { return currMaxSize; }
// void setMinBuffer(uint64_t size) { minBuffer = size; }
// void clearUtlityData() { utilityData.clear(); }
// void clearThroughputData() { throughputData.clear(); }
// void clearDropData() { dropData.clear(); }

// private:
// uint64_t currMaxSize;
// uint64_t minBuffer;
// std::vector<std::pair<uint64_t,uint64_t>> utilityData;
// std::vector<std::pair<uint64_t,double>> throughputData;
// std::vector<std::pair<uint64_t,uint64_t>> dropData;
// };

// } // namespace ns3

// #endif /* UTILITY_PROBER_H */