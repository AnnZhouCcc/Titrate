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

#ifndef SFB_QUEUE_DISC_H
#define SFB_QUEUE_DISC_H

#define SFB_BUCKET_SHIFT 4
#define SFB_BINS (1 << SFB_BUCKET_SHIFT) /* N bins per Level */
#define SFB_BUCKET_MASK (SFB_BINS - 1)
#define SFB_LEVELS (32 / SFB_BUCKET_SHIFT) /* L */

#include <queue>
#include "ns3/packet.h"
#include "ns3/queue-disc.h"
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/timer.h"
#include "ns3/event-id.h"
#include "ns3/random-variable-stream.h"

namespace ns3 {

class UniformRandomVariable;

class SfbQueueDisc : public QueueDisc
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief SfbQueueDisc Constructor
   */
  SfbQueueDisc ();

  /**
   * \brief SfbQueueDisc Destructor
   */
  virtual ~SfbQueueDisc ();

  /**
   * \brief Stats
   */
  typedef struct
  {
    uint32_t unforcedDrop;      //!< Early probability drops: proactive
    uint32_t forcedDrop;        //!< Drops due to queue limit: reactive
  } Stats;

  /**
   * \brief Bin
   */
  typedef struct
  {
    double pmark;
    uint32_t packets;
  } Bin;

  /**
   * \brief Get SFB statistics after running.
   *
   * \returns The drop statistics.
   */
  Stats GetStats ();

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
  /**
   * \brief Dispose of the object
   */
  virtual void DoDispose (void);

  /**
   * \brief Initialize the queue parameters.
   */
  virtual void InitializeParams (void);

  /**
   * \brief Initialize the Bins parameters.
   */
  virtual void InitializeBins (void);

  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual double GetMinProbability (Ptr<QueueDiscItem> item);
  virtual void IncrementBinsQueueLength (Ptr<QueueDiscItem> item);
  virtual void IncrementBinsPmarks (Ptr<QueueDiscItem> item);
  virtual void IncrementBinPmark (uint32_t i, uint32_t j);
  virtual void DecrementBinsQueueLength (Ptr<QueueDiscItem> item);
  virtual void DecrementBinPmark (uint32_t i, uint32_t j);

  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual Ptr<const QueueDiscItem> DoPeek (void) const;
  virtual bool CheckConfig (void);


  /**
   * \brief Check if a packet needs to be dropped due to probability drop
   * \returns false for no drop, true for drop
   */
  virtual bool DropEarly (Ptr<QueueDiscItem> item);

private:

//  static const uint32_t SFB_BUCKET_SHIFT = 4;
//  static const uint32_t SFB_BINS = (1 << SFB_BUCKET_SHIFT); /* N bins per Level */
//  static const uint32_t SFB_BUCKET_MASK = (SFB_BINS - 1);
//  static const uint32_t SFB_LEVELS = (32 / SFB_BUCKET_SHIFT); /* L */

  Stats m_stats;                                //!< BLUE statistics
  Ptr<UniformRandomVariable> m_uv;              //!< Rng stream

  // ** Variables supplied by user
  uint32_t m_meanPktSize;                       //!< Average Packet Size
  double m_increment;                           //!< increment value for marking probability
  double m_decrement;                           //!< decrement value for marking probability
  double m_freezeTime;

  // ** Variables maintained by SFB
  Bin m_bins[SFB_LEVELS][SFB_BINS];
  uint32_t m_binSize;
  uint32_t m_perturbation;                      //!< hash perturbation value
};

} // namespace ns3

#endif // BLUE_QUEUE_DISC_H