//* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
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
 * Authors: Nandita G <gm.nandita@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 *
 */


#ifndef CHOKE_QUEUE_DISC_H
#define CHOKE_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/random-variable-stream.h"

namespace ns3 {

class TraceContainer;

/**
 * \ingroup traffic-control
 *
 * \brief A Choke packet queue disc
 */
class ChokeQueueDisc : public QueueDisc
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief ChokeQueueDisc Constructor
   *
   * Create a CHOKe queue disc
   */
  ChokeQueueDisc ();

  /**
   * \brief Destructor
   *
   * Destructor
   */
  virtual ~ChokeQueueDisc ();

  /**
   * \brief Drop types
   */
  enum
  {
    DTYPE_NONE,        //!< Ok, no drop
    DTYPE_FORCED,      //!< A "forced" drop
    DTYPE_UNFORCED,    //!< An "unforced" (random) drop
  };

  /**
   * \brief Set the thresh limits of CHOKe.
   *
   * \param minTh Minimum thresh in bytes or packets.
   * \param maxTh Maximum thresh in bytes or packets.
   */
  void SetTh (double minTh, double maxTh);

  /**
   * Assign a fixed random variable stream number to the random variables
   * used by this model.  Return the number of streams (possibly zero) that
   * have been assigned.
   *
   * \param stream first stream index to use
   * \return the number of stream indices assigned by this model
   */
  int64_t AssignStreams (int64_t stream);
  // Reasons for dropping packets
  static constexpr const char* UNFORCED_DROP = "Unforced drop";  //!< Early probability drops
  static constexpr const char* FORCED_DROP = "Forced drop";      //!< Forced drops, m_qAvg > m_maxTh
  // Reasons for marking packets
  static constexpr const char* UNFORCED_MARK = "Unforced mark";  //!< Early probability marks
  static constexpr const char* FORCED_MARK = "Forced mark";      //!< Forced marks, m_qAvg > m_maxTh
  static constexpr const char* RANDOM_MARK = "Random mark";

protected:
  /**
   * \brief Dispose of the object
   */
  virtual void DoDispose (void);

private:
  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual Ptr<const QueueDiscItem> DoPeek (void) const;
  virtual bool CheckConfig (void);

  /**
   * \brief Initialize the queue parameters.
   *
   * Note: if the link bandwidth changes in the course of the
   * simulation, the bandwidth-dependent CHOKe parameters do not change.
   * This should be fixed, but it would require some extra parameters,
   * and didn't seem worth the trouble...
   */
  virtual void InitializeParams (void);
  /**
   * \brief Compute the average queue size
   * \param nQueued number of queued packets
   * \param m simulated number of packets arrival during idle period
   * \param qAvg average queue size
   * \param qW queue weight given to cur q size sample
   * \returns new average queue size
   */
  double Estimator (uint32_t nQueued, uint32_t m, double qAvg, double qW);
  
  /**
   * \brief Check if a packet needs to be dropped due to probability mark
   * \param item queue item
   * \returns 0 for no drop/mark, 1 for drop
   */
  uint32_t DropEarly (Ptr<QueueDiscItem> item);

  /**
   * \brief Returns a probability using these function parameters for the DropEarly function
   * \param qAvg Average queue length
   * \param maxTh Max avg length threshold
   * \param gentle "gentle" algorithm
   * \param vA vA
   * \param vB vB
   * \param maxP max_p
   * \returns Prob. of packet drop before "count"
   */
  void CalculatePNew (void);
  /**
   * \brief Returns a probability using these function parameters for the DropEarly function
   * \param p Prob. of packet drop before "count"
   * \param count number of packets since last random number generation
   * \param countBytes number of bytes since last drop
   * \param meanPktSize Avg pkt size
   * \param wait True for waiting between dropped packets
   * \param size packet size
   * \returns Prob. of packet drop
   */
  double ModifyP (double p, uint32_t count, uint32_t countBytes,
                  uint32_t meanPktSize, bool wait, uint32_t size);

  // ** Variables supplied by user
  uint32_t m_meanPktSize;   //!< Avg pkt size
  bool m_isWait;            //!< True for waiting between dropped packets
  double m_minTh;           //!< Min avg length threshold (bytes)
  double m_maxTh;           //!< Max avg length threshold (bytes), should be >= 2*minTh
  double m_qW;              //!< Queue weight given to cur queue size sample
  double m_lInterm;         //!< The max probability of dropping a packet
  bool m_isNs1Compat;       //!< Ns-1 compatibility
  DataRate m_linkBandwidth; //!< Link bandwidth
  Time m_linkDelay;         //!< Link delay
  bool m_useEcn;            //!< True if ECN is used (packets are marked instead of being dropped)
  bool m_useHardDrop;       //!< True if packets are always dropped above max threshold
  uint32_t m_perturbation;  //!< hash perturbation value

  // ** Variables maintained by CHOKe
  double m_vProb1;          //!< Prob. of packet drop before "count"
  double m_vA;              //!< 1.0 / (m_maxTh - m_minTh)
  double m_vB;              //!< -m_minTh / (m_maxTh - m_minTh)
  double m_curMaxP;         //!< Current max_p
  double m_vProb;           //!< Prob. of packet drop
  uint32_t m_countBytes;    //!< Number of bytes since last drop
  uint32_t m_old;           //!< 0 when average queue first exceeds threshold
  bool m_idle;          //!< 0/1 idle status
  double m_ptc;             //!< packet time constant in packets/second
  double m_qAvg;            //!< Average queue length
  uint32_t m_count;         //!< Number of packets since last random number generation
  Time m_idleTime;          //!< Start of current idle period

  Ptr<UniformRandomVariable> m_uv;  //!< rng stream
  Ptr<UniformRandomVariable> m_rnd;
};

}; // namespace ns3

#endif // CHOKE_QUEUE_DISC_H