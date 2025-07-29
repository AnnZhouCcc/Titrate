#ifndef AUTO_FLOW_H
#define AUTO_FLOW_H

#include "ns3/queue-disc.h"
#include "ns3/queue.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"

#include <vector>
#include <numeric>

namespace ns3 {

class AutoFlow : public QueueDiscClass {
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief AutoFlow constructor
   */
  AutoFlow ();

  virtual ~AutoFlow ();

  /**
   * \brief Set the index for this flow
   * \param index the index for this flow
   */
  void SetIndex (uint32_t index);
  /**
   * \brief Get the index of this flow
   * \return the index of this flow
   */
  uint32_t GetIndex (void) const;

  void SetClassIndex (uint32_t classIndex);

  uint32_t GetClassIndex (void) const;

  Time GetHeadTimestamp (void);

  Time GetLastUsed (void);
  
  void MeasureQueueLen (void);
  void UpdateQueueStats (uint32_t curClassQueueLen, uint32_t flowNum);

  uint32_t GetAvgQueueLen (void);
  double_t GetDequeueRatio (void);
  double_t GetOccupancyRatio (void);
  double_t GetDequeueRatioFairDiff (void);
  double_t GetOccupancyRatioFairDiff (void);
  int32_t GetDequeueFairDiff (void);
  int32_t GetOccupancyFairDiff (void);

  void UpdateAtEnqueue (Ptr<QueueDiscItem> item);
  
  void UpdateAtDequeue (Ptr<QueueDiscItem> item);

  Time GetCreatedTime (void);

  void UpdateFlowWeight (double_t remainingWeight, uint32_t numFlows);

  double_t GetFlowWeight (void);

  // Reasons for dropping packets
  static constexpr const char* TARGET_EXCEEDED_DROP = "Target exceeded drop";  //!< Sojourn time above target
  static constexpr const char* OVERLIMIT_DROP = "Overlimit drop";  //!< Overlimit dropped packet

private:
  // Operations offered by multi queue disc should be the same as queue disc

  bool m_initial;
  int32_t m_deficit;    //!< the deficit for this flow
  uint32_t m_index;     //!< the index for this flow
  uint32_t m_classIndex;
  Time m_lastUsed;
  Time m_lastUpdateWeight;
  Time m_updateInterval;
  Time m_queuingDelay;
  Time m_created;

  std::vector<uint32_t> m_queueLenList;
  uint32_t m_avgQueueLen;

  double_t m_dequeueRatio;
  double_t m_occupancyRatio;
  double_t m_dequeueRatioFairDiff;
  double_t m_occupancyRatioFairDiff;
  int32_t m_dequeueFairDiff;
  int32_t m_occupancyFairDiff;

  double_t m_weight;
  std::string m_decayingFunc;
  double_t m_decayingCoef;

  QueueDisc::Stats m_lastMyStats;
  QueueDisc::Stats m_lastClassStats;
};


}  // namespace ns3

#endif /* AUTO_FLOW_H */