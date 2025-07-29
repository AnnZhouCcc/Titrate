#include "ns3/log.h"
#include "wfq-queue-disc.h"
#include "ns3/ipv4-queue-disc-item.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("WfqQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (WfqQueueDisc);

TypeId
WfqClass::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::WfqClass")
      .SetParent<Object> ()
      .SetGroupName ("TrafficControl")
      .AddConstructor<WfqClass> ()
    ;
    return tid;
}

WfqClass::WfqClass ()
{
    NS_LOG_FUNCTION (this);
}

TypeId
WfqQueueDisc::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::WfqQueueDisc")
      .SetParent<Object> ()
      .SetGroupName ("TrafficControl")
      .AddConstructor<WfqQueueDisc> ()
    ;
    return tid;
}

WfqQueueDisc::WfqQueueDisc ()
{
    NS_LOG_FUNCTION (this);
}

WfqQueueDisc::~WfqQueueDisc ()
{
    NS_LOG_FUNCTION (this);
}

void
WfqQueueDisc::AddWfqClass (Ptr<QueueDisc> qdisc, int32_t cl, uint32_t weight)
{
    WfqQueueDisc::AddWfqClass (qdisc, cl, 0, weight);
}

void
WfqQueueDisc::AddWfqClass (Ptr<QueueDisc> qdisc, int32_t cl, uint32_t priority, uint32_t weight)
{
    Ptr<WfqClass> wfqClass = CreateObject<WfqClass> ();
    wfqClass->priority = priority;
    wfqClass->qdisc = qdisc;
    wfqClass->headFinTime = 0;
    wfqClass->lengthBytes = 0;
    wfqClass->weight = weight;
    m_Wfqs[cl] = wfqClass;
}

bool
WfqQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION (this << item);

    Ptr<WfqClass> wfqClass = 0;

    int32_t cl = Classify (item);

    std::map<int32_t, Ptr<WfqClass> >::iterator itr = m_Wfqs.find (cl);

    if (itr == m_Wfqs.end ())
    {
        NS_LOG_ERROR ("Cannot find class, dropping the packet");
        return false;
    }

    wfqClass = itr->second;

    NS_LOG_LOGIC ("Found class for the enqueued item: " << cl << " with priority: " << wfqClass->priority);

    Ptr<Ipv4QueueDiscItem> ipv4Item = DynamicCast<Ipv4QueueDiscItem> (item);
    if (ipv4Item == 0)
    {
        NS_LOG_ERROR ("Cannot convert to the Ipv4QueueDiscItem");
        return false;
    }

    if (!wfqClass->qdisc->Enqueue (item))
    {
        DropBeforeEnqueue (item, LIMIT_EXCEEDED_DROP);
        return false;
    }

    uint32_t length = ipv4Item->GetPacket ()->GetSize ();

    uint64_t virtualTime = 0;
    std::map<uint32_t, uint64_t>::iterator itr2 = m_virtualTime.find (wfqClass->priority);
    if (itr2 != m_virtualTime.end ())
    {
        virtualTime = itr2->second;
    }

    if (wfqClass->qdisc->GetNPackets () == 1)
    {
        wfqClass->headFinTime = length / wfqClass->weight + virtualTime;
        m_virtualTime[wfqClass->priority] = wfqClass->headFinTime;
    }

    wfqClass->lengthBytes += length;

    return true;
}

Ptr<QueueDiscItem>
WfqQueueDisc::DoDequeue (void)
{
    NS_LOG_FUNCTION (this);

    int32_t highestPriority = -1;
    std::map<int32_t, Ptr<WfqClass> >::const_iterator itr = m_Wfqs.begin ();

    // Strict priority scheduling
    for (; itr != m_Wfqs.end (); ++itr)
    {
        Ptr<WfqClass> wfqClass = itr->second;
        if (static_cast<int32_t> (wfqClass->priority) > highestPriority
                && wfqClass->lengthBytes > 0)
        {
            highestPriority = static_cast<int32_t> (wfqClass->priority);
        }
    }

    if (highestPriority == -1)
    {
        NS_LOG_LOGIC ("Cannot find active queue");
        return 0;
    }

    // Find the smallest head finish time
    uint64_t smallestHeadFinTime = 0;
    Ptr<WfqClass> wfqClassToDequeue = 0;

    itr = m_Wfqs.begin ();
    for (; itr != m_Wfqs.end (); ++itr)
    {
        Ptr<WfqClass> wfqClass = itr->second;
        if (static_cast<int32_t> (wfqClass->priority) != highestPriority
                || wfqClass->lengthBytes == 0)
        {
            continue;
        }
        if (wfqClassToDequeue == 0 || wfqClass->headFinTime < smallestHeadFinTime)
        {
            wfqClassToDequeue = wfqClass;
            smallestHeadFinTime = wfqClass->headFinTime;
        }
    }

    Ptr<const QueueDiscItem> item = wfqClassToDequeue->qdisc->Peek ();

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

    Ptr<QueueDiscItem> retItem = wfqClassToDequeue->qdisc->Dequeue ();

    if (retItem == 0)
    {
        NS_LOG_ERROR ("Cannot dequeue from the internal queue disc");
        return 0;
    }

    wfqClassToDequeue->lengthBytes -= ipv4Item->GetPacket ()->GetSize ();

    if (wfqClassToDequeue->lengthBytes > 0)
    {
        Ptr<const QueueDiscItem> nextItem = wfqClassToDequeue->qdisc->Peek ();
        Ptr<const Ipv4QueueDiscItem> ipv4NextItem = DynamicCast<const Ipv4QueueDiscItem> (nextItem);
        uint32_t nextLength = ipv4NextItem->GetPacket ()->GetSize ();
        wfqClassToDequeue->headFinTime += nextLength / wfqClassToDequeue->weight;

        uint64_t virtualTime = 0;
        std::map<uint32_t, uint64_t>::iterator itr2 = m_virtualTime.find (wfqClassToDequeue->priority);
        if (itr2 != m_virtualTime.end ())
        {
            virtualTime = itr2->second;
        }

        if (virtualTime < wfqClassToDequeue->headFinTime)
        {
            m_virtualTime[wfqClassToDequeue->priority] = wfqClassToDequeue->headFinTime;
        }
    }

    return retItem;
}

Ptr<const QueueDiscItem>
WfqQueueDisc::DoPeek (void) const
{
    NS_LOG_FUNCTION (this);

    int32_t highestPriority = -1;
    std::map<int32_t, Ptr<WfqClass> >::const_iterator itr = m_Wfqs.begin ();

    // Strict priority scheduling
    for (; itr != m_Wfqs.end (); ++itr)
    {
        Ptr<WfqClass> wfqClass = itr->second;
        if (static_cast<int32_t> (wfqClass->priority) > highestPriority
                && wfqClass->lengthBytes > 0)
        {
            highestPriority = static_cast<int32_t> (wfqClass->priority);
        }
    }

    if (highestPriority == -1)
    {
        NS_LOG_LOGIC ("Cannot find active queue");
        return 0;
    }

    // Find the smallest head finish time
    uint64_t smallestHeadFinTime = 0;
    Ptr<WfqClass> wfqClassToDequeue = 0;

    itr = m_Wfqs.begin ();
    for (; itr != m_Wfqs.end (); ++itr)
    {
        Ptr<WfqClass> wfqClass = itr->second;
        if (static_cast<int32_t> (wfqClass->priority) != highestPriority
                || wfqClass->lengthBytes == 0)
        {
            continue;
        }
        if (wfqClassToDequeue == 0 || wfqClass->headFinTime < smallestHeadFinTime)
        {
            wfqClassToDequeue = wfqClass;
            smallestHeadFinTime = wfqClass->headFinTime;
        }
    }

    return wfqClassToDequeue->qdisc->Peek ();
}

bool
WfqQueueDisc::CheckConfig (void)
{
    NS_LOG_FUNCTION (this);
    return true;
}

void
WfqQueueDisc::InitializeParams (void)
{
    NS_LOG_FUNCTION (this);

    std::map<int32_t, Ptr<WfqClass> >::iterator itr = m_Wfqs.begin ();
    for ( ; itr != m_Wfqs.end (); ++itr)
    {
        itr->second->qdisc->Initialize ();
    }

}


}
