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
#include "epsn-tag.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (EpsnTag);

TypeId 
EpsnTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::EpsnTag")
    .SetParent<Tag> ()
    .AddConstructor<EpsnTag> ()
  ;
  return tid;
}
TypeId 
EpsnTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
EpsnTag::GetSerializedSize (void) const
{
  return 4;
}
void 
EpsnTag::Serialize (TagBuffer buf) const
{
  buf.WriteU32 (m_epsn);
}
void 
EpsnTag::Deserialize (TagBuffer buf)
{
  m_epsn = buf.ReadU32 ();
}
void 
EpsnTag::Print (std::ostream &os) const
{
  os << "epsn=" << m_epsn;
}
EpsnTag::EpsnTag ()
  : Tag () 
{
}

EpsnTag::EpsnTag (uint32_t epsn)
  : Tag (),
    m_epsn (epsn)
{
}

void
EpsnTag::SetSeq (uint32_t epsn)
{
  m_epsn = epsn;
}
uint32_t
EpsnTag::GetSeq (void) const
{
  return m_epsn;
}


} // namespace ns3
