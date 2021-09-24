/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2018 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
#include "netlinkifc.h"
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/if_arp.h>


NetLinkIfc* NetLinkIfc::pInstance = NULL;
recursive_mutex NetLinkIfc::g_state_mutex;
mutex NetLinkIfc::g_instance_mutex;

NetLinkIfc* NetLinkIfc::get_instance()
{
    if(!pInstance)
    {
        std::lock_guard<std::mutex> guard(g_instance_mutex);
        if(!pInstance)
        {
           pInstance = new NetLinkIfc;
#ifdef _DEBUG_
           cout<<"CREATED NEW INSTANCE"<<endl;
#endif
        }
    }

    return pInstance;
}

NetLinkIfc::NetLinkIfc ():m_state(eNETIFC_STATE_INIT),m_event(eNETIFC_EVENT_UNKNOWN),m_cachemgr(NULL),m_clisocketId(NULL)
{
    //1. start socket.
    //2. Setup all the needed flags
    if (nl_cache_mngr_alloc(NULL,NETLINK_ROUTE,NL_AUTO_PROVIDE,&m_cachemgr) != 0)
    {
       //throw exception.For now print error.
       printf("Error Allocating cacheManager\n");
       m_cachemgr = NULL;
    }
    m_clisocketId = nl_cli_alloc_socket();
    nl_cli_connect(m_clisocketId, NETLINK_ROUTE);
}

NetLinkIfc::~NetLinkIfc()
{
    nl_socket_free(m_clisocketId);
    nl_cache_mngr_free(m_cachemgr);
}


void NetLinkIfc::initialize()
{
    //Initialize state machine.
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    stateMachine[eNETIFC_STATE_POPULATE_IFC][eNETIFC_EVENT_LINK_ADMIN_UP] = &NetLinkIfc::linkAdminUp;
    stateMachine[eNETIFC_STATE_POPULATE_IFC][eNETIFC_EVENT_LINK_ADMIN_DOWN] = &NetLinkIfc::linkAdminDown;
    stateMachine[eNETIFC_STATE_POPULATE_IFC][eNETIFC_EVENT_ADD_LINK] = &NetLinkIfc::addlink;
    stateMachine[eNETIFC_STATE_POPULATE_IFC][eNETIFC_EVENT_DELETE_LINK] = &NetLinkIfc::deletelink;
    stateMachine[eNETIFC_STATE_POPULATE_IFC][eNETIFC_EVENT_ADD_IP6ADDR] = &NetLinkIfc::addip6addr;
    stateMachine[eNETIFC_STATE_POPULATE_IFC][eNETIFC_EVENT_ADD_IPADDR] = &NetLinkIfc::addipaddr;
    stateMachine[eNETIFC_STATE_POPULATE_IFC][eNETIFC_EVENT_DONE] = &NetLinkIfc::populateinterfacecompleted;
    stateMachine[eNETIFC_STATE_POPULATE_ADDRESS][eNETIFC_EVENT_LINK_ADMIN_UP] = &NetLinkIfc::linkAdminUp;
    stateMachine[eNETIFC_STATE_POPULATE_ADDRESS][eNETIFC_EVENT_LINK_ADMIN_DOWN] = &NetLinkIfc::linkAdminDown;
    stateMachine[eNETIFC_STATE_POPULATE_ADDRESS][eNETIFC_EVENT_ADD_LINK] = &NetLinkIfc::addlink;
    stateMachine[eNETIFC_STATE_POPULATE_ADDRESS][eNETIFC_EVENT_DELETE_LINK] = &NetLinkIfc::deletelink;
    stateMachine[eNETIFC_STATE_POPULATE_ADDRESS][eNETIFC_EVENT_ADD_IP6ADDR] = &NetLinkIfc::addip6addr;
    stateMachine[eNETIFC_STATE_POPULATE_ADDRESS][eNETIFC_EVENT_ADD_IPADDR] = &NetLinkIfc::addipaddr;
    stateMachine[eNETIFC_STATE_POPULATE_ADDRESS][eNETIFC_EVENT_DONE] = &NetLinkIfc::populateaddrescompleted;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_ADD_IPADDR] = &NetLinkIfc::addipaddr;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_DELETE_IPADDR] = &NetLinkIfc::deleteipaddr;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_ADD_IP6ADDR] = &NetLinkIfc::addip6addr;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_DELETE_IP6ADDR] = &NetLinkIfc::deleteip6addr;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_ADD_IP6ROUTE] = &NetLinkIfc::addip6route;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_ADD_IPROUTE] = &NetLinkIfc::addiproute;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_DELETE_IP6ROUTE] = &NetLinkIfc::deleteip6route;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_DELETE_IPROUTE] = &NetLinkIfc::deleteiproute;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_LINK_ADMIN_UP] = &NetLinkIfc::linkAdminUp;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_LINK_ADMIN_DOWN] = &NetLinkIfc::linkAdminDown;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_ADD_LINK] = &NetLinkIfc::addlink;
    stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_DELETE_LINK] = &NetLinkIfc::deletelink;
    //stateMachine[eNETIFC_STATE_RUNNING][eNETIFC_EVENT_REINITIALIZE] = &NetLinkIfc::reinitialize;

    //Populate cache.

    int err = nl_cache_mngr_add(m_cachemgr, "route/link", &NetLinkIfc::link_change_cb, this,&m_link_cache);
    if (err < 0)
    {
       printf("Error Adding route/link cache\n");
    }

    err = nl_cache_mngr_add(m_cachemgr, "route/route", &NetLinkIfc::route_change_cb, this,&m_route_cache);
    if (err < 0)
    {
       printf("Error Adding route/route cache\n");
    }

    err = nl_cache_mngr_add(m_cachemgr, "route/addr", &NetLinkIfc::addr_change_cb, this,&m_addr_cache);
    if (err < 0)
    {
       printf("Error Adding route/addr cache\n");
    }

    m_state = eNETIFC_STATE_POPULATE_IFC;
    nl_cache_foreach(m_link_cache,&NetLinkIfc::link_init_cb,this);
    string args;
    runStateMachine(eNETIFC_EVENT_DONE,args);


    nl_cache_foreach(m_addr_cache,&NetLinkIfc::addr_init_cb,this);
    runStateMachine(eNETIFC_EVENT_DONE,args);

    nl_cache_foreach(m_route_cache,&NetLinkIfc::route_init_cb,this);
    runStateMachine(eNETIFC_EVENT_DONE,args);

}
void NetLinkIfc::runStateMachine(netifcEvent event,string args)
{
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    map<netifcState,map<netifcEvent,memfunc> >::const_iterator iter = stateMachine.find(m_state);
    if ((iter != stateMachine.end()) && (iter->second.find(event) != iter->second.end()))
    {
       m_event = event;
       (this->*stateMachine[m_state][event])(args);
    }
    else
    {
       //printf("State event Mapping didnot work out!!\n");
    }

}

void NetLinkIfc::run(bool forever)
{
   //1. create thread
   //2. if selected to run forever wait till thread dies.
   
   std::thread cacheThrd(NetLinkIfc::cacheMgrMsg,this);
   if (forever)
   {
       cacheThrd.join();
   }
   else
   {
       cacheThrd.detach();
   }
}

void NetLinkIfc::tokenize(string& inputStr, vector<string>& tokens)
{
   std::size_t start = inputStr.find_first_not_of(";"), end = start;
    
   while (start != string::npos)
   {
      end = inputStr.find(";",start);
      tokens.push_back(inputStr.substr(start,(end == string::npos) ? string::npos : end - start));
      start = inputStr.find_first_not_of(";",end);
   }
}

bool NetLinkIfc::addipaddrentry(multimap<int,ipaddr>& mmap,int ifindex,ipaddr& addr)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   multimap<int,ipaddr>::iterator it = mmap.find(ifindex);
   if (it != mmap.end())
   {
      std::pair <std::multimap<int,ipaddr>::iterator, std::multimap<int,ipaddr>::iterator> ret = mmap.equal_range(ifindex);
      for (it = ret.first;it != ret.second;++it)
      {
         if (it->second == addr)
         {
#ifdef _DEBUG_
            cout<<"Same Adress found ignoring: "<<addr.address<<endl;
#endif
            return false;
         }
      }
   }
   mmap.insert(std::pair<int,ipaddr>(ifindex,addr));
   return true;
}

bool NetLinkIfc::deleteaddrentry(multimap<int,ipaddr>& mmap,int ifindex,ipaddr& addr)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   multimap<int,ipaddr>::iterator it = mmap.find(ifindex);
   if (it != mmap.end())
   {
      std::pair <std::multimap<int,ipaddr>::iterator, std::multimap<int,ipaddr>::iterator> ret = mmap.equal_range(ifindex);
      for (it = ret.first;it != ret.second;++it)
      {
         if (it->second == addr)
         {
#ifdef _DEBUG_
            cout<<"Deleting Address: "<<addr.address<<endl;
#endif
            mmap.erase(it);
            return true;
         }
      }
   }
   return false;
}

void NetLinkIfc::publish(NlType type,string args)
{
   for (auto const& i : m_subscribers)
   {
      if (i->isSameType(type))
      {
         i->invoke(args);
      }
   }
}

void NetLinkIfc::updateLinkAdminState(string link, bool adminstate)
{
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    static std::map<std::string, bool> link_admin_states;
    if (link_admin_states[link] != adminstate) // if <link> not in map, insert {<link>, false} before comparing
    {
        link_admin_states[link] = adminstate;
        string msgArgs = link + (adminstate ? " up" : " down");
        publish(NlType::link, msgArgs);
    }
}

void NetLinkIfc::linkAdminUp(string link)
{
    updateLinkAdminState(link, true);
}

void NetLinkIfc::linkAdminDown(string link)
{
    updateLinkAdminState(link, false);
}

void NetLinkIfc::updateLinkOperState(string link, bool operstate)
{
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    static std::map<std::string, bool> link_oper_states;
    if (link_oper_states[link] != operstate) // if <link> not in map, insert {<link>, false} before comparing
    {
        link_oper_states[link] = operstate;
        string msgArgs = link + (operstate ? " add" : " delete");
        publish(NlType::link, msgArgs);
    }
}

void NetLinkIfc::addlink(string str)
{
    updateLinkOperState(str, true);
}
void NetLinkIfc::deletelink(string str)
{
    updateLinkOperState(str, false);
}

void NetLinkIfc::addipaddr(string str)
{
   //format: eth0;2601:a40:303:0:988a:d98e:7772:b153
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   vector<string> tokens;

   tokenize(str,tokens);

   if (tokens.size() < 3)
   {
       cout<<"ADD IPV4 ADRESS: Ignoring Message due to Improper formatting. Incoming string: "<<str.c_str()<<endl;
       return;
   }
#ifdef _DEBUG_
   cout<<"ADD IPV4 ADRESS ;INTERFACE NAME = "<<tokens[0]<<"   address="<<tokens[1]<<" FLAGS = "<<tokens[2]<<endl;
#endif
   string msgArgs = "add ipv4 ";
   msgArgs += tokens[0] + " " + tokens[1] + " " + tokens[2];
#ifdef _DEBUG_
   cout<<"MSGARGS = "<<msgArgs<<endl;
#endif
   publish(NlType::address,msgArgs);
}
void NetLinkIfc::deleteipaddr(string str)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   vector<string> tokens;
   tokenize(str,tokens);

   if (tokens.size() < 3)
   {
       cout<<"DELETE IPV4 ADRESS: Ignoring Message due to Improper formatting. Incoming string: "<<str.c_str()<<endl;
       return;
   }
#ifdef _DEBUG_
   cout<<"DELETE IPV4 ADRESS ;INTERFACE NAME = "<<tokens[0]<<"   address="<<tokens[1]<<" FLAGS = "<<tokens[2]<<endl;
#endif

   string msgArgs = "delete ipv4 ";
   msgArgs += tokens[0] + " " + tokens[1] + " " + tokens[2];
#ifdef _DEBUG_
   cout<<"MSGARGS = "<<msgArgs<<endl;
#endif
   publish(NlType::address,msgArgs);
}
void NetLinkIfc::addip6addr(string str )
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   vector<string> tokens;

   tokenize(str,tokens);

   if (tokens.size() < 3)
   {
       cout<<"ADD IPV6 ADRESS: Ignoring Message due to Improper formatting. Incoming string: "<<str.c_str()<<endl;
       return;
   }
#ifdef _DEBUG_
   cout<<"ADD IPV6 ADRESS ;INTERFACE NAME = "<<tokens[0]<<"   address="<<tokens[1]<<" FLAGS = "<<tokens[2]<<endl;
#endif

   string msgArgs = "add ipv6 ";
   msgArgs += tokens[0] + " " + tokens[1] + " " + tokens[2];
#ifdef _DEBUG_
   cout<<"MSGARGS = "<<msgArgs<<endl;
#endif
   publish(NlType::address,msgArgs);
}
void NetLinkIfc::deleteip6addr(string str)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   vector<string> tokens;
   tokenize(str,tokens);

   if (tokens.size() < 3)
   {
       cout<<"DELETE IPV6 ADRESS: Ignoring Message due to Improper formatting. Incoming string: "<<str.c_str()<<endl;
       return;
   }
#ifdef _DEBUG_
   cout<<"DELETE IPV6 ADRESS ;INTERFACE NAME = "<<tokens[0]<<"   address="<<tokens[1]<<" FLAGS = "<<tokens[2]<<endl;
#endif

   string msgArgs = "delete ipv6 ";
   msgArgs += tokens[0] + " " + tokens[1] + " " + tokens[2];
#ifdef _DEBUG_
   cout<<"MSGARGS = "<<msgArgs<<endl;
#endif
   publish(NlType::address,msgArgs);
}


void NetLinkIfc::populateinterfacecompleted(string str)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
#ifdef _DEBUG_
   cout<<"INSIDE COMPLETED INTERFACE POPULATION; Received: "<<str<<endl;
#endif
   m_state = eNETIFC_STATE_POPULATE_ADDRESS;
}
void NetLinkIfc::populateaddrescompleted(string str)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
#ifdef _DEBUG_
   cout<<"INSIDE COMPLETED ADDRESS POPULATION; Received: "<<str<<endl;
#endif
   m_state = eNETIFC_STATE_RUNNING;
}

void NetLinkIfc::addiproute(string str)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
#ifdef _DEBUG_
   cout<<"INSIDE ADDIPROUTE; Received: "<<str<<endl;
#endif

   string msgArgs = "add ipv4 ";
   msgArgs += str;
   publish(NlType::route,msgArgs);
}

void NetLinkIfc::addip6route(string str)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
#ifdef _DEBUG_
   cout<<"INSIDE ADDIP6ROUTE; Received: "<<str<<endl;
#endif

   string msgArgs = "add ipv6 ";
   msgArgs += str;
   publish(NlType::route,msgArgs);
}

void NetLinkIfc::deleteiproute(string str)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
#ifdef _DEBUG_
   cout<<"INSIDE DELETEIPROUTE; Received: "<<str<<endl;
#endif

   string msgArgs = "delete ipv4 ";
   msgArgs += str;
   publish(NlType::route,msgArgs);
}

void NetLinkIfc::deleteip6route(string str)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
#ifdef _DEBUG_
   cout<<"INSIDE DELETEIP6ROUTE; Received: "<<str<<endl;
#endif

   string msgArgs = "delete ipv6 ";
   msgArgs += str;
   publish(NlType::route,msgArgs);
}


void NetLinkIfc::cacheMgrMsg(NetLinkIfc* instance)
{
   while (1)
   {
      int err = nl_cache_mngr_poll(instance->m_cachemgr, -1);
      if (err < 0)
      {
         cout<<"Poll Request Failed"<<endl;
      }
   }
}

void NetLinkIfc::deleteinterfaceip(string ifc, unsigned int family)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   struct rtnl_addr *addr;

   addr = nl_cli_addr_alloc();

   int ifindex = 0;

   //1. populate index.
   if (!(ifindex = rtnl_link_name2i(m_link_cache, ifc.c_str())))
   {
      cout <<ENOENT<<" Link "<<ifc<<" does not exist"<<endl;
      return;
   }
   rtnl_addr_set_ifindex(addr, ifindex);
   
   //2. Set Family.
   rtnl_addr_set_family(addr, family);

  //3. Set scope to Global
  rtnl_addr_set_scope(addr, 0);
  nl_cache_foreach_filter(m_addr_cache, ((struct nl_object *) (addr)), delete_addr_cb, m_clisocketId);
  rtnl_addr_put(addr);
}

void delete_addr_cb(struct nl_object *obj, void *arg)
{
   struct rtnl_addr *addr = (struct rtnl_addr*)nl_object_priv(obj);
#ifdef _DEBUG_
   cout<<"TRYING TO DELETE ADDRESS"<<endl;
#endif
   int err;
   if ((err = rtnl_addr_delete((struct nl_sock *)arg, addr, 0)) < 0)
   {
#ifdef _DEBUG_
      cout <<"Unable to delete address: "<<nl_geterror(err)<<endl;
#endif
   }
}

void NetLinkIfc::deleteinterfaceroutes(string ifc, unsigned int family)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);

   struct rtnl_route *route;
   int nf = 0;

   route = nl_cli_route_alloc();

   int ifindex = 0;

   //1. populate index.
   if (!(ifindex = rtnl_link_name2i(m_link_cache, ifc.c_str())))
   {
#ifdef _DEBUG_
      cout <<ENOENT<<" Link "<<ifc<<" does not exist"<<endl;
#endif
      return;
   }

   //2. Set the Family
   rtnl_route_set_family(route, family);

   //3. Set the device.
   struct rtnl_nexthop *nh = rtnl_route_nh_alloc();
   rtnl_route_nh_set_ifindex(nh,ifindex);
   rtnl_route_add_nexthop(route, nh);

   nl_cache_foreach_filter(m_route_cache, (struct nl_object *)route, delete_route_cb, m_clisocketId);
   rtnl_route_put(route);
}


void delete_route_cb(struct nl_object *obj, void *arg)
{
#ifdef _DEBUG_
   cout<<"TRYING TO DELETE ROUTE"<<endl;
#endif
   struct rtnl_route *route = (struct rtnl_route*)nl_object_priv(obj);
   int err;
   if ((err = rtnl_route_delete((struct nl_sock *)arg, route, 0)) < 0)
   {
#ifdef _DEBUG_
      cout <<"Unable to delete route: "<<nl_geterror(err)<<endl;
#endif
   }
   else
   {
#ifdef _DEBUG_
      cout<<"SUCCESSFULLY DELETED ROUTE"<<endl;
#endif
   }
}

void NetLinkIfc::activatelink(string ifc)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);

   struct rtnl_link *link, *up, *down;

   link = nl_cli_link_alloc();
   up = nl_cli_link_alloc();
   down = nl_cli_link_alloc();

   nl_cli_link_parse_name(link,(char*)ifc.c_str());
   rtnl_link_set_flags(up, IFF_UP);
   rtnl_link_unset_flags(down, IFF_UP);
   nlargs linkArg;
   linkArg.socketId = m_clisocketId;
   linkArg.linkInfo = down;
   nl_cache_foreach_filter(m_link_cache, OBJ_CAST(link), modify_link_cb, &linkArg);
   linkArg.linkInfo = up;
   nl_cache_foreach_filter(m_link_cache, OBJ_CAST(link), modify_link_cb, &linkArg);
   rtnl_link_put(link);
   rtnl_link_put(up);
   rtnl_link_put(down);
}
void modify_link_cb(struct nl_object *obj, void *arg)
{
   nlargs* linkArg = (nlargs*) arg;
   struct rtnl_link *link = (struct rtnl_link*)nl_object_priv(obj);
   int err;
   if ((err = rtnl_link_change(linkArg->socketId, link, (struct rtnl_link *)linkArg->linkInfo, 0)) < 0)
   {
#ifdef _DEBUG_
      cout <<"Unable to change link: "<<nl_geterror(err)<<endl;
#endif
   }
   else
   {
#ifdef _DEBUG_
      cout<<"LINK INFO SUCCESSFULLY CHANGED"<<endl;
#endif
   }
}

bool NetLinkIfc::getIpaddr(string ifc,unsigned int family,vector<string>& ipaddr)
{
    int ifindex = 0;
    if (ifc.empty())
    {
        cout<<"NetLinkIfc::getIpaddr : no interface name"<<endl;
        return false;
    }
    else
    {
        cout <<" Interface "<<ifc<<endl;
    }
    if((family != AF_INET) && (family != AF_INET6))
    {
        cout<<"NetLinkIfc::getIpaddr : wrong address family "<< family <<endl;
        return false;
    }
    if (!(ifindex = rtnl_link_name2i(m_link_cache, ifc.c_str())))
    {
        cout <<ENOENT<<" Link "<<ifc<<" does not exist"<<endl;
        return false;
    }
    else
    {
        cout <<"interface index is " <<ifindex <<endl;
    }
    struct rtnl_addr *rtnlAddr = nl_cli_addr_alloc();
    if(!rtnlAddr)
    {
        cout<<"NetLinkIfc::getIpaddr : rtnl_addr NULL " <<endl;
        return false;
    }
    rtnl_addr_set_ifindex(rtnlAddr, ifindex);
    rtnl_addr_set_family(rtnlAddr, family);
    rtnl_addr_set_scope(rtnlAddr, 0);
    nl_cache_foreach_filter(m_addr_cache, (struct nl_object *) rtnlAddr,get_ip_addr_cb, (void *)&ipaddr);
    if (rtnlAddr)
        rtnl_addr_put(rtnlAddr);
    return true;
}
void get_ip_addr_cb(struct nl_object *obj, void *arg)
{
    char *ipBuffer;
    ipBuffer=(char*)malloc(sizeof(char) * INET6_ADDRSTRLEN);
    if(ipBuffer)
    {
        vector<string> *ptrIpAddr = static_cast<vector<string>*>(arg);
        struct nl_addr *addr = rtnl_addr_get_local((struct rtnl_addr *) obj);
        if (NULL == addr)
        {
            cout<<"NetLinkIfc::getIpaddr :failed to get rtnl local address"<<endl;
        }
        ipBuffer = nl_addr2str(addr,ipBuffer,INET6_ADDRSTRLEN);
        if (NULL == ipBuffer)
        {
            cout<<"NetLinkIfc::getIpaddr :failed in nl_addr2str"<<endl;
        }
        else
        {
            ptrIpAddr->push_back(ipBuffer);
            cout << "ip address is "<<ipBuffer<<endl;
        }
        free(ipBuffer);
    }
    else
    {
        cout << "NetLinkIfc:get_ip_addr_cb Malloc Allocation error "<<endl;
    }
}

void NetLinkIfc::getInterfaces (std::vector<iface_info> &interfaces)
{
    nl_cache_foreach(m_link_cache, get_interfaces_cb, &interfaces);
}

void get_interfaces_cb(struct nl_object *obj, void *arg)
{
    struct rtnl_link* link = ((struct rtnl_link*) obj);
    if (!arg)
        return;
    std::vector<iface_info> &interfaces = *(static_cast<std::vector<iface_info>*>(arg));

    if (ARPHRD_ETHER != rtnl_link_get_arptype(link))
        return;  // if it's not ethernet-based, we are not collecting it (lo, sit0, etc.)

    iface_info interface;
    interface.m_if_name = rtnl_link_get_name(link);          // TODO: Returns: Link name or NULL if name is not specified
    interface.m_if_index = rtnl_link_get_ifindex(link);      // TODO: Returns: Interface index or 0 if not set.
    interface.m_if_flags = rtnl_link_get_flags(link);

    struct nl_addr* addr = rtnl_link_get_addr(link);         // TODO: Returns: Link layer address or NULL if not set.
    if (addr)
    {
        char mac[32];                                        // TODO: use a nice standard macro for MAC address length
        interface.m_if_macaddr = nl_addr2str(addr, mac, 32);
    }

    interfaces.push_back(interface);
}

bool NetLinkIfc::getDefaultRoute(bool is_ipv6, string& interface, string& gateway)
{
    default_route r;

    if (nl_cache_is_empty(m_route_cache))
        return false;

    char n = '0';
    struct nl_addr *dst = nl_addr_build(is_ipv6 ? AF_INET6 : AF_INET, &n, 0);
    if (!dst)
        return false;

    struct rtnl_route *filter = nl_cli_route_alloc();
    if (!filter)
        goto free_dst;

    rtnl_route_set_scope(filter, RT_SCOPE_UNIVERSE);
    rtnl_route_set_family(filter, is_ipv6 ? AF_INET6 : AF_INET);
    rtnl_route_set_table(filter, RT_TABLE_MAIN);
    rtnl_route_set_dst(filter, dst);

    nl_cache_foreach_filter(m_route_cache, (struct nl_object*) filter, get_default_route_cb, &r);

    rtnl_route_put(filter);
free_dst:
    nl_addr_put(dst);

     interface.resize(1024,'\0');
     if (rtnl_link_i2name(m_link_cache,r.interface_index,(char*)(interface.c_str()),1024) != NULL)
     {
         gateway = r.gateway;
         return true;
     }
     return false;
}

void get_default_route_cb(struct nl_object* obj, void* arg)
{
    struct rtnl_route *route = (struct rtnl_route *) obj;
    if (!arg)
        return;
    default_route &r = *(static_cast<default_route*>(arg));

    uint32_t priority = rtnl_route_get_priority(route);
    if (priority >= r.priority)
        return;

    if (1 != rtnl_route_get_nnexthops(route))
        return; // ignore routes that have no nexthops or are multipath
    struct rtnl_nexthop *nh = rtnl_route_nexthop_n(route, 0); // get the only nexthop entry
    if (!nh)
        return;

    struct nl_addr* nh_addr = rtnl_route_nh_get_gateway(nh);
    if (!nh_addr)
        return;

    r.priority = priority;
    r.interface_index = rtnl_route_nh_get_ifindex(nh);
    char addr[INET6_ADDRSTRLEN];
    r.gateway = nl_addr2str(nh_addr, addr, INET6_ADDRSTRLEN);

#ifdef _DEBUG_
    cout << "FOUND new default route PRIORITY = " << r.priority <<  " INTERFACE = " << r.interface_index << " GATEWAY = " << r.gateway.c_str() << endl;
#endif
}
void NetLinkIfc::link_change_cb(struct nl_cache *cache, struct nl_object *obj, int action, void *data)
{
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    string act_str;
    if (action == NL_ACT_NEW)
    {
#ifdef _DEBUG_
        cout<<"New Link Message "<<endl;
#endif //_DEBUG_
        act_str = "add";
    }
    else if (action == NL_ACT_DEL)
    {
#ifdef _DEBUG_
        cout<<"Delete Link Message "<<endl;
#endif //_DEBUG_
        act_str = "delete";
    }
    else if (action == NL_ACT_CHANGE)
    {
#ifdef _DEBUG_
        cout<<"Change Link Message "<<endl;
#endif //_DEBUG_
        act_str = "change";
    }
    else
    {
#ifdef _DEBUG_
        cout<<"Unknown Link Event Message "<<endl;
#endif //_DEBUG_
        return;
    }


    struct rtnl_link* link = reinterpret_cast<struct rtnl_link*>(obj);
    NetLinkIfc* inst = reinterpret_cast<NetLinkIfc*>(data);

    inst->processlink_rtnl(act_str,link);
}
void NetLinkIfc::addr_change_cb(struct nl_cache *cache, struct nl_object *obj, int action, void *data)
{
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    string act_str;
    if (action == NL_ACT_NEW)
    {
#ifdef _DEBUG_
        cout<<"New Address Message "<<endl;
#endif //_DEBUG_
        act_str = "add";
    }
    else if (action == NL_ACT_DEL)
    {
#ifdef _DEBUG_
        cout<<"Delete Address Message "<<endl;
#endif //_DEBUG_
        act_str = "delete";
    }
    else if (action == NL_ACT_CHANGE)
    {
#ifdef _DEBUG_
        cout<<"Change Address Message "<<endl;
#endif //_DEBUG_
        act_str = "change";
    }
    else
    {
#ifdef _DEBUG_
        cout<<"Unknown Address Event Message "<<endl;
#endif //_DEBUG_
        return;
    }

    struct rtnl_addr* addr = reinterpret_cast<struct rtnl_addr*>(obj);
    NetLinkIfc* inst = reinterpret_cast<NetLinkIfc*>(data);
    inst->processaddr_rtnl(act_str,addr);
}
void NetLinkIfc::route_change_cb(struct nl_cache *cache, struct nl_object *obj, int action, void *data)
{
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    string act_str;
    if (action == NL_ACT_NEW)
    {
#ifdef _DEBUG_
        cout<<"New Route Message "<<endl;
#endif //_DEBUG_
        act_str = "add";
    }
    else if (action == NL_ACT_DEL)
    {
#ifdef _DEBUG_
        cout<<"Delete Route Message "<<endl;
#endif //_DEBUG_
        act_str = "delete";
    }
    else if (action == NL_ACT_CHANGE)
    {
#ifdef _DEBUG_
        cout<<"Change Route Message "<<endl;
#endif //_DEBUG_
        act_str = "change";
    }
    else
    {
#ifdef _DEBUG_
        cout<<"Unknown Route Event Message "<<endl;
#endif //_DEBUG_
        return;
    }
    struct rtnl_route* route = reinterpret_cast<struct rtnl_route*>(obj);
    NetLinkIfc* inst = reinterpret_cast<NetLinkIfc*>(data);
    inst->processroute_rtnl(act_str,route);
}

void NetLinkIfc::processlink_rtnl(string action,struct rtnl_link* link)
{
    //1. Interface name.
    string msgArgs;
    msgArgs.resize(1024,'\0');
    if (rtnl_link_get_name(link) != NULL)
    {
        msgArgs = rtnl_link_get_name(link);
    }

    int ifindex = rtnl_link_get_ifindex(link);
    if (action == "delete")
    {
        string ifcname(1024,'\0');
        ifcname = rtnl_link_get_name(link);
        std::pair <std::multimap<int,ipaddr>::iterator, std::multimap<int,ipaddr>::iterator> ret = m_ipAddrMap.equal_range(ifindex);
        for (std::multimap<int,ipaddr>::iterator iter = ret.first; iter != ret.second;++iter)
        {
            string msgArgs = ifcname + ";" + iter->second.address + ";" + (iter->second.global ? "global":"local");
#ifdef _DEBUG_
            cout<<"DELETING INTERFACE: IF INDEX = "<<ifindex<<" ; INTERFACE NAME = "<<ifcname.c_str()<<endl;
#endif
            char addrStr[32] = {0};
            if (inet_pton(AF_INET,iter->second.address.c_str(),addrStr))
            {
                runStateMachine(eNETIFC_EVENT_DELETE_IPADDR,msgArgs);
            }
            else
            {
                runStateMachine(eNETIFC_EVENT_DELETE_IP6ADDR,msgArgs);
            }
        }
        m_ipAddrMap.erase(ifindex);
        return;
    }   
    unsigned int operstate = rtnl_link_get_operstate(link);
    unsigned int flags = rtnl_link_get_flags(link);
    if (flags & IFF_UP)
    {
        runStateMachine(eNETIFC_EVENT_LINK_ADMIN_UP, msgArgs);
    }
    if (operstate == IF_OPER_UP)
    {
        runStateMachine(eNETIFC_EVENT_ADD_LINK, msgArgs);
    }
    else if (operstate == IF_OPER_DOWN)
    {
        runStateMachine(eNETIFC_EVENT_DELETE_LINK, msgArgs);
    }
    else
    {
        cout<<"Unrecognized operstate received. Operstate= "<<operstate<<"; Link Name = "<<msgArgs.c_str()<<endl;
    }

    if (!(flags & IFF_UP))
    {
        runStateMachine(eNETIFC_EVENT_LINK_ADMIN_DOWN, msgArgs);
    }

}
void NetLinkIfc::processaddr_rtnl(string action, struct rtnl_addr* addr)
{
    std::string ifname;
    ifname.resize(512,'\0');

    //fileter.
    if (action == "change")
    {
        return;
    }

    ifname = rtnl_link_get_name(rtnl_addr_get_link(addr));

    if (rtnl_addr_get_label(addr) != NULL)
    {
        ifname = rtnl_addr_get_label(addr);
    }

    std::string address;
    address.resize(1024,'\0');

    if ((rtnl_addr_get_peer(addr) != NULL) && (nl_addr_get_len(rtnl_addr_get_peer(addr)) > 0))
    {
        inet_ntop(rtnl_addr_get_family(addr),nl_addr_get_binary_addr(rtnl_addr_get_peer(addr)),(char*)address.c_str(),1024);
    }
    else if ((rtnl_addr_get_local(addr) != NULL) && (nl_addr_get_len(rtnl_addr_get_local(addr)) > 0))
    {
        inet_ntop(rtnl_addr_get_family(addr),nl_addr_get_binary_addr(rtnl_addr_get_local(addr)),(char*)address.c_str(),1024);
    }

    address.erase(std::remove_if(address.begin(),address.end(),[](char c) {
                               return !(isalnum(c) || (c == '/') || (c == ' ') || (c == ':') || (c == '-') || (c == '.') || (c == '@') || (c == '_') || (c == '[') || (c == ']') || (c == ';'));}),address.end());

    string family = rtnl_addr_get_family(addr) == AF_INET ? "IPV4":"IPV6";
    int prefix = rtnl_addr_get_prefixlen(addr);

    ipaddr loc_addr;
    loc_addr.address = address;
    loc_addr.global = (rtnl_addr_get_scope(addr) == 0);
    loc_addr.family = rtnl_addr_get_family(addr);
    loc_addr.prefix = rtnl_addr_get_prefixlen(addr);
    int ifindex = rtnl_addr_get_ifindex(addr);
    netifcEvent event = eNETIFC_EVENT_UNKNOWN;

    string msgargs = ifname + ";" + address + ";" + (loc_addr.global ? "global":"local");

    msgargs.erase(std::remove_if(msgargs.begin(),msgargs.end(),[](char c) {
                               return !(isalnum(c) || (c == '/') || (c == ' ') || (c == ':') || (c == '-') || (c == '.') || (c == '@') || (c == '_') || (c == '[') || (c == ']') || (c == ';'));}),msgargs.end());
    if (action == "add")
    {
        if (addipaddrentry(m_ipAddrMap,ifindex,loc_addr))
        {
            event = loc_addr.family == AF_INET6 ? eNETIFC_EVENT_ADD_IP6ADDR : eNETIFC_EVENT_ADD_IPADDR;
        }
    }
    else if (action == "delete")
    {
        deleteaddrentry(m_ipAddrMap,ifindex,loc_addr);
        event = loc_addr.family == AF_INET6 ? eNETIFC_EVENT_DELETE_IP6ADDR : eNETIFC_EVENT_DELETE_IPADDR;
    }
    runStateMachine(event,msgargs);
}

void NetLinkIfc::processroute_rtnl(string action, struct rtnl_route* route)
{
    int numhops = rtnl_route_get_nnexthops(route);
    string msgargs;
    msgargs = std::to_string(rtnl_route_get_family(route));
    int oifindex = 0;
    string gwaddr(1024,'\0'),prefsrc(1024,'\0'),dstaddr(1024,'\0');
    bool dflt_route = false;
    bool dst_addr = false;

    gwaddr = "NA";
    prefsrc = "NA";
    dstaddr = rtnl_route_get_family(route) == AF_INET6 ? "::0" : "0.0.0.0";
    for (int i =0;i<numhops;++i)
    {
        //We assume to have only one hop in our typical routes.
        struct rtnl_nexthop * nh = rtnl_route_nexthop_n(route,i);
        oifindex = rtnl_route_nh_get_ifindex(nh);
        if ((rtnl_route_nh_get_gateway(nh) != NULL) && (nl_addr_get_len(rtnl_route_nh_get_gateway(nh)) > 0))
        {
            gwaddr.resize(1024,'\0');
            inet_ntop(rtnl_route_get_family(route),nl_addr_get_binary_addr(rtnl_route_nh_get_gateway(nh)),(char*)gwaddr.c_str(),1024);
            dflt_route = true;
        }
    }

    if (oifindex == 0)
    {
        cout<<"Output Index is missing"<<endl;
        return;
    }
    if ((rtnl_route_get_dst(route) != NULL) && (nl_addr_get_len(rtnl_route_get_dst(route)) > 0))
    {
        dstaddr.resize(1024,'\0');
        inet_ntop(rtnl_route_get_family(route),nl_addr_get_binary_addr(rtnl_route_get_dst(route)),(char*)dstaddr.c_str(),1024);
        dst_addr = true;
    }

    if ((rtnl_route_get_pref_src(route) != NULL) && (nl_addr_get_len(rtnl_route_get_pref_src(route)) > 0))
    {
        prefsrc.resize(1024,'\0');
        inet_ntop(rtnl_route_get_family(route),nl_addr_get_binary_addr(rtnl_route_get_pref_src(route)),(char*)prefsrc.c_str(),1024);
    }

    int priority = rtnl_route_get_priority(route);
    netifcEvent event = eNETIFC_EVENT_UNKNOWN;

    string oper;
    if ((action == "add") || (action == "change"))
    {
        event = rtnl_route_get_family(route) == AF_INET6 ? eNETIFC_EVENT_ADD_IP6ROUTE : eNETIFC_EVENT_ADD_IPROUTE;
        oper = "add";
    }
    else
    {
        event = rtnl_route_get_family(route) == AF_INET6 ? eNETIFC_EVENT_DELETE_IP6ROUTE : eNETIFC_EVENT_DELETE_IPROUTE;
        oper = "delete";
    }

    //format: family interface destinationip gatewayip preferred_src metric add/delete
    msgargs = std::to_string(rtnl_route_get_family(route));
    msgargs += "; ";

    string ifname(1024,'\0');
    rtnl_link_i2name(m_link_cache,oifindex,(char*)(ifname.c_str()),1024);
    msgargs += ifname + "; " + dstaddr + "; " + gwaddr + "; " + prefsrc + "; " + std::to_string(rtnl_route_get_priority(route)) +"; " +oper;
    msgargs.erase(std::remove_if(msgargs.begin(),msgargs.end(),[](char c) {
                               return !(isalnum(c) || (c == '/') || (c == ' ') || (c == ':') || (c == '-') || (c == '.') || (c == '@') || (c == '_') || (c == '[') || (c == ']') || (c == ';'));}),msgargs.end());


    if ( dflt_route && !dst_addr )
    {
        cout<<"Publishing default Route: Args msgargs = "<<msgargs.c_str()<<endl;
        publish(NlType::dfltroute,msgargs);
    }

    runStateMachine(event,msgargs);
}

void NetLinkIfc::link_init_cb(struct nl_object *obj , void * arg)
{
    NetLinkIfc* inst = reinterpret_cast<NetLinkIfc*>(arg);
    struct rtnl_link* link = reinterpret_cast<struct rtnl_link*>(obj);
    inst->processlink_rtnl("add",link);
}
void NetLinkIfc::addr_init_cb(struct nl_object *obj , void * arg)
{
    NetLinkIfc* inst = reinterpret_cast<NetLinkIfc*>(arg);
    struct rtnl_addr* addr = reinterpret_cast<struct rtnl_addr*>(obj);
    inst->processaddr_rtnl("add",addr);
}
void NetLinkIfc::route_init_cb(struct nl_object *obj , void * arg)
{
    NetLinkIfc* inst = reinterpret_cast<NetLinkIfc*>(arg);
    struct rtnl_route* route = reinterpret_cast<struct rtnl_route*>(obj);
    inst->processroute_rtnl("add",route);
}
