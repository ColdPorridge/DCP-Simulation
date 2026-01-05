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
#include "vp-tag.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (VpTag);

TypeId 
VpTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::VpTag")
    .SetParent<Tag> ()
    .AddConstructor<VpTag> ()
  ;
  return tid;
}
TypeId 
VpTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
VpTag::GetSerializedSize (void) const
{
  return 4;
}
void 
VpTag::Serialize (TagBuffer buf) const
{
  buf.WriteU16 (m_vp);
}
void 
VpTag::Deserialize (TagBuffer buf)
{
  m_vp = buf.ReadU16 ();
}
void 
VpTag::Print (std::ostream &os) const
{
  os << "vp=" << m_vp;
}
VpTag::VpTag ()
  : Tag () 
{
}

VpTag::VpTag (uint16_t vp)
  : Tag (),
    m_vp (vp)
{
}

void
VpTag::SetVp (uint16_t vp)
{
  m_vp = vp;
}
uint16_t
VpTag::GetVp (void) const
{
  return m_vp;
}


} // namespace ns3
