#ifndef AUTO_QUEUE_DISC_H
#define AUTO_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include "ns3/queue.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/auto-flow.h"
#include <list>
#include <vector>

namespace ns3 {

class AutoQueueDisc : public QueueDisc
{
public:

  static TypeId GetTypeId (void);

  AutoQueueDisc ();

  virtual ~AutoQueueDisc ();

private:
  class AutoClass : public Object
  {
  public:

    static TypeId GetTypeId (void);

    AutoClass ();

    AutoClass (uint32_t prio);

    Ptr<AutoFlow> FindFlow (uint32_t h);
    Ptr<AutoFlow> GetFrontFlow (void);
    uint32_t GetNPackets (void);
    uint32_t GetMaxPackets (void);
    void AddFlow (Ptr<AutoFlow> flow);
    void RemoveFlow (Ptr<AutoFlow> flow);
    uint32_t GetNFlows (void);

    void FillDeficit (void);

    double_t FlowTime2Weight (Time timeDiff, double_t remainingWeight);

    void UpdateClassWeights (double_t remainingWeight);  
    
    QueueDisc::Stats GetStats (void);

    uint32_t m_maxPackets;
    uint32_t m_deficit;
    uint32_t m_priority;
    uint32_t m_index;
    uint32_t m_quantum;
    double_t m_flowWeights;
    double_t m_prioWeights;

    bool m_lastRoundMoved;

    // Reasons for dropping packets
    static constexpr const char* UNCLASSIFIED_DROP = "Unclassified drop";  //!< No packet filter able to classify packet
    static constexpr const char* OVERLIMIT_DROP = "Overlimit drop";        //!< Overlimit dropped packets
    static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";  //!< Packet dropped due to queue disc limit exceeded

    std::list <Ptr<AutoFlow> > m_flowList;
    uint32_t m_flows;
    uint32_t m_perturbation;
    QueueDisc::Stats m_classStats;
    uint32_t m_lastQueueLen;
  };

public:
  void AddAutoClass (Ptr<AutoClass> autoClass, uint32_t quantum, double_t weight, uint32_t maxSize);
  uint32_t GetNumClasses (void) const;
  double_t GetClassNPackets (size_t index);

  // Reasons for dropping packets
  static constexpr const char* UNCLASSIFIED_DROP = "Unclassified drop";  //!< No packet filter able to classify packet
  static constexpr const char* OVERLIMIT_DROP = "Overlimit drop";        //!< Overlimit dropped packets
  static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";  //!< Packet dropped due to queue disc limit exceeded


private:
  static const uint32_t prio2band[16];

  // Operations offered by multi queue disc should be the same as queue disc
  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual Ptr<const QueueDiscItem> DoPeek (void) const;
  virtual bool CheckConfig (void);
  virtual void InitializeParams (void);

  void UpdateFlowClassification (void);

  // The internal DWRR queue discs are first organized in a map
  // with the priority as key and then in a linked list if they are
  // with the same priority
  std::vector<Ptr<AutoClass> > m_autoClasses;
  uint16_t m_numClass;
  uint32_t m_baseQuantum;
  double_t m_prioRatio;
  bool m_autoClassify;
  Time m_updateInterval;
  Time m_lastUpdated;

  std::string m_decayingFunc;
  double_t m_decayingCoef;

  double_t m_dequeueDiffThres;
  double_t m_occupyDiffThres;
  uint16_t m_occupyMinPktsThres;

  std::map<uint32_t, std::list<Ptr<AutoClass> > > m_active;
  uint32_t m_flows;          //!< Number of flow queues
  uint32_t m_perturbation;   //!< hash perturbation value
  std::map<uint32_t, Ptr<AutoFlow> > m_flowsIndices;    //!< Map with the index of class for each flow
  
  ObjectFactory m_flowFactory;         //!< Factory to create a new flow
  ObjectFactory m_queueDiscFactory;    //!< Factory to create a new queue
};

} // namespace ns3

#endif
