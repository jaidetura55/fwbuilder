/* 

                          Firewall Builder

                 Copyright (C) 2008 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@fwbuilder.org

  $Id$

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


  ====================================================================

  Algorithms implemented in this module are used to decide if an
  object <obj> that defines one or several addresses matches firewall,
  its interface or any of its addresses in the sense that packets with
  address equal to that of <obj> would be accepted by the firewall
  machine. Two match modes are defined: EXACT and PARTIAL. The
  difference makes sense when object <obj> is an AddressRange because
  the range may match subnet of an interface exactly or overlap with
  it only partially.

  This class is used in policy compilers to determine chain (for
  iptables) or find interface that matches addresses used in policy or
  nat rules.

*/

#include <assert.h>

#include "fwbuilder/libfwbuilder-config.h"
#include "fwbuilder/ObjectMatcher.h"

#include "fwbuilder/InetAddr.h"
#include "fwbuilder/InetAddrMask.h"
#include "fwbuilder/AddressRange.h"
#include "fwbuilder/RuleElement.h"
#include "fwbuilder/Firewall.h"
#include "fwbuilder/Cluster.h"
#include "fwbuilder/Network.h"
#include "fwbuilder/NetworkIPv6.h"
#include "fwbuilder/Interface.h"
#include "fwbuilder/IPv4.h"
#include "fwbuilder/IPv6.h"
#include "fwbuilder/DNSName.h"
#include "fwbuilder/MultiAddress.h"
#include "fwbuilder/Group.h"

#include <iostream>
#include <string>

 
using namespace libfwbuilder;
using namespace std;


/**
 * returns true if :
 * 1. obj1 is the same as obj2, or 
 * 2. any child of obj2 or 
 * 3. its address matches that of any obj2's interfaces, or 
 * 4. its address matches broadcast address of one of the interfaces
 * 5. address of obj1 is a broadcast (255.255.255.255)
 * 6. address of obj1 is a multicast ( added 09/15/02, --vk )
 *
 * This used double dispatch pattern: complexMatch calls virtual
 * method dispatchComplexMatch method of obj1, which in turn calls
 * appropriate variant of ObjectMatcher::checkComplexMatch method
 * depending on which class obj1 is an object of.
 */
bool ObjectMatcher::complexMatch(Address *obj1, Address *obj2)
{
    if (obj1->getId()==obj2->getId()) return true;

    if (Cluster::cast(obj1) && Firewall::cast(obj2))
    {
        // "parent_cluster_id" is set in CompilerDriver::populateClusterElements()
        // which unfortunately is part of fwbuilder rather than fwcompiler
        int cluster_id = obj2->getInt("parent_cluster_id");
        if (obj1->getId() == cluster_id) return true;
    }

    return obj1->dispatchComplexMatch(this, obj2);
}

/**
 * Compare single InetAddr to an address defined by Address
 * object. The right hand side Address object should be a "primary"
 * address object (i.e. IPv4, IPv6, Network, NetworkIPv6, AddressRange
 * but not Interface or Host). This method takes into account the
 * hierarcy of which rhs_obj is part. That is, it treats IPv4 which is
 * a child of interface differently from IPv4 which is a standalone
 * object. This method uses flag match_subnets to decide whether it
 * should consider only ip address of rhs_obj or a subnet defined by
 * its address and netmask.
 */
int ObjectMatcher::matchRHS(const InetAddr *inet_addr_obj, Address *rhs_obj)
{
    const InetAddr *addr = rhs_obj->getAddressPtr();
    const InetAddr *netm = rhs_obj->getNetmaskPtr();

    if (match_subnets)
    {
        return matchSubnetRHS(inet_addr_obj, addr, netm);
    } else
    {
        if (matchInetAddrRHS(inet_addr_obj, addr) == 0) return 0;

        if (recognize_broadcasts)
        {
/*
 * bug #1040773: need to match network address as well as
 * broadcast. Packets sent to the network address (192.168.1.0 for net
 * 192.168.1.0/24) go in the broadcast frame and behave just like IP
 * broadcast packets (sent to 192.168.1.1255 for the same net)
 *
 */
            InetAddrMask n(*addr, *netm);
            int f1 = matchInetAddrRHS(inet_addr_obj, n.getNetworkAddressPtr());
            int f2 = matchInetAddrRHS(inet_addr_obj, n.getBroadcastAddressPtr());
            if (f1 == 0 || f2 == 0) return 0;
            if (f2 > 0) return 1;
        }
        return -1;
    }
}

/**
 * Match inet one address against another
 * Returns:
 *  0 if first address is equal to the secnd
 * -1 if first address is less than the secnd
 *  1 if first address is greater than the secnd
 */
int ObjectMatcher::matchInetAddrRHS(const InetAddr *inet_addr_obj,
                                    const InetAddr *rhs_obj_addr)
{
    if ((*inet_addr_obj) == (*rhs_obj_addr)) return 0;
    if ((*inet_addr_obj) < (*rhs_obj_addr)) return -1;
    return 1;
}

/**
 * Match inet address against subnet defined by addr/mask pair.
 * Returns:
 *  0 if inet address belongs to the subnet,
 * -1 if inet address is less than the beginning of the subnet
 *  1 if inet address is greater than the end of the subnet
 */
int ObjectMatcher::matchSubnetRHS(const InetAddr *inet_addr_obj,
                                  const InetAddr *rhs_obj_addr,
                                  const InetAddr *rhs_obj_netm)
{
    InetAddrMask n(*rhs_obj_addr, *rhs_obj_netm);
    int f1 = matchInetAddrRHS(inet_addr_obj, n.getNetworkAddressPtr());
    int f2 = matchInetAddrRHS(inet_addr_obj, n.getBroadcastAddressPtr());
    if (f1 >= 0 && f2 <= 0) return 0;
    if (f1 < 0) return -1;
    if (f2 > 0) return 1;
}

bool ObjectMatcher::checkComplexMatchForSingleAddress(const InetAddr *obj1_addr,
                                                      FWObject *obj2)
{
    if (!obj1_addr->isAny() && 
        ( (recognize_broadcasts && obj1_addr->isBroadcast()) || 
          (recognize_multicasts && obj1_addr->isMulticast()) )
    ) return true;

    string addr_type = (ipv6) ? IPv6::TYPENAME : IPv4::TYPENAME;
    list<FWObject*> all_addresses = obj2->getByTypeDeep(addr_type);
    for (list<FWObject*>::iterator it = all_addresses.begin();
         it != all_addresses.end(); ++it)
    {
        if (matchRHS(obj1_addr, Address::cast(*it)) == 0) return true;
    }
    return false;
}

bool ObjectMatcher::checkComplexMatchForSingleAddress(Address *obj1,
                                                      FWObject *obj2)
{
    const InetAddr *obj1_addr = obj1->getAddressPtr();
    // obj1_addr may be NULL if obj1 does not have any real address,
    // one case when this happens is when obj1 is physAddress
    if (obj1_addr)
        return checkComplexMatchForSingleAddress(obj1_addr, obj2);

    return false;
}

bool ObjectMatcher::checkComplexMatch(Interface *obj1, FWObject *obj2)
{
    if (obj1->getParent()->getId() == obj2->getId()) return true;

    if (!obj1->isRegular()) return false;
    if ((obj1->getByType(IPv4::TYPENAME)).size()>1) return false;
    if ((obj1->getByType(IPv6::TYPENAME)).size()>1) return false;

    return checkComplexMatchForSingleAddress(obj1, obj2);
}

bool ObjectMatcher::checkComplexMatch(Network *obj1, FWObject *obj2)
{
    /*
     * bug #1055937: "Any->all_multicasts not in INPUT Chain"
     * Need to check for multicast networks. We assume they always
     * match if obj2 is firewall
     */
    //Network *nobj1 = Network::cast(obj1);
    const InetAddr *inet_addr = obj1->getAddressPtr();
    if (inet_addr)
    {
        if (inet_addr->isMulticast() && Firewall::isA(obj2)) return true;
        /*
         * need to check for network object with mask 255.255.255.255
         * Such objects are created by the method that expands address
         * ranges, and some often used ranges trigger that (like
         * "255.255.255.255-255.255.255.255" or "0.0.0.0-0.0.0.0")
         */
        if (!obj1->getNetmaskPtr()->isHostMask()) return false;
    } else
        return false;
    return checkComplexMatchForSingleAddress(obj1, obj2);
}

bool ObjectMatcher::checkComplexMatch(NetworkIPv6 *obj1, FWObject *obj2)
{
    const InetAddr *inet_addr = obj1->getAddressPtr();
    if (inet_addr)
    {
        if (inet_addr->isMulticast() && Firewall::isA(obj2)) return true;
        if (!obj1->getNetmaskPtr()->isHostMask()) return false;
    } else
        return false;

    return checkComplexMatchForSingleAddress(obj1, obj2);
}

bool ObjectMatcher::checkComplexMatch(IPv4 *obj1, FWObject *obj2)
{
    FWObject *p=obj1;
    while ( (p=p->getParent())!=NULL)
        if (p->getId()==obj2->getId()) return true;

    return checkComplexMatchForSingleAddress(obj1, obj2);
}

bool ObjectMatcher::checkComplexMatch(IPv6 *obj1, FWObject *obj2)
{
    FWObject *p=obj1;
    while ( (p=p->getParent())!=NULL)
        if (p->getId()==obj2->getId()) return true;

    return checkComplexMatchForSingleAddress(obj1, obj2);
}

bool ObjectMatcher::checkComplexMatch(Host *obj1, FWObject *obj2)
{
/*
 *  match only if all interfaces of obj1 match obj2
 */
    bool res = true;
    list<FWObject*> l = obj1->getByTypeDeep(Interface::TYPENAME);
    for (list<FWObject*>::iterator it = l.begin(); it!=l.end(); ++it)
        res &= checkComplexMatchForSingleAddress(Interface::cast(*it), obj2);
    return res;
}

bool ObjectMatcher::checkComplexMatch(physAddress *obj1, FWObject *obj2)
{
    list<FWObject*> all_pa = obj2->getByTypeDeep(physAddress::TYPENAME);
    for (list<FWObject*>::iterator i = all_pa.begin(); i != all_pa.end(); ++i)
    {
        physAddress *iface_pa = physAddress::cast(*i);
        if (obj1->getPhysAddress() == iface_pa->getPhysAddress()) return true;
    }
    return false;
}

bool ObjectMatcher::checkComplexMatch(AddressRange *obj1, FWObject *obj2)
{
    const InetAddr &range_start = obj1->getRangeStart();
    const InetAddr &range_end = obj1->getRangeEnd();

    if (!range_start.isAny() && 
        ( (recognize_broadcasts && range_start.isBroadcast()) || 
          (recognize_multicasts && range_start.isMulticast()) )
    ) return true;

    if (!range_end.isAny() && 
        ( (recognize_broadcasts && range_end.isBroadcast()) || 
          (recognize_multicasts && range_end.isMulticast()) )
    ) return true;

    string addr_type = (ipv6) ? IPv6::TYPENAME : IPv4::TYPENAME;
    list<FWObject*> all_addresses = obj2->getByTypeDeep(addr_type);
    for (list<FWObject*>::iterator it = all_addresses.begin();
         it != all_addresses.end(); ++it)
    {
        Address *rhs_addr = Address::cast(*it);
        const InetAddr *addr = rhs_addr->getAddressPtr();

        if (match_subnets)
        {
            const InetAddr *netm = rhs_addr->getNetmaskPtr();
            int f_b = matchSubnetRHS(&range_start, addr, netm);
            int f_e = matchSubnetRHS(&range_end, addr, netm);
#if 0
            cerr << "Address Range " << range_start.toString() 
                 << ":" << range_end.toString()
                 << " rhs_addr " << rhs_addr->getName()
                 << " f_b=" << f_b
                 << " f_e=" << f_e
                 << " match_mode=" << address_range_match_mode
                 << endl;
#endif
            if (address_range_match_mode == EXACT)
            {
                if (f_b == 0  && f_e == 0) return true;
            }
            // PARTIAL match only makes sense when match_subnets is true
            if (address_range_match_mode == PARTIAL)
            {
                if (f_b == 0 || f_e == 0) return true; // one end of the range is inside subnet
                if (f_b == -1 && f_e == 1) return true; // range is wider than subnet, subnet fits inside the range completely
            }
        } else
        {
            // If we do not need to match subnets, we just look if address
            // @addr is inside the range
            int f_b = matchInetAddrRHS(&range_start, addr);
            int f_e = matchInetAddrRHS(&range_end, addr);
            if (f_b <= 0  && f_e >= 0) return true;
        }
    }
    return false;

    bool f_b = checkComplexMatchForSingleAddress(&range_start, obj2);
    bool f_e = checkComplexMatchForSingleAddress(&range_end, obj2);

    if (address_range_match_mode == EXACT && f_b && f_e) return true;
    if (address_range_match_mode == PARTIAL && (f_b || f_e)) return true;
    return false;
}

