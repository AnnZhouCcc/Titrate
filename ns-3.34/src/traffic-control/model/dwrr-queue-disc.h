#ifndef DWRR_QUEUE_DISC_H
#define DWRR_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include "ns3/queue.h"
#include <list>

namespace ns3 {

class DwrrClass : public QueueDiscClass
{
public:

    static TypeId GetTypeId (void);

    DwrrClass ();

    uint32_t priority;
    uint32_t quantum;
    uint32_t deficit;
};

class DwrrQueueDisc : public QueueDisc
{
public:

    static TypeId GetTypeId (void);

    DwrrQueueDisc ();

    virtual ~DwrrQueueDisc ();

    void AddDwrrClass (Ptr<QueueDisc> queue, uint32_t quantum);

    static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";  //!< Packet dropped due to queue disc limit exceeded
    static constexpr const char* UNCLASSIFIED_DROP = "Unknown classes";  //!< Packet dropped due to queue disc limit exceeded

private:
    static const uint32_t prio2band[16];

    // Operations offered by multi queue disc should be the same as queue disc
    virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
    virtual Ptr<QueueDiscItem> DoDequeue (void);
    virtual Ptr<const QueueDiscItem> DoPeek (void) const;
    virtual bool CheckConfig (void);
    virtual void InitializeParams (void);

    // The internal DWRR queue discs are first organized in a map
    // with the priority as key and then in a linked list if they are
    // with the same priority
    std::map<uint32_t, std::list<Ptr<DwrrClass> > > m_active;

    uint16_t m_numClass;
    uint32_t m_baseQuantum;
    double_t m_prioRatio;
};

} // namespace ns3

#endif
