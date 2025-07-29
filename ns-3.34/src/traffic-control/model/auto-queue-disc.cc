#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "auto-queue-disc.h"
#include "drop-head-queue-disc.h"
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("AutoQueueDisc");
NS_OBJECT_ENSURE_REGISTERED (AutoQueueDisc);

TypeId
AutoQueueDisc::AutoClass::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::AutoClass")
    .SetParent<Object> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<AutoClass> ()
  ;
  return tid;
}

AutoQueueDisc::AutoClass::AutoClass ()
: m_lastRoundMoved (0)
, m_flowWeights (0)
{
  NS_LOG_FUNCTION (this);
}

AutoQueueDisc::AutoClass::AutoClass (uint32_t index)
: m_lastRoundMoved (0)
, m_index (index)
, m_flowWeights (0)
{
  NS_LOG_FUNCTION (this);
}

Ptr<AutoFlow>
AutoQueueDisc::AutoClass::FindFlow (uint32_t h)
{
  NS_LOG_FUNCTION (this << h);
  for (auto it = m_flowList.begin (); it != m_flowList.end (); it++) {
    if ((*it)->GetIndex () == h) {
      return (*it);
    }
  }
  return NULL;
}

Ptr<AutoFlow>
AutoQueueDisc::AutoClass::GetFrontFlow (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<QueueDiscItem> item;
  Time earlyTimestamp = Seconds (1e6);
  Ptr<AutoFlow> earlyFlow = NULL;
  for (auto it = m_flowList.begin (); it != m_flowList.end (); it++) {
    if (earlyTimestamp > (*it)->GetHeadTimestamp ()) {
      earlyTimestamp = (*it)->GetHeadTimestamp ();
      earlyFlow = (*it);
    }
  }
  return earlyFlow;
}

uint32_t
AutoQueueDisc::AutoClass::GetNPackets (void)
{
  NS_LOG_FUNCTION (this);

  uint32_t nPackets = 0;
  for (auto it = m_flowList.begin (); it != m_flowList.end (); it++) {
    nPackets += (*it)->GetQueueDisc ()->GetNPackets ();
  }
  return nPackets;
}

uint32_t
AutoQueueDisc::AutoClass::GetMaxPackets (void)
{
  NS_LOG_FUNCTION (this);

  return m_maxPackets;
}

QueueDisc::Stats
AutoQueueDisc::AutoClass::GetStats (void)
{
  NS_LOG_FUNCTION (this);
  m_classStats = QueueDisc::Stats ();
  for (auto it = m_flowList.begin (); it != m_flowList.end (); it++) {
    m_classStats += (*it)->GetQueueDisc ()->GetStats ();
  }
  return m_classStats;
}

void
AutoQueueDisc::AutoClass::AddFlow (Ptr<AutoFlow> flow)
{
  NS_LOG_FUNCTION (this << flow);
  m_flowList.push_back (flow);
}

void
AutoQueueDisc::AutoClass::RemoveFlow (Ptr<AutoFlow> flow)
{
  NS_LOG_FUNCTION (this << flow);
  m_flowList.erase(std::remove(m_flowList.begin(), m_flowList.end(), flow), m_flowList.end());
}

uint32_t
AutoQueueDisc::AutoClass::GetNFlows (void)
{
  NS_LOG_FUNCTION (this);
  auto it = m_flowList.begin ();
  while ( it != m_flowList.end ()) {
    if (Simulator::Now () - (*it)->GetLastUsed () > Seconds (1.)) {
      it = m_flowList.erase (it);
    }
    else
      it++;
  }
  return m_flowList.size ();
}

void
AutoQueueDisc::AutoClass::FillDeficit (void)
{
  NS_LOG_FUNCTION (this);
  m_deficit += m_flowWeights * m_prioWeights * m_quantum;
  NS_LOG_FUNCTION (Simulator::Now () << " class " << m_index << 
    " m_flowWeights " << m_flowWeights <<
    " m_prioWeights " << m_prioWeights <<
    " deficit " << m_deficit);
}

void
AutoQueueDisc::AutoClass::UpdateClassWeights (double_t remainingWeight)
{
  NS_LOG_FUNCTION (this);
  double_t weight = 0.0;
  GetNFlows ();
  for (auto flow : m_flowList) {
    if (Simulator::Now () - flow->GetLastUsed () < Seconds (.1) || flow->GetQueueDisc ()->GetNPackets () > 0) {
      if (m_index == 0)
        flow->UpdateFlowWeight (remainingWeight, GetNFlows ()); 
      weight += flow->GetFlowWeight ();
    }
  }
  m_flowWeights = std::max (1., weight);
  if (Simulator::Now ().GetMilliSeconds () % 10 == 0) {
    NS_LOG_INFO (Simulator::Now () << " Index " << m_index << " flowWeights " << m_flowWeights);
  }
}


TypeId
AutoQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::AutoQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<AutoQueueDisc> ()
    .AddAttribute ("MaxSize",
           "The max queue size",
           QueueSizeValue (QueueSize ("1000p")),
           MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                  &QueueDisc::GetMaxSize),
           MakeQueueSizeChecker ())
    .AddAttribute ("NumClass",
           "The number of classes for DiffServ in this queue disc.",
           UintegerValue (4),
           MakeUintegerAccessor (&AutoQueueDisc::m_numClass),
           MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("BaseQuantum",
           "The quantum for the flow with the lowest priority.",
           UintegerValue (1500),
           MakeUintegerAccessor (&AutoQueueDisc::m_baseQuantum),
           MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("PrioRatio",
           "The ratio of quantum over different classes.",
           DoubleValue (2.),
           MakeDoubleAccessor (&AutoQueueDisc::m_prioRatio),
           MakeDoubleChecker<double_t> ())
    .AddAttribute ("DequeueDiffThres",
            "The threshold of the difference between one flow's share and the fair share",
            DoubleValue (0.2),
            MakeDoubleAccessor (&AutoQueueDisc::m_dequeueDiffThres),
            MakeDoubleChecker<double_t> ())
    .AddAttribute ("OccupyDiffThres",
            "The threshold of the difference between one flow's share and the fair share",
            DoubleValue (0.1),
            MakeDoubleAccessor (&AutoQueueDisc::m_occupyDiffThres),
            MakeDoubleChecker<double_t> ())
    .AddAttribute ("OccupyMinPktsThres",
            "The threshold of the minimum number of packets in one flow",
            UintegerValue (5),
            MakeUintegerAccessor (&AutoQueueDisc::m_occupyMinPktsThres),
            MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("AutoClassify",
           "Whether classify flows automatically or not.",
           BooleanValue (true),
           MakeBooleanAccessor (&AutoQueueDisc::m_autoClassify),
           MakeBooleanChecker ())
    .AddAttribute ("UpdateInterval",
           "The interval to update priority for each flow.",
           TimeValue (Seconds (.1)),
           MakeTimeAccessor (&AutoQueueDisc::m_updateInterval),
           MakeTimeChecker ())
    .AddAttribute ("Flows",
           "The number of queues into which the incoming packets are classified",
           UintegerValue (1024),
           MakeUintegerAccessor (&AutoQueueDisc::m_flows),
           MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Perturbation",
           "The salt used as an additional input to the hash function used to classify packets",
           UintegerValue (0),
           MakeUintegerAccessor (&AutoQueueDisc::m_perturbation),
           MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}

AutoQueueDisc::AutoQueueDisc ()
: m_lastUpdated (Seconds (0))
{
  NS_LOG_FUNCTION (this);
}

AutoQueueDisc::~AutoQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

void
AutoQueueDisc::AddAutoClass (Ptr<AutoClass> autoClass, uint32_t quantum, double_t weight, uint32_t maxSize)
{
  autoClass->m_priority = 0;
  autoClass->m_quantum = quantum;
  autoClass->m_prioWeights = weight;
  autoClass->m_deficit = 0;
  autoClass->m_maxPackets = maxSize;
  m_autoClasses.push_back (autoClass);
}

const uint32_t AutoQueueDisc::prio2band[16] = {1, 2, 2, 2, 1, 2, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};

bool
AutoQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  uint32_t cl;
  bool retval = false;
  Ptr<AutoClass> curClass = NULL;
  Ptr<AutoFlow> curFlow = NULL;

  uint32_t flowHash, h;
  flowHash = item->Hash (m_perturbation);
  h = flowHash % m_flows;

  for (uint32_t classIndex = 0; classIndex <= m_numClass; classIndex++) {
    curClass = m_autoClasses[classIndex];
    curFlow = curClass->FindFlow (h);
    if (curFlow) {
      break;
    }
  }

  if (!curFlow) {
    if (m_autoClassify) {
      curClass = m_autoClasses[0];
    }
    else {
      uint8_t tosByte = 0, priority = 0;
      if (item->GetUint8Value (QueueItem::IP_DSFIELD, tosByte)){
        priority = Socket::IpTos2Priority (tosByte >> 2);
      }
      cl = prio2band[priority & 0x0f];
    
      if (cl >= m_autoClasses.size ()) {
        NS_LOG_ERROR ("Cannot find class, dropping the packet");
        return false;
      }

      curClass = m_autoClasses[cl];

      NS_LOG_LOGIC ("Found class for the enqueued item: " << cl << " with priority: " << curClass->m_priority);
    }
    
    curFlow = m_flowFactory.Create<AutoFlow> ();
    Ptr<QueueDisc> qd = m_queueDiscFactory.Create<QueueDisc> ();
    qd->Initialize ();
    curFlow->SetQueueDisc (qd);
    curFlow->SetIndex (h);
    curFlow->SetClassIndex (curClass->m_index);
    curFlow->UpdateQueueStats (1, curClass->GetNFlows () + 1); // this is used to initialize the allStats
    AddQueueDiscClass (curFlow);
    curClass->AddFlow (curFlow);
  }
  else {
    if (m_autoClassify) {
      UpdateFlowClassification ();
      curClass = m_autoClasses[curFlow->GetClassIndex ()];
    }
  }
    
  retval = curFlow->GetQueueDisc ()->Enqueue (item);

  while (GetCurrentSize () > GetMaxSize ()) {
    /* Mimicking FqCoDelDrop method */
    /* 1. Find the fattest class, but based on its own control target */
    int32_t maxDiff = 0;
    int32_t maxIndex = -1;
    for (uint32_t curIndex = 0; curIndex <= m_numClass; curIndex++) {
      auto curClass = m_autoClasses[curIndex];
      int32_t curDiff = int32_t(curClass->GetNPackets ()) - int32_t(GetMaxSize ().GetValue () >> (m_numClass + 1 - curIndex));
      if (curDiff > maxDiff) {
        maxDiff = curDiff;
        maxIndex = curIndex;
      }
    }
    NS_LOG_LOGIC ("Queue full -- dropping head pkt");
    Ptr<QueueDiscItem> discardItem;
    Ptr<AutoFlow> autoFlow = m_autoClasses[maxIndex]->GetFrontFlow ();
    discardItem = autoFlow->GetQueueDisc ()->Dequeue ();
    DropAfterDequeue (discardItem, LIMIT_EXCEEDED_DROP);
  }

  curFlow->UpdateAtEnqueue (item);

  if (retval && curClass->GetNPackets () == 1) {
    m_active[curClass->m_priority].push_back (curClass);
    curClass->FillDeficit ();
  }

  return retval;
}

Ptr<QueueDiscItem>
AutoQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<const QueueDiscItem> item = 0;

  if (m_active.empty ())
  {
    NS_LOG_LOGIC ("Active map is empty");
    return 0;
  }

  int32_t highestPriority = -1;
  std::map<uint32_t, std::list<Ptr<AutoClass> > >::const_iterator itr = m_active.begin ();
  for (; itr != m_active.end (); ++itr) {
    if (static_cast<int32_t>(itr->first) > highestPriority
        && !(itr->second).empty ()) {
      highestPriority = static_cast<int32_t> (itr->first);
    }
  }

  if (highestPriority == -1) {
    NS_LOG_LOGIC ("Cannot find active queue");
    return 0;
  }

  while (!m_active[highestPriority].empty ()) {
    Ptr<AutoClass> curClass = m_active[highestPriority].front ();
    Ptr<AutoFlow> autoFlow = curClass->GetFrontFlow ();
    if (!autoFlow) {
      m_active[highestPriority].pop_front ();
      continue;
    }

    item = autoFlow->GetQueueDisc ()->Peek ();
    if (item == 0) {
      NS_ABORT_MSG ("Cannot peek from the internal queue disc");
      return 0;
    }

    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem> (item);
    if (ipv4Item == 0) {
      NS_ABORT_MSG ("Cannot convert to the Ipv4QueueDiscItem");
      return 0;
    }

    uint32_t length = ipv4Item->GetPacket ()->GetSize ();

    double_t remainingWeight = 0;
    for (auto autoClass : m_autoClasses) {
      if (autoClass->m_index > 0 && autoClass->GetNFlows () > 0)
        remainingWeight += autoClass->m_flowWeights;
    }
    curClass->UpdateClassWeights (remainingWeight);

    if (length <= curClass->m_deficit) {
      curClass->m_deficit -= length;
      Ptr<QueueDiscItem> retItem = autoFlow->GetQueueDisc ()->Dequeue ();
      if (retItem == 0) {
        return 0;
      }
      if (curClass->GetNPackets () == 0) {
        m_active[highestPriority].pop_front ();
      }
      return retItem;
    }

    curClass->FillDeficit ();
    m_active[highestPriority].pop_front ();
    m_active[highestPriority].push_back (curClass);
  }
  return 0;
}

Ptr<const QueueDiscItem>
AutoQueueDisc::DoPeek (void) const
{
  NS_LOG_FUNCTION (this);

  if (m_active.empty ()) {
    NS_LOG_LOGIC ("Active map is empty");
    return 0;
  }

  int32_t highestPriority = -1;
  std::map<uint32_t, std::list<Ptr<AutoClass> > >::const_iterator itr = m_active.begin ();
  for (; itr != m_active.end (); ++itr) {
    if (static_cast<int32_t>(itr->first) >= highestPriority
        && !(itr->second).empty ()) {
      highestPriority = static_cast<int32_t>(itr->first);
    }
  }

  if (highestPriority == -1) {
    NS_LOG_LOGIC ("Cannot find active queue");
    return 0;
  }

  Ptr<AutoClass> autoClass = m_active.at (highestPriority).front ();
  Ptr<AutoFlow> autoFlow = autoClass->GetFrontFlow ();

  return autoFlow->GetQueueDisc ()->Peek ();
}

void
AutoQueueDisc::UpdateFlowClassification (void)
{
  NS_LOG_FUNCTION (this);

  if (Simulator::Now () - m_lastUpdated < m_updateInterval) 
    return;

  std::map<Ptr<AutoFlow>, std::pair<uint32_t, uint32_t> > updateResult;

  /* update the new flow class */
  m_autoClasses[0]->GetNFlows ();  // erase outdated flows
  
  uint32_t curClassQueueLen = 0;
  for (auto autoFlow : m_autoClasses[0]->m_flowList) {
    autoFlow->MeasureQueueLen ();
    curClassQueueLen += autoFlow->GetAvgQueueLen ();
  }

  for (auto autoFlow : m_autoClasses[0]->m_flowList) {      
    int16_t newClass = std::max ((double_t) 1, m_numClass + 1.999 - std::log2 (double_t(GetMaxSize ().GetValue ()) / curClassQueueLen));

    autoFlow->UpdateQueueStats (curClassQueueLen, m_autoClasses[0]->GetNFlows ());
    if (autoFlow->GetFlowWeight () >= 1.) {
      /* Time to move the flow out */
      /* 1. Roughly decide the class by the queueLen */

      /* Check if the current flow is outstandingly above or below the fair share */
      double_t occupancyRatioFairDiff = autoFlow->GetOccupancyRatioFairDiff ();
      int32_t occupancyFairDiff = autoFlow->GetOccupancyFairDiff ();
        
      if ((occupancyRatioFairDiff > m_occupyDiffThres && occupancyFairDiff > GetMaxSize ().GetValue () >> (m_numClass + 2))) {
        newClass = std::min ((int) m_numClass, newClass + 1);
      }
      else if ((occupancyRatioFairDiff < -m_occupyDiffThres && -occupancyFairDiff > GetMaxSize ().GetValue () >> (m_numClass + 2))) {
        newClass = std::max (1, newClass - 1);
      }
      updateResult[autoFlow] = std::make_pair (0, (uint16_t) newClass);
      NS_LOG_INFO (Simulator::Now () << " Flow " << autoFlow->GetIndex () << 
        " is moved from class 0 to class " << newClass << 
        " class queue length " << curClassQueueLen <<
        " and occupancy of " << occupancyRatioFairDiff << " " << occupancyFairDiff);
    }
  }
  
  /* update classes for old flows */
  for (auto autoClass : m_autoClasses) {
    uint16_t oldClass = autoClass->m_index;
    int16_t newClass;
    if (oldClass == 0)
      continue;
    bool moveCurClass = false;
    autoClass->GetNFlows ();  // erase outdated flows

    uint32_t curClassQueueLen = 0;
    for (auto autoFlow : autoClass->m_flowList) {
      autoFlow->MeasureQueueLen ();
      curClassQueueLen += autoFlow->GetAvgQueueLen ();
    }

    for (auto autoFlow : autoClass->m_flowList) {
      newClass = (int16_t) oldClass;
      autoFlow->UpdateQueueStats (curClassQueueLen, autoClass->GetNFlows ());
      /* If the autoClass has flow changes in the last round, queue stats is inaccurate
         but this line still needs to be executed here to updateQueueStats for autoFlow */
      if (autoClass->m_lastRoundMoved)
        continue;
      double_t occupancyRatio = autoFlow->GetOccupancyRatio ();
      double_t occupancyRatioFairDiff = autoFlow->GetOccupancyRatioFairDiff ();
      int32_t occupancyFairDiff = autoFlow->GetOccupancyFairDiff ();
        
      if ((occupancyRatioFairDiff > m_occupyDiffThres && occupancyFairDiff > GetMaxSize ().GetValue () >> (m_numClass + 2))) {
        newClass = std::min ((int) m_numClass, newClass + 1);
      }
      else if ((occupancyRatioFairDiff < -m_occupyDiffThres && -occupancyFairDiff > GetMaxSize ().GetValue () >> (m_numClass + 2))) {
        newClass = std::max (1, newClass - 1);
      }
      
      if (newClass != (int16_t) oldClass) {
        updateResult[autoFlow] = std::make_pair (oldClass, (uint16_t) newClass);
        moveCurClass = true;
        NS_LOG_INFO (Simulator::Now () << " Flow " << autoFlow->GetIndex () << 
          " is moved from class " << oldClass << " to class " << newClass << 
          " class queue length " << curClassQueueLen <<
          " and occupancy of " << occupancyRatio << " " << occupancyRatioFairDiff << " " << occupancyFairDiff);
      }
    }
    /* If no flow is going to be moved in a certain class (relatively fair), 
       but the queueLen of autoClass is too high or too low, 
       move the entire class */
    if (!moveCurClass && !autoClass->m_lastRoundMoved) {
      newClass = (int16_t) oldClass;
      if (curClassQueueLen < GetMaxSize ().GetValue () >> (m_numClass - oldClass + 3)) {
        newClass = std::max (1, newClass - 1);
      }
      else if (curClassQueueLen > GetMaxSize ().GetValue () >> (m_numClass - oldClass + 1)) {
        newClass = std::min ((int) m_numClass, newClass + 1);
      }
      if (newClass != (int16_t) oldClass) {
        for (auto autoFlow : autoClass->m_flowList) {
          if (autoFlow->GetFlowWeight () >= 1. && 
            (newClass - (int16_t) oldClass) * autoFlow->GetOccupancyRatioFairDiff () > 0) {
            updateResult[autoFlow] = std::make_pair (oldClass, (uint16_t) newClass);
            NS_LOG_INFO (Simulator::Now () << " Flow " << autoFlow->GetIndex () << 
            " is moved from class " << oldClass << " to class " << newClass << 
            " class queue length " << curClassQueueLen << " due to queueLen" <<
            " but with a occupyRatio of " << autoFlow->GetOccupancyRatio ());
          }
        }
      }
    }
    
    autoClass->m_lastRoundMoved = false;
  }

  for (uint32_t classIndex = 0; classIndex <= m_numClass; classIndex++) {
    bool hasPromote = false;
    for (auto const& [flow, updates] : updateResult) {
      if (updates.first == classIndex && updates.second > classIndex) {
        hasPromote = true;
        break;
      }
    }
    if (hasPromote) {
      for (auto it = updateResult.begin (); it != updateResult.end ();) {
        if (it->second.first == classIndex && it->second.second < classIndex) {
          NS_LOG_INFO ("Erase movement from " << it->second.first << " to " << it->second.second);
          it = updateResult.erase (it);
        }
        else
          it++;
      }
    }
  }

  for (auto const& [flow, updates] : updateResult) {
    flow->SetClassIndex (updates.second);
    Ptr<AutoClass> oldClass = m_autoClasses[updates.first];
    Ptr<AutoClass> newClass = m_autoClasses[updates.second];
    oldClass->RemoveFlow (flow);
    newClass->AddFlow (flow);
    oldClass->m_lastRoundMoved = true;
    newClass->m_lastRoundMoved = true;
    if (std::find(m_active[newClass->m_priority].begin (),
        m_active[newClass->m_priority].end (), newClass) == m_active[newClass->m_priority].end ()) {
      m_active[newClass->m_priority].push_back (newClass);
    }
  }
   
  m_lastUpdated = Simulator::Now ();
}

uint32_t
AutoQueueDisc::GetNumClasses (void) const
{
  NS_LOG_FUNCTION (this);
  return m_numClass;
}

double_t
AutoQueueDisc::GetClassNPackets (size_t index)
{
  NS_LOG_FUNCTION (this << index);
  return (double_t) m_autoClasses[index]->GetNPackets ();
}

bool
AutoQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  double_t weight;
  if (m_prioRatio >= 1) 
    weight = std::pow(m_prioRatio, m_numClass);
  else
    weight = 1.;
  
  for (uint32_t autoClass = 0; autoClass <= m_numClass; autoClass++) {
    AddAutoClass (Create<AutoClass> (autoClass), m_baseQuantum, weight, GetMaxSize ().GetValue () / (m_numClass + 1));
    weight /= m_prioRatio;
  }

  return true;
}

void
AutoQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);

  m_flowFactory.SetTypeId ("ns3::AutoFlow");
  
  m_queueDiscFactory.SetTypeId ("ns3::DropHeadQueueDisc");
  m_queueDiscFactory.Set ("MaxSize", QueueSizeValue (GetMaxSize ()));
}


}
