/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 NITK Surathkal
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
 * Authors: Vivek Jain <jain.vivek.anand@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 */

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/simulator.h"
#include "ns3/abort.h"
#include "sfb-queue-disc.h"
#include "ns3/ipv4-packet-filter.h"
#include "ns3/drop-tail-queue.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SfbQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (SfbQueueDisc);

TypeId SfbQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SfbQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<SfbQueueDisc> ()
    .AddAttribute ("MaxSize",
                   "The maximum number of packets accepted by this queue disc",
                   QueueSizeValue (QueueSize ("1000p")),
                   MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                   MakeQueueSizeChecker ())
    .AddAttribute ("Perturbation",
                   "The salt used as an additional input to the hash function used to classify packets",
                   UintegerValue (0),
                   MakeUintegerAccessor (&SfbQueueDisc::m_perturbation),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MeanPktSize",
                   "Average of packet size",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&SfbQueueDisc::m_meanPktSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Increment",
                   "Pmark increment value",
                   DoubleValue (0.0005),
                   MakeDoubleAccessor (&SfbQueueDisc::m_increment),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("Decrement",
                   "Pmark decrement Value",
                   DoubleValue (0.00005),
                   MakeDoubleAccessor (&SfbQueueDisc::m_decrement),
                   MakeDoubleChecker<double> ())
  ;

  return tid;
}

SfbQueueDisc::SfbQueueDisc () :
  QueueDisc ()
{
  NS_LOG_FUNCTION (this);
  m_uv = CreateObject<UniformRandomVariable> ();
}

SfbQueueDisc::~SfbQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

void
SfbQueueDisc::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_uv = 0;
  QueueDisc::DoDispose ();
}

SfbQueueDisc::Stats
SfbQueueDisc::GetStats ()
{
  NS_LOG_FUNCTION (this);
  return m_stats;
}

int64_t
SfbQueueDisc::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uv->SetStream (stream);
  return 1;
}

void
SfbQueueDisc::InitializeParams (void)
{
  InitializeBins ();
  m_stats.forcedDrop = 0;
  m_stats.unforcedDrop = 0;
  m_binSize = 1.0 * GetMaxSize ().GetValue () / SFB_BINS; 
}

void
SfbQueueDisc::InitializeBins (void)
{
  for (uint32_t i = 0; i < SFB_LEVELS; i++)
    {
      for (uint32_t j = 0; j < SFB_BINS; j++)
        {
          m_bins[i][j].packets = 0;
          m_bins[i][j].pmark = 0;
        }
    }
}

bool
SfbQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  double_t pMin = 1;
  if (GetCurrentSize () > GetMaxSize ())
    {
      IncrementBinsPmarks (item);
      m_stats.forcedDrop++;
      DropBeforeEnqueue (item, "Full queue");
      return false;
    }
  
  uint32_t hashed = item->Hash (m_perturbation);
  for (uint32_t i = 0; i < SFB_LEVELS; i++)
    {
      uint32_t curHash = hashed & SFB_BUCKET_MASK;
      hashed >>= SFB_BUCKET_SHIFT;
      if (m_bins[i][curHash].packets == 0)
        {
          DecrementBinPmark (i, curHash);
        }
      else if (m_bins[i][curHash].packets > m_binSize)
        {
          NS_LOG_LOGIC ("Incrementing at DoEnqueue bin " << curHash << " level " << i <<
            " due to " << m_bins[i][curHash].packets << " packets more than size " << m_binSize);
          IncrementBinPmark (i, curHash);
        }
      if (m_bins[i][curHash].pmark < pMin)
        {
          pMin = m_bins[i][curHash].pmark;
        }
    }

  if (GetMinProbability (item) == 1.0)
    {
      //rateLimit();
    }
  else if (DropEarly (item))
    {
      DropBeforeEnqueue (item, "Unforced drop");
      return false;
    }

  bool isEnqueued = GetInternalQueue (0)->Enqueue (item);
  if (isEnqueued == true)
    {
      IncrementBinsQueueLength (item);
    }
  
  /* Check Queue Length Consistency */
  for (uint32_t i = 0; i < SFB_LEVELS; i++)
    {
      uint32_t totalPackets = 0;
      for (uint32_t j = 0; j < SFB_BINS; j++)
        {
          totalPackets += m_bins[i][j].packets;
        }
      std::cout << "Level " << i << ": " << totalPackets << " " << GetInternalQueue (0)->GetNPackets () << std::endl;
      NS_ASSERT (totalPackets == GetInternalQueue (0)->GetNPackets ());
    }
  return isEnqueued;
}

double
SfbQueueDisc::GetMinProbability (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this);
  double u =  1.0;
  uint32_t hashed = item->Hash (m_perturbation);
  for (uint32_t i = 0; i < SFB_LEVELS; i++)
    {
      uint32_t curHash = hashed & SFB_BUCKET_MASK;
      hashed >>= SFB_BUCKET_SHIFT;
      if (u > m_bins[i][curHash].pmark)
        {
          u = m_bins[i][curHash].pmark;
        }
    }
  NS_LOG_LOGIC (this << " Min Probability = " << u);
  return u;
}

bool
SfbQueueDisc::DropEarly (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this);
  double u =  m_uv->GetValue ();
  if (u <= GetMinProbability (item))
    {
      return true;
    }
  return false;
}

void
SfbQueueDisc::IncrementBinsQueueLength (Ptr<QueueDiscItem> item)
{
  uint32_t hashed = item->Hash (m_perturbation);
  for (uint32_t i = 0; i < SFB_LEVELS; i++)
    {
      uint32_t curHash = hashed & SFB_BUCKET_MASK;
      hashed >>= SFB_BUCKET_SHIFT;
      m_bins[i][curHash].packets++;
      if (m_bins[i][curHash].packets > m_binSize)
        {
          NS_LOG_LOGIC ("Incrementing at QueueLength bin " << curHash << " level " << i <<
            " due to " << m_bins[i][curHash].packets << " packets more than size " << m_binSize);
          IncrementBinPmark (i, curHash);
        }
    }
}

void
SfbQueueDisc::IncrementBinsPmarks (Ptr<QueueDiscItem> item)
{
  uint32_t hashed = item->Hash (m_perturbation);
  for (uint32_t i = 0; i < SFB_LEVELS; i++)
    {
      uint32_t curHash = hashed & SFB_BUCKET_MASK;
      hashed >>= SFB_BUCKET_SHIFT;
      NS_LOG_LOGIC ("Incrementing at Pmarks bin " << curHash << " level " << i <<
        " due to " << m_bins[i][curHash].packets << " packets more than size " << m_binSize);
      IncrementBinPmark (i, curHash);
    }
}

void
SfbQueueDisc::IncrementBinPmark (uint32_t i, uint32_t j)
{
  NS_LOG_FUNCTION (this << i << j);
  m_bins[i][j].pmark += m_increment;
  if (m_bins[i][j].pmark > 1.0)
    {
      m_bins[i][j].pmark = 1.0;
    }
}

void
SfbQueueDisc::DecrementBinsQueueLength (Ptr<QueueDiscItem> item)
{
  uint32_t hashed = item->Hash (m_perturbation);
  for (uint32_t i = 0; i < SFB_LEVELS; i++)
    {
      uint32_t curHash = hashed & SFB_BUCKET_MASK;
      hashed >>= SFB_BUCKET_SHIFT;
      NS_ASSERT (m_bins[i][curHash].packets > 0);
      m_bins[i][curHash].packets--;
    }
}

void
SfbQueueDisc::DecrementBinPmark (uint32_t i, uint32_t j)
{
  m_bins[i][j].pmark -= m_decrement;
  if (m_bins[i][j].pmark < 0.0)
    {
      m_bins[i][j].pmark = 0.0;
    }
}

Ptr<QueueDiscItem>
SfbQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<QueueDiscItem> item = StaticCast<QueueDiscItem> (GetInternalQueue (0)->Dequeue ());

  NS_LOG_LOGIC ("Popped " << item);

  if (item)
    {
      DecrementBinsQueueLength (item);
    }

  return item;
}

Ptr<const QueueDiscItem>
SfbQueueDisc::DoPeek () const
{
  NS_LOG_FUNCTION (this);
  if (GetInternalQueue (0)->IsEmpty ())
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }

  Ptr<const QueueDiscItem> item = StaticCast<const QueueDiscItem> (GetInternalQueue (0)->Peek ());

  NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
  NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());

  return item;
}

bool
SfbQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("SfbQueueDisc cannot have classes");
      return false;
    }

  if (GetNInternalQueues () == 0)
    {
      // create a DropTail queue
      Ptr<InternalQueue> queue = CreateObject<DropTailQueue<QueueDiscItem> > ();
      queue->SetMaxSize (GetMaxSize ());
      AddInternalQueue (queue);
    }

  if (GetNInternalQueues () != 1)
    {
      NS_LOG_ERROR ("SfbQueueDisc needs 1 internal queue");
      return false;
    }

  return true;
}

} //namespace ns3