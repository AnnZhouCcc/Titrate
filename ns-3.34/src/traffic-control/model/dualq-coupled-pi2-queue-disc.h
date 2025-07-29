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
 */

#ifndef DUALQ_COUPLED_PI2_QUEUE_DISC_H
#define DUALQ_COUPLED_PI2_QUEUE_DISC_H

#include <queue>
#include "ns3/packet.h"
#include "ns3/queue-disc.h"
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/timer.h"
#include "ns3/string.h"
#include "ns3/event-id.h"
#include "ns3/simulator.h"
#include "ns3/random-variable-stream.h"
#include "ns3/traced-value.h"

namespace ns3 {

class UniformRandomVariable;

/**
 * \ingroup traffic-control
 *
 * Implements DualQ Coupled PI2 queue disc based on appendix A pseudocode
 * from draft-ietf-tsvwg-aqm-dualq-coupled-10.txt.  The following differences
 * exist with respect to what is specified in draft 10 appendix A:
 * 1) a heuristic to avoid drops in C-queue if fewer than 2 MTU in the queue
 */
class DualQCoupledPi2QueueDisc : public QueueDisc
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief Constructor
   */
  DualQCoupledPi2QueueDisc ();
  /**
   * \brief  Destructor
   */
  virtual ~DualQCoupledPi2QueueDisc ();
  /**
   * \brief Get the current value of the queue in bytes.
   *
   * \returns The queue size in bytes.
   */
  uint32_t GetQueueSize (void) const;
  /**
   * \brief Set the limit of the queue in bytes.
   *
   * \param lim The limit in bytes.
   */
  void SetQueueLimit (uint32_t lim);

  // Reasons for dropping packets
  static constexpr const char* UNFORCED_CLASSIC_DROP = "Unforced drop in classic queue";  //!< Early probability drops: proactive
  static constexpr const char* FORCED_DROP = "Forced drop";      //!< Drops due to queue limit: reactive
  static constexpr const char* UNFORCED_CLASSIC_MARK = "Unforced classic mark";  //!< Unforced mark in classic queue
  static constexpr const char* UNFORCED_L4S_MARK = "Unforced mark in L4S queue";

  /**
   * Assign a fixed random variable stream number to the random variables
   * used by this model.  Return the number of streams (possibly zero) that
   * have been assigned.
   *
   * \param stream first stream index to use
   * \return the number of stream indices assigned by this model
   */
  int64_t AssignStreams (int64_t stream);

protected:
  // Documented in base class
  virtual void DoDispose (void);

private:
  // Documented in base class
  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual Ptr<const QueueDiscItem> DoPeek (void) const;
  virtual bool CheckConfig (void);

  /**
   * \brief Initialize the queue parameters.
   */
  virtual void InitializeParams (void);
  /**
   * \brief check if traffic is classified as L4S (ECT(1) or CE)
   * \param item the QueueDiscItem to check
   * \return true if ECT(1) or CE, false otherwise
   */
  bool IsL4S (Ptr<QueueDiscItem> item);
  /**
   * \brief Implement the L4S recur function for probabilistic marking
   * \param likelihood the likelihood of marking
   * \return true if the queue should mark the packet
   */
  bool Recur (double likelihood);
  /**
   * \brief Periodically calculate the drop probability
   */
  void DualPi2Update (void);
  /**
   * L4S AQM function
   * \param lqTime Delay to evaluate against threshold
   * \return value between 0 and 1 representing the probability of mark
   */
  double Laqm (Time lqTime) const;
  /**
   * Simple time-shifted FIFO (TS-FIFO).  Must be at least one packet
   * in the queue.
   * \param lqTime L4S sojourn time
   * \param cqTime Classic sojourn time
   * \return either 0 (Classic) or 1 (L4S)
   */
  std::size_t Scheduler (void) const;

  // Values supplied by user
  Time m_target;                //!< Queue delay target for Classic traffic
  Time m_tUpdate;               //!< Time period after which CalculateP () is called
  Time m_tShift;                //!< Scheduler time bias
  uint32_t m_mtu;               //!< Device MTU (bytes)
  double m_alpha;               //!< Parameter to PI Square controller
  double m_beta;                //!< Parameter to PI Square controller
  Time m_minTh;                 //!< L4S marking threshold (in time)
  double m_k;                   //!< Coupling factor
  uint32_t m_queueLimit;        //!< Queue limit in bytes / packets
  Time m_startTime;             //!< Start time of the update timer

  // Variables maintained by DualQ Coupled PI2
  Time m_classicQueueTime;      //!< Arrival time of a packet of Classic Traffic
  Time m_lqTime;                //!< Arrival time of a packet of L4S Traffic
  uint32_t m_thLen;             //!< Minimum threshold (in bytes) for marking L4S traffic
  double m_baseProb;            //!< Variable used in calculation of drop probability
  TracedValue<double> m_pCL;    //!< Coupled probability
  TracedValue<double> m_pC;     //!< Classic drop/mark probability
  TracedValue<double> m_pL;     //!< L4S mark probability
  TracedCallback<Time> m_traceClassicSojourn;   //!< Classic sojourn time
  TracedCallback<Time> m_traceL4sSojourn;       //!< L4S sojourn time
  Time m_prevQ;                 //!< Old value of queue delay
  EventId m_rtrsEvent;          //!< Event used to decide the decision of interval of drop probability calculation
  Ptr<UniformRandomVariable> m_uv;  //!< Rng stream
  double m_count {0};           //! Count for likelihood recur
};

}    // namespace ns3

#endif
