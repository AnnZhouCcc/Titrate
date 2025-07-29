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
 * Authors: Nandita G <gm.nandita@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 *
 */

#ifndef DROPFROM_H
#define DROPFROM_H

#include "ns3/queue.h"

namespace ns3 {

/**
 * \ingroup queue
 *
 * \brief A FIFO packet queue that drops packets from a position in queue
 */
template <typename Item>
class DropFromQueue : public Queue<Item>
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * \brief DropFromQueue Constructor
   *
   * Creates a dropfrom queue with a maximum size of 100 packets by default
   */
  DropFromQueue ();
  virtual ~DropFromQueue ();
  virtual bool Enqueue (Ptr<Item> item);
  virtual Ptr<Item> Dequeue (void);
  virtual Ptr<Item> Remove (void);
  virtual Ptr<const Item> Peek (void) const;
  Ptr<const Item> PeekAt (uint32_t) const;
  Ptr<Item> RemoveFrom (uint32_t);
  bool EnqueueAt (uint32_t,Ptr<Item> item);

private:
  using Queue<Item>::begin;
  using Queue<Item>::end;
  using Queue<Item>::DoEnqueue;
  using Queue<Item>::DoDequeue;
  using Queue<Item>::DoRemove;
  using Queue<Item>::DoPeek;

  NS_LOG_TEMPLATE_DECLARE;     //!< redefinition of the log component
};


/**
 * Implementation of the templates declared above.
 */

template <typename Item>
TypeId
DropFromQueue<Item>::GetTypeId (void)
{
  static TypeId tid = TypeId (("ns3::DropFromQueue<" + GetTypeParamName<DropFromQueue<Item> > () + ">").c_str ())
    .SetParent<Queue<Item> > ()
    .SetGroupName ("Network")
    .template AddConstructor<DropFromQueue<Item> > ()
  ;
  return tid;
}

template <typename Item>
DropFromQueue<Item>::DropFromQueue ()
  : Queue<Item> (),
  NS_LOG_TEMPLATE_DEFINE ("DropFromQueue")
{
  NS_LOG_FUNCTION (this);
}

template <typename Item>
DropFromQueue<Item>::~DropFromQueue ()
{
  NS_LOG_FUNCTION (this);
}

template <typename Item>
bool
DropFromQueue<Item>::Enqueue (Ptr<Item> item)
{
  NS_LOG_FUNCTION (item);

  return DoEnqueue (end (), item);
}

template <typename Item>
Ptr<Item>
DropFromQueue<Item>::Dequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<Item> item = DoDequeue (begin ());

  NS_LOG_LOGIC ("Popped " << item);

  return item;
}

template <typename Item>
Ptr<Item>
DropFromQueue<Item>::Remove (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<Item> item = DoRemove (begin ());

  NS_LOG_LOGIC ("Removed " << item);

  return item;
}

template <typename Item>
Ptr<const Item>
DropFromQueue<Item>::Peek (void) const
{
  NS_LOG_FUNCTION (this);

  return DoPeek (begin ());
}

template <typename Item>
Ptr<const Item>
DropFromQueue<Item>::PeekAt (uint32_t pos) const
{
  NS_LOG_FUNCTION (this);

  auto ptr = begin ();
  for (uint32_t i = 0; i < pos; i++)
    {
      ptr++;
    }
  Ptr<const Item> item = DoPeek (ptr);

  NS_LOG_LOGIC ("Peeked " << item);

  return item;
}

template <typename Item>
Ptr<Item>
DropFromQueue<Item>::RemoveFrom (uint32_t pos)
{
  NS_LOG_FUNCTION (this);

  auto ptr = begin ();
  for (uint32_t i = 0; i < pos; i++)
    {
      ptr++;
    }
  Ptr<Item> item = DoDequeue (ptr);

  NS_LOG_LOGIC ("Removed " << item);

  return item;
}

template <typename Item>
bool
DropFromQueue<Item>::EnqueueAt (uint32_t pos,Ptr<Item> item)
{
  NS_LOG_FUNCTION (this);

  auto ptr = begin ();
  for (uint32_t i = 1; i < pos; i++)
    {
      ptr++;
    }
  return DoEnqueue (ptr, item);

}

// The following explicit template instantiation declarations prevent all the
// translation units including this header file to implicitly instantiate the
// DropFromQueue<Packet> class and the DropFromQueue<QueueDiscItem> class. The
// unique instances of these classes are explicitly created through the macros
// NS_OBJECT_TEMPLATE_CLASS_DEFINE (DropFromQueue,Packet) and
// NS_OBJECT_TEMPLATE_CLASS_DEFINE (DropFromQueue,QueueDiscItem), which are included
// in drop-from-queue.cc
extern template class DropFromQueue<Packet>;
extern template class DropFromQueue<QueueDiscItem>;

} // namespace ns3

#endif /* DROPFROM_H */