#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/socket.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "dwrr-queue-disc.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DwrrQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (DwrrQueueDisc);

TypeId
DwrrClass::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::DwrrClass")
      .SetParent<Object> ()
      .SetGroupName ("TrafficControl")
      .AddConstructor<DwrrClass> ()
    ;
    return tid;
}

DwrrClass::DwrrClass ()
{
    NS_LOG_FUNCTION (this);
}

TypeId
DwrrQueueDisc::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::DwrrQueueDisc")
      .SetParent<Object> ()
      .SetGroupName ("TrafficControl")
      .AddConstructor<DwrrQueueDisc> ()
      .AddAttribute ("MaxSize",
                     "The max queue size",
                     QueueSizeValue (QueueSize ("1000p")),
                     MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                            &QueueDisc::GetMaxSize),
                     MakeQueueSizeChecker ())
      .AddAttribute ("NumClass",
                     "The number of classes for DiffServ in this queue disc.",
                     UintegerValue (4),
                     MakeUintegerAccessor (&DwrrQueueDisc::m_numClass),
                     MakeUintegerChecker<uint16_t> ())
      .AddAttribute ("BaseQuantum",
                     "The quantum for the flow with the lowest priority.",
                     UintegerValue (1500),
                     MakeUintegerAccessor (&DwrrQueueDisc::m_baseQuantum),
                     MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("PrioRatio",
                     "The ratio of quantum over different classes.",
                     DoubleValue (2.),
                     MakeDoubleAccessor (&DwrrQueueDisc::m_prioRatio),
                     MakeDoubleChecker<double_t> ())
    ;
    return tid;
}

DwrrQueueDisc::DwrrQueueDisc ()
{
    NS_LOG_FUNCTION (this);
}

DwrrQueueDisc::~DwrrQueueDisc ()
{
    NS_LOG_FUNCTION (this);
}

void
DwrrQueueDisc::AddDwrrClass (Ptr<QueueDisc> qdisc, uint32_t quantum)
{
    Ptr<DwrrClass> dwrrClass = CreateObject<DwrrClass> ();
    dwrrClass->SetQueueDisc (qdisc);
    dwrrClass->priority = 0;
    dwrrClass->quantum = quantum;
    dwrrClass->deficit = 0;
    QueueDisc::AddQueueDiscClass (dwrrClass);
}

const uint32_t DwrrQueueDisc::prio2band[16] = {1, 2, 2, 2, 1, 2, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};

bool
DwrrQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION (this << item);
    uint8_t tosByte = 0, priority = 0;
    if (item->GetUint8Value (QueueItem::IP_DSFIELD, tosByte)) 
    {
        priority = Socket::IpTos2Priority (tosByte >> 2);
    }
    uint32_t cl = prio2band[priority & 0x0f];

    if (cl >= GetNQueueDiscClasses ())
    {
        DropBeforeEnqueue (item, UNCLASSIFIED_DROP);
        NS_LOG_ERROR ("Cannot find class, dropping the packet");
        return false;
    }

    Ptr<DwrrClass> dwrrClass = StaticCast <DwrrClass, QueueDiscClass> (GetQueueDiscClass (cl));

    NS_LOG_LOGIC ("Found class for the enqueued item: " << cl << " with priority: " << dwrrClass->priority);

    bool retval = dwrrClass->GetQueueDisc ()->Enqueue (item);

    if (retval && dwrrClass->GetQueueDisc ()->GetNPackets () == 1)
    {
        m_active[dwrrClass->priority].push_back (dwrrClass);
        dwrrClass->deficit = dwrrClass->quantum;
    }

    while (GetCurrentSize () > GetMaxSize ())
    {
        NS_LOG_LOGIC ("Queue disc limit exceeded -- dropping packet");
        uint32_t maxIndex = 0;
        uint32_t maxSize = 0;
        for (uint32_t dropCl = 0; dropCl < GetNQueueDiscClasses (); dropCl++)
        {
            Ptr<DwrrClass> dropDwrrClass = StaticCast <DwrrClass, QueueDiscClass> (GetQueueDiscClass (dropCl));
            if (dropDwrrClass->GetQueueDisc ()->GetNPackets () > maxSize)
            {
                maxSize = dropDwrrClass->GetQueueDisc ()->GetNPackets ();
                maxIndex = dropCl;
            }
        }
        Ptr<QueueDiscItem> discardItem = GetQueueDiscClass (maxIndex)->GetQueueDisc ()->Dequeue ();
        DropAfterDequeue (item, LIMIT_EXCEEDED_DROP);
    }

    return retval;
}

Ptr<QueueDiscItem>
DwrrQueueDisc::DoDequeue (void)
{
    NS_LOG_FUNCTION (this);

    Ptr<const QueueDiscItem> item = 0;

    if (m_active.empty ())
    {
        NS_LOG_LOGIC ("Active map is empty");
        return 0;
    }

    int32_t highestPriority = -1;
    std::map<uint32_t, std::list<Ptr<DwrrClass> > >::const_iterator itr = m_active.begin ();
    for (; itr != m_active.end (); ++itr)
    {
        if (static_cast<int32_t>(itr->first) > highestPriority
                && !(itr->second).empty ())
        {
            highestPriority = static_cast<int32_t> (itr->first);
        }
    }

    if (highestPriority == -1)
    {
        NS_LOG_LOGIC ("Cannot find active queue");
        return 0;
    }

    while (true)
    {
        Ptr<DwrrClass> dwrrClass = m_active[highestPriority].front ();

        item = dwrrClass->GetQueueDisc ()->Peek ();
        if (item == 0)
        {
            NS_LOG_LOGIC ("Cannot peek from the internal queue disc");
            return 0;
        }

        Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem> (item);
        if (ipv4Item == 0)
        {
            NS_LOG_ERROR ("Cannot convert to the Ipv4QueueDiscItem");
            return 0;
        }

        uint32_t length = ipv4Item->GetPacket ()->GetSize ();
        // std::cout << "pos 8 " << length << " " << dwrrClass->deficit << " " << dwrrClass->quantum << std::endl;

        if (length <= dwrrClass->deficit)
        {
            dwrrClass->deficit -= length;
            Ptr<QueueDiscItem> retItem = dwrrClass->GetQueueDisc ()->Dequeue ();
            if (retItem == 0)
            {
                return 0;
            }
            if (dwrrClass->GetQueueDisc ()->GetNPackets () == 0)
            {
                m_active[highestPriority].pop_front ();
            }
            return retItem;
        }

        dwrrClass->deficit += dwrrClass->quantum;
        m_active[highestPriority].pop_front ();
        m_active[highestPriority].push_back (dwrrClass);
    }

    return 0;
}

Ptr<const QueueDiscItem>
DwrrQueueDisc::DoPeek (void) const
{
    NS_LOG_FUNCTION (this);

    if (m_active.empty ())
    {
        NS_LOG_LOGIC ("Active map is empty");
        return 0;
    }

    int32_t highestPriority = -1;
    std::map<uint32_t, std::list<Ptr<DwrrClass> > >::const_iterator itr = m_active.begin ();
    for (; itr != m_active.end (); ++itr)
    {
        if (static_cast<int32_t>(itr->first) >= highestPriority
                && !(itr->second).empty ())
        {
            highestPriority = static_cast<int32_t>(itr->first);
        }
    }

    if (highestPriority == -1)
    {
        NS_LOG_LOGIC ("Cannot find active queue");
        return 0;
    }

    Ptr<DwrrClass> dwrrClass = m_active.at (highestPriority).front ();

    return dwrrClass->GetQueueDisc ()->Peek ();
}

bool
DwrrQueueDisc::CheckConfig (void)
{
    NS_LOG_FUNCTION (this);
    ObjectFactory factory;
    factory.SetTypeId ("ns3::FifoQueueDisc");
    factory.Set ("MaxSize", QueueSizeValue (GetMaxSize ()));
    uint32_t quantum;
    if (m_prioRatio >= 1) 
        quantum = m_baseQuantum * std::pow (m_prioRatio, m_numClass - 1);
    else
        quantum = m_baseQuantum;
    
    for (uint32_t dwrrClass = 0; dwrrClass < m_numClass; dwrrClass++) {
        AddDwrrClass (factory.Create<QueueDisc> (), quantum);
        quantum /= m_prioRatio;
    }

    return true;
}

void
DwrrQueueDisc::InitializeParams (void)
{
    NS_LOG_FUNCTION (this);
}


}
