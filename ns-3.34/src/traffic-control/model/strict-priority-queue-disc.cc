/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007, 2014 University of Washington
 *               2015 Universita' degli Studi di Napoli Federico II
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:  Stefano Avallone <stavallo@unina.it>
 *           Tom Henderson <tomhend@u.washington.edu>
 */

#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/queue.h"
#include "ns3/socket.h"
#include "strict-priority-queue-disc.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("StrictPriorityQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (StrictPriorityQueueDisc);

TypeId StrictPriorityQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::StrictPriorityQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<StrictPriorityQueueDisc> ()
    .AddAttribute ("MaxSize",
                   "The maximum number of packets accepted by this queue disc.",
                   QueueSizeValue (QueueSize ("1000p")),
                   MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                   MakeQueueSizeChecker ())
  ;
  return tid;
}

StrictPriorityQueueDisc::StrictPriorityQueueDisc ()
  : QueueDisc (QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS)
{
  NS_LOG_FUNCTION (this);
}

StrictPriorityQueueDisc::~StrictPriorityQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

const uint32_t StrictPriorityQueueDisc::prio2band[16] = {1, 2, 2, 2, 1, 2, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};

bool
StrictPriorityQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  uint8_t tosByte = 0, priority = 0;
  if (item->GetUint8Value (QueueItem::IP_DSFIELD, tosByte)) 
    {
      priority = Socket::IpTos2Priority (tosByte >> 2);
    }

  uint32_t band = prio2band[priority & 0x0f];

  bool retval = GetInternalQueue (band)->Enqueue (item);

  // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called by the
  // internal queue because QueueDisc::AddInternalQueue sets the trace callback

  if (!retval)
    {
      NS_LOG_WARN ("Packet enqueue failed. Check the size of the internal queues");
    }

  NS_LOG_LOGIC ("Number packets band " << band << ": " << GetInternalQueue (band)->GetNPackets ());
  
  if (GetCurrentSize () > GetMaxSize ())
    {
      NS_LOG_LOGIC ("Overload; drop from low priority queue");
      StrictPriorityDropHead (band);
    }

  return retval;
}

Ptr<QueueDiscItem>
StrictPriorityQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<QueueDiscItem> item;

  for (uint32_t i = 0; i < GetNInternalQueues (); i++)
    {
      if ((item = GetInternalQueue (i)->Dequeue ()) != 0)
        {
          NS_LOG_LOGIC ("Popped from band " << i << ": " << item);
          NS_LOG_LOGIC ("Number packets band " << i << ": " << GetInternalQueue (i)->GetNPackets ());
          return item;
        }
    }
  
  NS_LOG_LOGIC ("Queue empty");
  return item;
}

Ptr<const QueueDiscItem>
StrictPriorityQueueDisc::DoPeek (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<const QueueDiscItem> item;

  for (uint32_t i = 0; i < GetNInternalQueues (); i++)
    {
      if ((item = GetInternalQueue (i)->Peek ()) != 0)
        {
          NS_LOG_LOGIC ("Peeked from band " << i << ": " << item);
          NS_LOG_LOGIC ("Number packets band " << i << ": " << GetInternalQueue (i)->GetNPackets ());
          return item;
        }
    }

  NS_LOG_LOGIC ("Queue empty");
  return item;
}

bool
StrictPriorityQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("StrictPriorityQueueDisc cannot have classes");
      return false;
    }

  if (GetNPacketFilters () != 0)
    {
      NS_LOG_ERROR ("StrictPriorityQueueDisc needs no packet filter");
      return false;
    }

  if (GetNInternalQueues () == 0)
    {
      QueueSize internalQueueSize;
      if (GetMaxSize ().GetUnit () == QueueSizeUnit::PACKETS)
        {
          internalQueueSize = QueueSize (GetMaxSize ().GetUnit (), GetMaxSize ().GetValue () + 1);
        }
      if (GetMaxSize ().GetUnit () == QueueSizeUnit::BYTES)
        {
          internalQueueSize = QueueSize (GetMaxSize ().GetUnit (), GetMaxSize ().GetValue () + 1500);
        }
      // create 3 DropTail queues with GetLimit() packets each
      ObjectFactory factory;
      factory.SetTypeId ("ns3::DropTailQueue<QueueDiscItem>");
      factory.Set ("MaxSize", QueueSizeValue (internalQueueSize));
      AddInternalQueue (factory.Create<InternalQueue> ());
      AddInternalQueue (factory.Create<InternalQueue> ());
      AddInternalQueue (factory.Create<InternalQueue> ());
    }

  if (GetNInternalQueues () != 3)
    {
      NS_LOG_ERROR ("StrictPriorityQueueDisc needs 3 internal queues");
      return false;
    }

  if (GetInternalQueue (0)-> GetMaxSize ().GetUnit () != QueueSizeUnit::PACKETS ||
      GetInternalQueue (1)-> GetMaxSize ().GetUnit () != QueueSizeUnit::PACKETS ||
      GetInternalQueue (2)-> GetMaxSize ().GetUnit () != QueueSizeUnit::PACKETS)
    {
      NS_LOG_ERROR ("StrictPriorityQueueDisc needs 3 internal queues operating in packet mode");
      return false;
    }

  for (uint8_t i = 0; i < 2; i++)
    {
      if (GetInternalQueue (i)->GetMaxSize () < GetMaxSize ())
        {
          NS_LOG_ERROR ("The capacity of some internal queue(s) is less than the queue disc capacity");
          return false;
        }
    }

  return true;
}

void
StrictPriorityQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);
}

uint32_t
StrictPriorityQueueDisc::StrictPriorityDropHead (uint32_t incomingBand)
{
  NS_LOG_FUNCTION (this);

  uint32_t band = 2;
  Ptr<QueueDiscItem> item;
  while (GetCurrentSize () > GetMaxSize ()) 
    {
      while (GetInternalQueue (band)->GetNPackets () == 0)
        {
          if (band == 0)
            NS_FATAL_ERROR ("No packets inside but overflows");
          else
            band -= 1;
        }
        
        NS_LOG_LOGIC ("Drop packet due to queue disc overflow");
        item = GetInternalQueue (band)->Dequeue ();
        DropAfterDequeue (item, LIMIT_EXCEEDED_DROP);
    } 
  return band;
}

} // namespace ns3
