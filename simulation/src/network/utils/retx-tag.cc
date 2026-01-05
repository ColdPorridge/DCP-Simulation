/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
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
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "retx-tag.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (ReTxTag);

TypeId 
ReTxTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ReTxTag")
    .SetParent<Tag> ()
    .AddConstructor<ReTxTag> ()
  ;
  return tid;
}
TypeId 
ReTxTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
ReTxTag::GetSerializedSize (void) const
{
  return 4;
}
void 
ReTxTag::Serialize (TagBuffer buf) const
{
  buf.WriteU32 (m_retx);
}
void 
ReTxTag::Deserialize (TagBuffer buf)
{
  m_retx = buf.ReadU32 ();
}
void 
ReTxTag::Print (std::ostream &os) const
{
  os << "retx=" << m_retx;
}
ReTxTag::ReTxTag ()
  : Tag () 
{
}

ReTxTag::ReTxTag (uint32_t retx)
  : Tag (),
    m_retx (retx)
{
}

void
ReTxTag::SetReTx (uint32_t retx)
{
  m_retx = retx;
}
uint32_t
ReTxTag::GetReTx (void) const
{
  return m_retx;
}


} // namespace ns3
