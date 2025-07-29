/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
 * Copyright (c) 2019 Tom Henderson (update to IETF draft -10)
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
 * Author: Shravya K.S. <shravya.ks0@gmail.com>
 *
 */

#include <cmath>
#include <cstddef>
#include "ns3/log.h"
#include "ns3/fatal-error.h"
#include "ns3/assert.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/simulator.h"
#include "ns3/abort.h"
#include "ns3/object-factory.h"
#include "ns3/string.h"
#include "ns3/socket.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/net-device-queue-interface.h"
#include "dualq-coupled-pi2-queue-disc.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DualQCoupledPi2QueueDisc");

NS_OBJECT_ENSURE_REGISTERED (DualQCoupledPi2QueueDisc);

const std::size_t CLASSIC = 0;
const std::size_t L4S = 1;

TypeId DualQCoupledPi2QueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::DualQCoupledPi2QueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<DualQCoupledPi2QueueDisc> ()
    .AddAttribute ("Mtu",
                   "Device MTU (bytes); if zero, will be automatically configured",
                   UintegerValue (0),
                   MakeUintegerAccessor (&DualQCoupledPi2QueueDisc::m_mtu),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("A",
                   "Value of alpha (Hz)",
                   DoubleValue (0.15),
                   MakeDoubleAccessor (&DualQCoupledPi2QueueDisc::m_alpha),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("B",
                   "Value of beta (Hz)",
                   DoubleValue (3),
                   MakeDoubleAccessor (&DualQCoupledPi2QueueDisc::m_beta),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("Tupdate",
                   "Time period to calculate drop probability",
                   TimeValue (Seconds (0.015)),
                   MakeTimeAccessor (&DualQCoupledPi2QueueDisc::m_tUpdate),
                   MakeTimeChecker ())
    .AddAttribute ("Tshift",
                   "Time offset for TS-FIFO scheduler",
                   TimeValue (MilliSeconds (50)),
                   MakeTimeAccessor (&DualQCoupledPi2QueueDisc::m_tShift),
                   MakeTimeChecker ())
    .AddAttribute ("QueueLimit",
                   "Queue limit in bytes",
                   UintegerValue (1562500), // 250 ms at 50 Mbps
                   MakeUintegerAccessor (&DualQCoupledPi2QueueDisc::m_queueLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Target",
                   "PI AQM Classic queue delay target",
                   TimeValue (MilliSeconds (15)),
                   MakeTimeAccessor (&DualQCoupledPi2QueueDisc::m_target),
                   MakeTimeChecker ())
    .AddAttribute ("L4SMarkThresold",
                   "L4S marking threshold in Time",
                   TimeValue (MicroSeconds (475)),
                   MakeTimeAccessor (&DualQCoupledPi2QueueDisc::m_minTh),
                   MakeTimeChecker ())
    .AddAttribute ("K",
                   "Coupling factor",
                   DoubleValue (2),
                   MakeDoubleAccessor (&DualQCoupledPi2QueueDisc::m_k),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("StartTime",  // Only if user wants to change queue start time
                   "Simulation time to start scheduling the update timer",
                   TimeValue (Seconds (0.0)),
                   MakeTimeAccessor (&DualQCoupledPi2QueueDisc::m_startTime),
                   MakeTimeChecker ())
    .AddTraceSource ("ProbCL",
                     "Coupled probability (p_CL)",
                     MakeTraceSourceAccessor (&DualQCoupledPi2QueueDisc::m_pCL),
                     "ns3::TracedValueCallback::Double")
    .AddTraceSource ("ProbL",
                     "L4S mark probability (p_L)",
                     MakeTraceSourceAccessor (&DualQCoupledPi2QueueDisc::m_pL),
                     "ns3::TracedValueCallback::Double")
    .AddTraceSource ("ProbC",
                     "Classic drop/mark probability (p_C)",
                     MakeTraceSourceAccessor (&DualQCoupledPi2QueueDisc::m_pC),
                     "ns3::TracedValueCallback::Double")
    .AddTraceSource ("ClassicSojournTime",
                     "Sojourn time of the last packet dequeued from the Classic queue",
                     MakeTraceSourceAccessor (&DualQCoupledPi2QueueDisc::m_traceClassicSojourn),
                     "ns3::Time::TracedCallback")
    .AddTraceSource ("L4sSojournTime",
                     "Sojourn time of the last packet dequeued from the L4S queue",
                      MakeTraceSourceAccessor (&DualQCoupledPi2QueueDisc::m_traceL4sSojourn),
                     "ns3::Time::TracedCallback")
  ;
  return tid;
}

DualQCoupledPi2QueueDisc::DualQCoupledPi2QueueDisc ()
  : QueueDisc ()
{
  NS_LOG_FUNCTION (this);
  m_uv = CreateObject<UniformRandomVariable> ();
  m_rtrsEvent = Simulator::Schedule (m_startTime, &DualQCoupledPi2QueueDisc::DualPi2Update, this);
}

DualQCoupledPi2QueueDisc::~DualQCoupledPi2QueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

void
DualQCoupledPi2QueueDisc::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_rtrsEvent.Cancel ();
  QueueDisc::DoDispose ();
}

void
DualQCoupledPi2QueueDisc::SetQueueLimit (uint32_t lim)
{
  NS_LOG_FUNCTION (this << lim);
  m_queueLimit = lim;
}

uint32_t
DualQCoupledPi2QueueDisc::GetQueueSize (void) const
{
  NS_LOG_FUNCTION (this);
  return (GetInternalQueue (CLASSIC)->GetNBytes () + GetInternalQueue (L4S)->GetNBytes ());
}

int64_t
DualQCoupledPi2QueueDisc::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uv->SetStream (stream);
  return 1;
}

bool
DualQCoupledPi2QueueDisc::IsL4S (Ptr<QueueDiscItem> item)
{
  uint8_t tosByte = 0;
  if (item->GetUint8Value (QueueItem::IP_DSFIELD, tosByte))
    {
      // ECT(1) or CE
      // We temporarily change the classification here to DSCP to match other AQMs
      if (Socket::IpTos2Priority (tosByte >> 2) > 0)
        {
          NS_LOG_DEBUG ("L4S detected: " << static_cast<uint16_t> (tosByte & 0x3));
          return true;
        }
    }
  NS_LOG_DEBUG ("Classic detected: " << static_cast<uint16_t> (tosByte & 0x3));
  return false;
}

bool
DualQCoupledPi2QueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);
  std::size_t queueNumber = CLASSIC;

  uint32_t nQueued = GetQueueSize ();
  // in pseudocode, it compares to MTU, not packet size
  if (nQueued + item->GetSize () > m_queueLimit)
    {
      // Drops due to queue limit
      DropBeforeEnqueue (item, FORCED_DROP);
      return false;
    }
  else
    {
      if (IsL4S (item))
        {
          queueNumber = L4S;
        }
    }

  bool retval = GetInternalQueue (queueNumber)->Enqueue (item);
  NS_LOG_LOGIC ("Packets enqueued in queue-" << queueNumber << ": " << GetInternalQueue (queueNumber)->GetNPackets ());
  return retval;
}

void
DualQCoupledPi2QueueDisc::InitializeParams (void)
{
  if (m_mtu == 0)
    {
      Ptr<NetDeviceQueueInterface> ndqi = GetNetDeviceQueueInterface ();
      Ptr<NetDevice> dev;
      // if the NetDeviceQueueInterface object is aggregated to a
      // NetDevice, get the MTU of such NetDevice
      if (ndqi && (dev = ndqi->GetObject<NetDevice> ()))
        {
          m_mtu = dev->GetMtu ();
        }
    }
  NS_ABORT_MSG_IF (m_mtu < 68, "Error: MTU does not meet RFC 791 minimum");
  m_thLen = 2 * m_mtu;
  m_prevQ = Time (Seconds (0));
  m_pCL = 0;
  m_pC = 0;
  m_pL = 0;
}

void
DualQCoupledPi2QueueDisc::DualPi2Update ()
{
  NS_LOG_FUNCTION (this);

  // Use queuing time of first-in Classic packet
  Ptr<const QueueDiscItem> item;
  Time curQ = Seconds (0);

  if ((item = GetInternalQueue (CLASSIC)->Peek ()) != 0)
    {
      curQ = Simulator::Now () - item->GetTimeStamp ();
    }

  m_baseProb = m_baseProb + m_alpha * (curQ - m_target).GetSeconds () + m_beta * (curQ - m_prevQ).GetSeconds ();
  // clamp p' to within [0,1]; page 34 of Internet-Draft
  m_baseProb = std::max<double> (m_baseProb, 0);
  m_baseProb = std::min<double> (m_baseProb, 1);
  m_pCL = m_baseProb * m_k;
  m_pCL = std::min<double> (m_pCL, 1);
  m_pC = m_baseProb * m_baseProb;
  m_prevQ = curQ;
  m_rtrsEvent = Simulator::Schedule (m_tUpdate, &DualQCoupledPi2QueueDisc::DualPi2Update, this);
}

Ptr<QueueDiscItem>
DualQCoupledPi2QueueDisc::DoDequeue ()
{
  NS_LOG_FUNCTION (this);
  Ptr<QueueDiscItem> item;
  while (GetQueueSize () > 0)
    {
      if (Scheduler () == L4S)
        {
          item = GetInternalQueue (L4S)->Dequeue ();
          m_traceL4sSojourn (Simulator::Now () - item->GetTimeStamp ());
          double pPrimeL = Laqm (Simulator::Now () - item->GetTimeStamp ());
          if (pPrimeL > m_pCL)
            {
              NS_LOG_DEBUG ("Laqm probability " << std::min<double> (pPrimeL, 1) << " is driving p_L");
            }
          else
            {
              NS_LOG_DEBUG ("coupled probability " << std::min<double> (m_pCL, 1) << " is driving p_L");
            }
          double pL = std::max<double> (pPrimeL, m_pCL);
          pL = std::min<double> (pL, 1); // clamp p_L at 1
          m_pL = pL; // Trace the value of p_L
          if (Recur (pL))
            {
              bool retval = Mark (item, UNFORCED_L4S_MARK);
              if (!retval) {
                DropAfterDequeue (item, UNFORCED_L4S_MARK);
              }
              NS_LOG_DEBUG ("L-queue packet is marked");
            }
          else
            {
              NS_LOG_DEBUG ("L-queue packet is not marked");
            }
          return item;
        }
      else
        {
          item = GetInternalQueue (CLASSIC)->Dequeue ();
          m_traceClassicSojourn (Simulator::Now () - item->GetTimeStamp ());
          // Heuristic in Linux code; never drop if less than 2 MTU in queue
          if (GetInternalQueue (CLASSIC)->GetNBytes () < 2 * m_mtu)
            {
              return item;
            }
          if (m_pC > m_uv->GetValue ())
            {
              if (!Mark (item, UNFORCED_CLASSIC_MARK))
                {
                  DropAfterDequeue (item, UNFORCED_CLASSIC_DROP);
                  NS_LOG_DEBUG ("C-queue packet is dropped");
                  continue;
                }
              else
                {
                  NS_LOG_DEBUG ("C-queue packet is marked");
                  return item;
                }
            }
          NS_LOG_DEBUG ("C-queue packet is neither marked nor dropped");
          return item;
        }
    }
  return 0;
}

std::size_t
DualQCoupledPi2QueueDisc::Scheduler () const
{
  NS_LOG_FUNCTION (this);
  Time cqTime = Seconds (0);
  Time lqTime = Seconds (0);
  Ptr<const QueueDiscItem> peekedItem;
  if ((peekedItem = GetInternalQueue (CLASSIC)->Peek ()) != 0)
    {
      cqTime = Simulator::Now () - peekedItem->GetTimeStamp ();
    }
  if ((peekedItem = GetInternalQueue (L4S)->Peek ()) != 0)
    {
      lqTime = Simulator::Now () - peekedItem->GetTimeStamp ();
    }
  NS_ASSERT_MSG (GetQueueSize () > 0, "Trying to schedule an empty queue");
  // return 0 if classic, 1 if L4S
  if (GetInternalQueue (L4S)->Peek () != 0 && ((lqTime + m_tShift) > cqTime))
    {
      return L4S;
    }
  else if (GetInternalQueue (CLASSIC)->Peek () != 0)
    {
      return CLASSIC;
    }
  else if (GetInternalQueue (L4S)->Peek () != 0)
    {
      NS_FATAL_ERROR ("Should be unreachable");
    }
  return L4S;
}

double
DualQCoupledPi2QueueDisc::Laqm (Time lqTime) const
{
  NS_LOG_FUNCTION (this << lqTime.GetSeconds ());
  if (lqTime > m_minTh)
    {
      return 1;
    }
  return 0;
}

bool
DualQCoupledPi2QueueDisc::Recur (double likelihood)
{
  NS_LOG_FUNCTION (this << likelihood);
  m_count += likelihood;
  if (m_count > 1)
    {
      m_count -= 1;
      return true;
    }
  return false;
}

Ptr<const QueueDiscItem>
DualQCoupledPi2QueueDisc::DoPeek () const
{
  NS_LOG_FUNCTION (this);
  Ptr<const QueueDiscItem> item;

  for (std::size_t i = 0; i < GetNInternalQueues (); i++)
    {
      if ((item = GetInternalQueue (i)->Peek ()) != 0)
        {
          NS_LOG_LOGIC ("Peeked from queue number " << i << ": " << item);
          NS_LOG_LOGIC ("Number packets queue number " << i << ": " << GetInternalQueue (i)->GetNPackets ());
          NS_LOG_LOGIC ("Number bytes queue number " << i << ": " << GetInternalQueue (i)->GetNBytes ());
          return item;
        }
    }

  NS_LOG_LOGIC ("Queue empty");
  return item;
}

bool
DualQCoupledPi2QueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("DualQCoupledPi2QueueDisc cannot have classes");
      return false;
    }

  if (GetNPacketFilters () > 0)
    {
      NS_LOG_ERROR ("DualQCoupledPi2QueueDisc cannot have packet filters");
      return false;
    }

  if (GetNInternalQueues () == 0)
    {
      // Create 2 DropTail queues
      Ptr<InternalQueue> queue0 = CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> > ("MaxSize", QueueSizeValue (GetMaxSize ()));
      Ptr<InternalQueue> queue1 = CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> > ("MaxSize", QueueSizeValue (GetMaxSize ()));
      QueueSize queueSize (BYTES, m_queueLimit);
      queue0->SetMaxSize (queueSize);
      queue1->SetMaxSize (queueSize);
      AddInternalQueue (queue0);
      AddInternalQueue (queue1);
    }

  if (GetNInternalQueues () != 2)
    {
      NS_LOG_ERROR ("DualQCoupledPi2QueueDisc needs 2 internal queue");
      return false;
    }

  if (GetInternalQueue (CLASSIC)->GetMaxSize ().GetValue () < m_queueLimit)
    {
      NS_LOG_ERROR ("The size of the internal Classic traffic queue is less than the queue disc limit");
      return false;
    }

  if (GetInternalQueue (L4S)->GetMaxSize ().GetValue() < m_queueLimit)
    {
      NS_LOG_ERROR ("The size of the internal L4S traffic queue is less than the queue disc limit");
      return false;
    }

  return true;
}

} //namespace ns3
