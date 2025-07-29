#ifndef TCN_QUEUE_DISC_H
#define TCN_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include "ns3/nstime.h"

namespace ns3 {

class TCNQueueDisc : public QueueDisc
{
public:
    static TypeId GetTypeId (void);

    TCNQueueDisc ();

    virtual ~TCNQueueDisc ();

    bool MarkingECN (Ptr<QueueDiscItem> item);

    static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";  //!< Packet dropped due to queue disc limit exceeded

private:
    // Operations offered by multi queue disc should be the same as queue disc
    virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
    virtual Ptr<QueueDiscItem> DoDequeue (void);
    virtual Ptr<const QueueDiscItem> DoPeek (void) const;
    virtual bool CheckConfig (void);
    virtual void InitializeParams (void);

    Time m_threshold;
};

}

#endif
