/*
 * Ann Zhou, copied from:
 * 
 * int-header.cc
 *
 *  Created on: Jan 4, 2021
 *      Author: vamsi
 */




#include "int-header.h"
using namespace ns3;

TypeId
IntHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::IntHeader")
    .SetParent<Tag> ()
    .AddConstructor<IntHeader> ()
//    .AddAttribute ("priorityCustom",
//                   "Priority value",
//                   EmptyAttributeValue (),
//                   MakeUintegerAccessor (&IntHeader::GetPriority),
//                   MakeUintegerChecker<uint8_t> ())
  ;
  return tid;
}
TypeId
IntHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t
IntHeader::GetSerializedSize (void) const
{
//  return 1;
  return max_hops*sizeof(telemetry)+1+1+8; // max_hops number of telemetry structures + 1 (for max_hops) and +1 (for n_hops) + 8 (timstamp)
}
void
IntHeader::Serialize (TagBuffer i) const
{
  i.WriteU8 (max_hops);
  i.WriteU8 (n_hops);
  i.WriteU64 (sent_timestamp);

  /* Next is an array of the following structure. Be careful with the order.*/
//  struct telemetry {
//	  uint64_t bandwidth;
//	  Time tsEnq;
//	  Time tsDeq;
//	  uint32_t qlenEnq;
//	  uint32_t qlenDeq;
//  };

  for (uint32_t x=0; x < max_hops; x++){
	  i.WriteU64(Feedback[x].bandwidth);
	  i.WriteU64(Feedback[x].tsEnq);
	  i.WriteU64(Feedback[x].tsDeq);
	  i.WriteU32(Feedback[x].qlenEnq);
	  i.WriteU32(Feedback[x].qlenDeq);
	  i.WriteU64(Feedback[x].txBytes);
  }
}
void
IntHeader::Deserialize (TagBuffer i)
{
	max_hops = i.ReadU8();
	n_hops = i.ReadU8();
	sent_timestamp = i.ReadU64();

	for (uint32_t x =0 ; x < max_hops;x++){
		Feedback[x].bandwidth = i.ReadU64();
		Feedback[x].tsEnq =   i.ReadU64();
		Feedback[x].tsDeq =   i.ReadU64();
		Feedback[x].qlenEnq = i.ReadU32();
		Feedback[x].qlenDeq = i.ReadU32();
		Feedback[x].txBytes = i.ReadU64();
	}
}
void
IntHeader::Print (std::ostream &os) const
{
  os << "I dont care... :P " ;
}
