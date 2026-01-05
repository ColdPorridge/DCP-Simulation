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
#include "origin-pg-tag.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (OriginPgTag);

TypeId 
OriginPgTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::OriginPgTag")
    .SetParent<Tag> ()
    .AddConstructor<OriginPgTag> ()
  ;
  return tid;
}
TypeId 
OriginPgTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
OriginPgTag::GetSerializedSize (void) const
{
  return 4;
}
void 
OriginPgTag::Serialize (TagBuffer buf) const
{
  buf.WriteU32 (m_originPg);
}
void 
OriginPgTag::Deserialize (TagBuffer buf)
{
  m_originPg = buf.ReadU32 ();
}
void 
OriginPgTag::Print (std::ostream &os) const
{
  os << "OriginPG=" << m_originPg;
}
OriginPgTag::OriginPgTag ()
  : Tag () 
{
}

OriginPgTag::OriginPgTag (uint32_t originPg)
  : Tag (),
    m_originPg (originPg)
{
}

void
OriginPgTag::SetPG (uint32_t originPg)
{
  m_originPg = originPg;
}
uint32_t
OriginPgTag::GetPG (void) const
{
  return m_originPg;
}


} // namespace ns3