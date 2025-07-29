#ifndef OTHER_POLICY_H
#define OTHER_POLICY_H

#include "fec-policy.h"
#include "ns3/object.h"
#include "ns3/nstime.h"
#include <vector>
#include "math.h"

namespace ns3{

class FixedPolicy : public FECPolicy {
public:
    FixedPolicy ();
    FixedPolicy (double_t rate);
    ~FixedPolicy ();
    static TypeId GetTypeId (void);
private:
    double_t k_rate;
public:
    FECParam GetPolicyFECParam (
        Ptr<NetStat> statistic, uint32_t bitrate, uint16_t ddl, uint16_t ddlLeft,
        bool isRtx, uint8_t frameSize, uint16_t maxGroupSize, bool fixGroupSize);
    std::string GetFecName (void);
};  // class FixedPolicy

class RtxOnlyPolicy : public FixedPolicy {
public:
    RtxOnlyPolicy ();
    ~RtxOnlyPolicy ();
    static TypeId GetTypeId (void);
    std::string GetFecName (void);
};  // class RtxOnlyPolicy

class PtoOnlyPolicy : public FixedPolicy {
public:
    PtoOnlyPolicy ();
    ~PtoOnlyPolicy ();
    static TypeId GetTypeId (void);
    std::string GetFecName (void);
};  // class PtoOnlyPolicy

class BolotPolicy : public FECPolicy {
public:
    BolotPolicy ();
    ~BolotPolicy ();
    static TypeId GetTypeId (void);
private:
    double_t k_bolotLow;
    double_t k_bolotHigh;
    double_t* k_rewardList;
    double_t* k_rateList;
    int m_lastComb;
public:
    FECParam GetPolicyFECParam (
        Ptr<NetStat> statistic, uint32_t bitrate, uint16_t ddl, uint16_t ddlLeft,
        bool isRtx, uint8_t frameSize, uint16_t maxGroupSize, bool fixGroupSize);
    std::string GetFecName (void);
};  // class BolotPolicy

class UsfPolicy : public FECPolicy {
public:
    UsfPolicy();
    ~UsfPolicy();
    static TypeId GetTypeId (void);
private:
    double_t k_bolotLow;
    double_t k_bolotHigh;
    double_t k_minThresh;
    double_t* k_rewardList;
    double_t* k_rateList;
    int m_lastComb;
    double_t m_lossPbPrevious;
public:
    FECParam GetPolicyFECParam (
        Ptr<NetStat> statistic, uint32_t bitrate, uint16_t ddl, uint16_t ddlLeft,
        bool isRtx, uint8_t frameSize, uint16_t maxGroupSize, bool fixGroupSize);
    std::string GetFecName (void);
};  // class UsfPolicy

}; // namespace ns3

#endif /* OTHER_POLICY_H */