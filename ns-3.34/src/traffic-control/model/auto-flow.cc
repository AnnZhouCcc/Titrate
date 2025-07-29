#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/string.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/socket.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "auto-flow.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("AutoFlow");

NS_OBJECT_ENSURE_REGISTERED (AutoFlow);

TypeId
AutoFlow::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::AutoFlow")
    .SetParent<QueueDiscClass> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<AutoFlow> ()
    .AddAttribute ("DecayingFunction",
           "The decaying function.",
           StringValue ("Linear"),
           MakeStringAccessor (&AutoFlow::m_decayingFunc),
           MakeStringChecker ())
    .AddAttribute ("DecayingCoef",
           "The decaying coefficient.",
           DoubleValue (0.5),
           MakeDoubleAccessor (&AutoFlow::m_decayingCoef),
           MakeDoubleChecker<double_t> ())
    .AddAttribute ("UpdateInterval",
           "The update interval",
           TimeValue (MilliSeconds (10)),
           MakeTimeAccessor (&AutoFlow::m_updateInterval),
           MakeTimeChecker ())
  ;
  return tid;
}

AutoFlow::AutoFlow ()
: m_initial (false)
, m_deficit (0)
, m_index (0)
, m_lastClassStats (QueueDisc::Stats ())
, m_lastMyStats (QueueDisc::Stats ())
, m_dequeueRatio (0.)
, m_dequeueRatioFairDiff (0.)
, m_dequeueFairDiff (0)
, m_occupancyRatio (0.)
, m_occupancyRatioFairDiff (0.)
, m_occupancyFairDiff (0)
, m_weight (0)
{
  NS_LOG_FUNCTION (this);
  m_created = Simulator::Now ();
}

AutoFlow::~AutoFlow ()
{
  NS_LOG_FUNCTION (this);
}

void
AutoFlow::MeasureQueueLen (void)
{
  NS_LOG_FUNCTION (this);
  m_avgQueueLen = std::accumulate (m_queueLenList.begin (), m_queueLenList.end (), 0.0) / m_queueLenList.size ();
  m_queueLenList.clear ();
}

void
AutoFlow::UpdateQueueStats (uint32_t curClassQueueLen, uint32_t flowNum)
{
  NS_LOG_FUNCTION (this);
  curClassQueueLen = std::max (1U, curClassQueueLen);

  m_occupancyRatio = (double_t) m_avgQueueLen / curClassQueueLen;
  m_occupancyRatioFairDiff = m_occupancyRatio - 1. / flowNum;
  m_occupancyFairDiff = m_avgQueueLen - (double_t) curClassQueueLen / flowNum;
}

uint32_t
AutoFlow::GetAvgQueueLen (void)
{
  NS_LOG_FUNCTION (this);
  return m_avgQueueLen;
}

double_t
AutoFlow::GetDequeueRatio (void)
{
  NS_LOG_FUNCTION (this);
  return m_dequeueRatio;
}

double_t
AutoFlow::GetOccupancyRatio (void)
{
  NS_LOG_FUNCTION (this);
  return m_occupancyRatio;
}

double_t
AutoFlow::GetDequeueRatioFairDiff (void)
{
  NS_LOG_FUNCTION (this);
  return m_dequeueRatioFairDiff;
}

double_t
AutoFlow::GetOccupancyRatioFairDiff (void)
{
  NS_LOG_FUNCTION (this);
  return m_occupancyRatioFairDiff;
}

int32_t
AutoFlow::GetDequeueFairDiff (void)
{
  NS_LOG_FUNCTION (this);
  return m_dequeueFairDiff;
}

int32_t
AutoFlow::GetOccupancyFairDiff (void)
{
  NS_LOG_FUNCTION (this);
  return m_occupancyFairDiff;
}

void
AutoFlow::UpdateAtEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  m_queueLenList.push_back (GetQueueDisc ()->GetCurrentSize ().GetValue ());
  m_lastUsed = Simulator::Now ();
}

void
AutoFlow::UpdateAtDequeue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this);
  m_queueLenList.push_back (GetQueueDisc ()->GetCurrentSize ().GetValue ());
  Time qDelay = Simulator::Now () - item->GetTimeStamp ();
  m_queuingDelay += (qDelay - m_queuingDelay) * 0.125;
  m_lastUsed = Simulator::Now ();
}

void
AutoFlow::SetIndex (uint32_t index)
{
  NS_LOG_FUNCTION (this << index);
  m_index = index;
}

uint32_t
AutoFlow::GetIndex (void) const
{
  NS_LOG_FUNCTION (this);
  return m_index;
}

void
AutoFlow::SetClassIndex (uint32_t index)
{
    NS_LOG_FUNCTION (this << index);
    m_classIndex = index;
}

uint32_t
AutoFlow::GetClassIndex (void) const
{
  NS_LOG_FUNCTION (this);
  return m_classIndex;
}

Time
AutoFlow::GetHeadTimestamp (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<const QueueDiscItem> item = GetQueueDisc ()->Peek ();
  if (item) {
    return item->GetTimeStamp ();
  }
  else {
    return Seconds (1e6);
  }
}

Time
AutoFlow::GetLastUsed (void)
{
  NS_LOG_FUNCTION (this);
  return m_lastUsed;
}

Time
AutoFlow::GetCreatedTime (void)
{
  NS_LOG_FUNCTION (this);
  return m_created;
}

void
AutoFlow::UpdateFlowWeight (double_t remainingWeight, uint32_t numFlows)
{
  Time timeDiff = Simulator::Now () - m_lastUpdateWeight;
  if (timeDiff < m_updateInterval)
    return;
  remainingWeight = std::max (1., remainingWeight);
  if (m_decayingFunc == "ExpClass") {
    if (m_weight == 0) {
      if (!m_initial) {
        m_initial = true;
        m_lastUpdateWeight = Simulator::Now () + Seconds (1. / m_decayingCoef);
      }
      else {
        m_weight = remainingWeight / numFlows;
        m_lastUpdateWeight = Simulator::Now ();
      }
    }
    else {
      m_weight = std::min (std::exp2 (timeDiff.GetSeconds () * m_decayingCoef) * m_weight, 1.);
      m_lastUpdateWeight = Simulator::Now ();
    }
  } 
  else {
    NS_FATAL_ERROR ("Unknown decaying function");
  }
  NS_LOG_INFO (Simulator::Now () << " index " << m_index << 
    " m_classIndex " << m_classIndex << 
    " remainingWeight " << remainingWeight << " numFlows " << numFlows <<
    " weight " << m_weight);
}

double_t
AutoFlow::GetFlowWeight (void)
{
  NS_LOG_FUNCTION (this);
  return m_weight;
}

} // namespace ns3