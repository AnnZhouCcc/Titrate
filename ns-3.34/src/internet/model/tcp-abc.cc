/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
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

#include "tcp-abc.h"
#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/tcp-socket-state.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpAbc");

NS_OBJECT_ENSURE_REGISTERED (TcpAbc);

TypeId TcpAbc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpAbc")
    .SetParent<TcpCongestionOps> ()
    .AddConstructor<TcpAbc> ()
    .SetGroupName ("Internet")
  ;
  return tid;
}

std::string TcpAbc::GetName () const
{
  return "TcpAbc";
}

TcpAbc::TcpAbc ()
  : TcpCongestionOps ()
{
  NS_LOG_FUNCTION (this);
}

TcpAbc::TcpAbc (const TcpAbc& sock)
  : TcpCongestionOps (sock)
{
  NS_LOG_FUNCTION (this);
}

TcpAbc::~TcpAbc (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps> TcpAbc::Fork (void)
{
  NS_LOG_FUNCTION (this);
  return CopyObject<TcpAbc> (this);
}

void
TcpAbc::Init (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  NS_LOG_INFO (this << "Enabling AbcEcn for ABC");
  tcb->m_useEcn = TcpSocketState::On;
  tcb->m_ecnMode = TcpSocketState::ABCEcn;
  tcb->m_ectCodePoint = TcpSocketState::Ect1;
}

// Step 9, Section 3.3 of RFC 8257.  GetSsThresh() is called upon
// entering the CWR state, and then later, when CWR is exited,
// cwnd is set to ssthresh (this value).  bytesInFlight is ignored.
uint32_t
TcpAbc::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  return tcb->m_cWnd;
}

bool TcpAbc::HasCongControl () const {return true;}
void TcpAbc::CongControl (Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs ){
    uint32_t kMinCwndSegment = 4;
    if (tcb->m_EcnValue == Ipv4Header::ECN_ECT1) {
      tcb->m_cWnd += tcb->m_segmentSize;
    }
    if (tcb->m_EcnValue == Ipv4Header::ECN_ECT0) {
      if (tcb->m_cWnd > (kMinCwndSegment + 1) * tcb->m_segmentSize) {
        tcb->m_cWnd -= tcb->m_segmentSize;
      }
      else {
        tcb->m_cWnd = kMinCwndSegment * tcb->m_segmentSize;
      }
    }

    NS_LOG_DEBUG ("[TcpAbc] time now: " << Simulator::Now().GetMilliSeconds() << 
      " ABC ECN set: " << tcb->m_EcnValue << " cwnd: " << tcb->m_cWnd);
}

} // namespace ns3
