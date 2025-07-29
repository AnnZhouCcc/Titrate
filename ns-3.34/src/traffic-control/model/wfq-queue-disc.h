#ifndef WFQ_QUEUE_DISC_H
#define WFQ_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include <list>

namespace ns3 {

class WfqClass : public Object
{
public:

    static TypeId GetTypeId (void);

    WfqClass ();

    uint32_t priority;

    Ptr<QueueDisc> qdisc;

    uint64_t headFinTime;
    uint32_t lengthBytes;
    uint32_t weight;
};

class WfqQueueDisc : public QueueDisc
{
public:

    static TypeId GetTypeId (void);

    WfqQueueDisc ();

    virtual ~WfqQueueDisc ();

    void AddWfqClass (Ptr<QueueDisc> qdisc, int32_t cl, uint32_t weight);
    void AddWfqClass (Ptr<QueueDisc> qdisc, int32_t cl, uint32_t priority, uint32_t weight);

    static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";  //!< Packet dropped due to queue disc limit exceeded

private:
    // Operations offered by multi queue disc should be the same as queue disc
    virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
    virtual Ptr<QueueDiscItem> DoDequeue (void);
    virtual Ptr<const QueueDiscItem> DoPeek (void) const;
    virtual bool CheckConfig (void);
    virtual void InitializeParams (void);

    std::map<int32_t, Ptr<WfqClass> > m_Wfqs;
    std::map<uint32_t, uint64_t> m_virtualTime;

};

} // namespace ns3

#endif
