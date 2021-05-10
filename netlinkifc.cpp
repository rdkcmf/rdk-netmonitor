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

static void force_del_default_route(string ifaceName);

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

NetLinkIfc::NetLinkIfc ():m_socketId(NULL),m_state(eNETIFC_STATE_INIT),m_event(eNETIFC_EVENT_UNKNOWN)
{
    //1. start socket.
    //2. Setup all the needed flags

    m_socketId = nl_socket_alloc();
    if (m_socketId == NULL )
    {
       //throw exception.For now print error.
       printf("Error opening Socket");
    }
    nl_socket_disable_seq_check(m_socketId);
    nl_socket_modify_cb(m_socketId, NL_CB_VALID, NL_CB_CUSTOM, NetLinkIfc::receiveNewMsg, this);
    nl_socket_modify_cb(m_socketId, NL_CB_FINISH, NL_CB_CUSTOM , NetLinkIfc::receiveNewMsg, this);
    nl_connect(m_socketId, NETLINK_ROUTE);
    nl_socket_add_memberships(m_socketId, RTNLGRP_LINK, 0);
    nl_socket_add_memberships(m_socketId, RTNLGRP_IPV4_IFADDR, 0);
    nl_socket_add_memberships(m_socketId, RTNLGRP_IPV6_IFADDR, 0);
    nl_socket_add_memberships(m_socketId, RTNLGRP_IPV4_ROUTE, 0);
    nl_socket_add_memberships(m_socketId, RTNLGRP_IPV6_ROUTE, 0);
    
    monitoredmsgtypes.push_back(RTM_NEWADDR);
    monitoredmsgtypes.push_back(RTM_DELADDR);
    monitoredmsgtypes.push_back(RTM_NEWLINK);
    monitoredmsgtypes.push_back(RTM_DELLINK);
    monitoredmsgtypes.push_back(RTM_NEWROUTE);
    monitoredmsgtypes.push_back(RTM_DELROUTE);

    m_clisocketId = nl_cli_alloc_socket();
    nl_cli_connect(m_clisocketId, NETLINK_ROUTE);

    m_link_cache = nl_cli_link_alloc_cache(m_clisocketId);
    m_route_cache = nl_cli_route_alloc_cache(m_clisocketId, 0);
    m_addr_cache = nl_cli_addr_alloc_cache(m_clisocketId);
}

NetLinkIfc::~NetLinkIfc()
{
    nl_socket_free(m_socketId);
    nl_socket_free(m_clisocketId);
    nl_cache_put(m_link_cache);
    nl_cache_put(m_route_cache);
    nl_cache_put(m_addr_cache);
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

    struct nlmsghdr   hdr;
   memset(&hdr,0,sizeof(hdr));

    hdr.nlmsg_type = RTM_GETLINK;
    hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP | NLM_F_ROOT;
    struct ifinfomsg  ifi;
    memset(&ifi,0,sizeof(ifi));

    struct nl_msg* req;

    req = nlmsg_inherit(&hdr);
    nlmsg_append(req,&ifi,sizeof(struct ifinfomsg),1);

    int ret = nl_send_auto(m_socketId, req);
    printf("RET CODE = %d\n",ret);
    m_state = eNETIFC_STATE_POPULATE_IFC;
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
   
   std::thread recvThrd(NetLinkIfc::receiveMsg,this);
   if (forever)
   {
       ofstream pidfile("/run/nlmon.pid",ios::out);
       if (pidfile.is_open())
       {
           pidfile<<getpid()<<"\n";
           pidfile.close();
       }
       recvThrd.join();
   }
   else
   {
       recvThrd.detach();
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

void NetLinkIfc::updateLinkState(string link, bool state)
{
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    static std::map<std::string, bool> link_states;
    auto it = link_states.find(link);
    if (it == link_states.end() || it->second != state)
    {
        link_states[link] = state;
        string msgArgs = link + (state ? " up" : " down");
        publish(NlType::link, msgArgs);
    }
}
void NetLinkIfc::linkAdminUp(string link)
{
    updateLinkState(link, true);
}
void NetLinkIfc::linkAdminDown(string link)
{
    updateLinkState(link, false);
}

void NetLinkIfc::addlink(string str)
{
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    string msgArgs = str + " add";
    publish(NlType::link,msgArgs);
}
void NetLinkIfc::deletelink(string str)
{
    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
    string msgArgs = str + " delete";
    publish(NlType::link,msgArgs);
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

   struct nlmsghdr   hdr;
   memset(&hdr,0,sizeof(hdr));

   hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP | NLM_F_ROOT;
   hdr.nlmsg_type = RTM_GETADDR | RTM_NEWADDR;

   struct ifaddrmsg  ifa;
   memset(&ifa,0,sizeof(ifa));
   ifa.ifa_family = AF_UNSPEC;

   struct nl_msg* req = nlmsg_inherit(&hdr);
   nlmsg_append(req,&ifa,sizeof(struct ifaddrmsg),0);
   nl_send_auto(m_socketId, req);
   m_state = eNETIFC_STATE_POPULATE_ADDRESS;
}
void NetLinkIfc::populateaddrescompleted(string str)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
#ifdef _DEBUG_
   cout<<"INSIDE COMPLETED ADDRESS POPULATION; Received: "<<str<<endl;
#endif

   struct nlmsghdr   hdr;
   memset(&hdr,0,sizeof(hdr));

   hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP | NLM_F_ROOT;
   hdr.nlmsg_type = RTM_GETROUTE;

   struct rtmsg rmsg;
   memset(&rmsg,0,sizeof(rmsg));
   rmsg.rtm_family = AF_UNSPEC;

   struct nl_msg* req = nlmsg_inherit(&hdr);
   nlmsg_append(req,&rmsg,sizeof(struct rtmsg),0);
   nl_send_auto(m_socketId, req);

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

void NetLinkIfc::receiveMsg(NetLinkIfc* instance)
{
   while (1)
   {
      nl_recvmsgs_default(instance->m_socketId);
   }
}

void NetLinkIfc::processLinkMsg(struct nlmsghdr* nlh)
{

    std::lock_guard<std::recursive_mutex> guard(g_state_mutex);

   netifcEvent event = eNETIFC_EVENT_UNKNOWN;
   struct ifinfomsg* iface = (struct ifinfomsg*)nlmsg_data(nlh);
   unsigned int oper_state=0;
   struct nlattr *attrs[IFLA_MAX];
   for (int i =0;i<IFLA_MAX;++i)
   {
      attrs[i] = NULL;
   }
   
   if (nlmsg_parse(nlh, sizeof (struct ifinfomsg), attrs, IFLA_MAX, NULL) < 0)
   {
       printf("PROBLEM PArsing Netlink response\n");
       return;
   }

   if (attrs[IFLA_IFNAME] != NULL)
   {
      if (nlh->nlmsg_type == RTM_NEWLINK)
      {
         event = eNETIFC_EVENT_ADD_IFC;
         m_interfaceMap[iface->ifi_index] = (char *) nla_data(attrs[IFLA_IFNAME]);
         if(attrs[IFLA_OPERSTATE] != NULL)
         {
             oper_state = static_cast<unsigned int>(*(char*) nla_data(attrs[IFLA_OPERSTATE]));
         }
#ifdef _DEBUG_
         cout<<"ADDING INTERFACE: IF INDEX = "<<iface->ifi_index<<" ; INTERFACE NAME = "<<m_interfaceMap[iface->ifi_index]<<" ; OPER STATE = "<<oper_state<<endl;
#endif
         string msgArgs = m_interfaceMap[iface->ifi_index];
         if (iface->ifi_flags & IFF_UP)
             runStateMachine(eNETIFC_EVENT_LINK_ADMIN_UP, msgArgs);
         if(oper_state == IF_OPER_UP) //IF_OPER_UP
         {
             runStateMachine(eNETIFC_EVENT_ADD_LINK,msgArgs);
         }
         else if(oper_state == IF_OPER_DOWN) //IF_OPER_DOWN
         {
             runStateMachine(eNETIFC_EVENT_DELETE_LINK,msgArgs);
         }
         if (!(iface->ifi_flags & IFF_UP))
             runStateMachine(eNETIFC_EVENT_LINK_ADMIN_DOWN, msgArgs);
      }
      else if (nlh->nlmsg_type == RTM_DELLINK)
      {
         event = eNETIFC_EVENT_DELETE_IFC;
         std::pair <std::multimap<int,ipaddr>::iterator, std::multimap<int,ipaddr>::iterator> ret = m_ipAddrMap.equal_range(iface->ifi_index);
         for (std::multimap<int,ipaddr>::iterator iter = ret.first; iter != ret.second;++iter)
         {
            string msgArgs = m_interfaceMap[iface->ifi_index] + ";" + iter->second.address + ";" + (iter->second.global ? "global":"local");
#ifdef _DEBUG_
         cout<<"DELETING INTERFACE: IF INDEX = "<<iface->ifi_index<<" ; INTERFACE NAME = "<<m_interfaceMap[iface->ifi_index]<<endl;
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
         m_interfaceMap.erase(iface->ifi_index);
         m_ipAddrMap.erase(iface->ifi_index);
      }
   }
}


void NetLinkIfc::processAddrMsg(struct nlmsghdr* nlh)
{
   //1. convert to event.
   //2. Parse attributes.
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   struct ifaddrmsg *rtmp = (struct ifaddrmsg *)nlmsg_data(nlh);

   struct nlattr *attrs[IFA_MAX];
   for (int i =0;i<IFA_MAX;++i)
   {
      attrs[i] = NULL;
   }
   if (nlmsg_parse(nlh, sizeof (struct ifaddrmsg), attrs, IFA_MAX, NULL) < 0)
   {
       printf("PROBLEM PArsing Netlink response\n");
       return;
   }
   
   if (attrs[IFA_ADDRESS] == NULL)
   {
       printf("Address Never Received\n");
       return;
   }


   //2. extract the right attribute.
   string addrStr;
   addrStr.resize(1024,'\0');
   inet_ntop(rtmp->ifa_family,nla_data(attrs[IFA_ADDRESS]),(char*)addrStr.c_str(),1024);

   ipaddr addr;
   addr.address = addrStr;
   addr.global = (rtmp->ifa_scope == 0);
   addr.family = rtmp->ifa_family;
   addr.prefix = rtmp->ifa_prefixlen;
   int ifindex = rtmp->ifa_index;
   netifcEvent event = eNETIFC_EVENT_UNKNOWN;
   string ifname = m_interfaceMap[ifindex];
   if (attrs[IFA_LABEL] != NULL)
   {
      ifname = (char*)nla_data(attrs[IFA_LABEL]);
   }
#ifdef _DEBUG_
   cout<<"LABEL = "<<ifname<<endl;
#endif

   if (ifname.empty())
   {
      char iName[100];
      nl_cache_refill(m_clisocketId,m_link_cache);
      if (rtnl_link_i2name(m_link_cache,ifindex,iName,sizeof(iName)) != NULL)
      {
         ifname = iName;
      }
   }

   string msgargs = ifname + ";" + addrStr + ";" + (addr.global ? "global":"local");
   if ((nlh->nlmsg_type == RTM_NEWADDR) && (rtmp->ifa_family == AF_INET6))
   {
      if (addipaddrentry(m_ipAddrMap,ifindex,addr))
      {
         event = eNETIFC_EVENT_ADD_IP6ADDR;
      }
   }
   if ((nlh->nlmsg_type == RTM_NEWADDR) && (rtmp->ifa_family == AF_INET))
   {
      if (addipaddrentry(m_ipAddrMap,ifindex,addr))
      {
         event = eNETIFC_EVENT_ADD_IPADDR;
      }
   }
   if ((nlh->nlmsg_type == RTM_DELADDR) && (rtmp->ifa_family == AF_INET6))
   {
      if (deleteaddrentry(m_ipAddrMap,ifindex,addr))
      {
         event = eNETIFC_EVENT_DELETE_IP6ADDR;
      }
   }
   if ((nlh->nlmsg_type == RTM_DELADDR) && (rtmp->ifa_family == AF_INET))
   {
      if (deleteaddrentry(m_ipAddrMap,ifindex,addr))
      {
         event = eNETIFC_EVENT_DELETE_IPADDR;
      }
   }

   //3. run statemachine.
   runStateMachine(event,msgargs);
}

void NetLinkIfc::processRouteMsg(struct nlmsghdr* nlh)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   struct rtmsg *rtmp = (struct rtmsg *)nlmsg_data(nlh);
   bool dflt_route = false;
   bool route_add = false;
   bool dst_addr = false;

   struct nlattr *attrs[RTA_MAX];
   for (int i =0;i<RTA_MAX;++i)
   {
      attrs[i] = NULL;
   }

   if (nlmsg_parse(nlh, sizeof (struct rtmsg), attrs, RTA_MAX, NULL) < 0)
   {
       printf("PROBLEM PArsing Netlink response\n");
       return;
   }

   netifcEvent event;
   if ((nlh->nlmsg_type == RTM_NEWROUTE) && (rtmp->rtm_family == AF_INET6))
   {
      event = eNETIFC_EVENT_ADD_IP6ROUTE;
      route_add = true;
   }
   if ((nlh->nlmsg_type == RTM_NEWROUTE) && (rtmp->rtm_family == AF_INET))
   {
      event = eNETIFC_EVENT_ADD_IPROUTE;
      route_add = true;
   }
   if ((nlh->nlmsg_type == RTM_DELROUTE) && (rtmp->rtm_family == AF_INET6))
   {
      event = eNETIFC_EVENT_DELETE_IP6ROUTE;
   }
   if ((nlh->nlmsg_type == RTM_DELROUTE) && (rtmp->rtm_family == AF_INET))
   {
      event = eNETIFC_EVENT_DELETE_IPROUTE;
   }

   //Message Args format: family;output interface;destination address;gatewayip;srcip;priority;add/delete
   string msgargs = "";
   if (attrs[RTA_OIF]  == NULL ) 
   {
      cout<<"Output interface is  missing. This is absolutely needed for post processing"<<endl;
      return;

   }

   msgargs += std::to_string(rtmp->rtm_family);
   if (attrs[RTA_OIF] != NULL)
   {
      msgargs += "; ";
      msgargs += m_interfaceMap[*(int*)nla_data(attrs[RTA_OIF])];
   }

   char addrStr[1024] = {0};
   if (attrs[RTA_DST] != NULL)
   {
      inet_ntop(rtmp->rtm_family,nla_data(attrs[RTA_DST]),addrStr,1024);
      msgargs += "; " ; 
      msgargs += addrStr;
      dst_addr = true;
   }
   else
   {
      msgargs += (event == eNETIFC_EVENT_ADD_IP6ROUTE || event == eNETIFC_EVENT_DELETE_IP6ROUTE) ? "; ::/0" : "; 0.0.0.0";
   }

   if (attrs[RTA_GATEWAY] != NULL)
   {
      inet_ntop(rtmp->rtm_family,nla_data(attrs[RTA_GATEWAY]),addrStr,1024);
      msgargs += "; "; 
      msgargs += addrStr;
      dflt_route = true;
   }
   else
   {
      msgargs += "; NA";
   }

   if (attrs[RTA_SRC] != NULL)
   {
      inet_ntop(rtmp->rtm_family,nla_data(attrs[RTA_SRC]),addrStr,1024);
      cout<<"SRC ADDRESS REECEIVED. This is Unusual. Received Address: "<<addrStr<<endl;
   }
   if (attrs[RTA_PREFSRC] != NULL)
   {
      inet_ntop(rtmp->rtm_family,nla_data(attrs[RTA_PREFSRC]),addrStr,1024);
      if (!dflt_route)
      {
         msgargs += "; NA";
      }
      msgargs += "; " ; 
      msgargs += addrStr;
   }
   else
   {
      msgargs += "; NA";
   }

   if (attrs[RTA_PRIORITY] != NULL)
   {
      msgargs += "; ";
      msgargs += std::to_string(*(int*)nla_data(attrs[RTA_PRIORITY]));
   }
   else
   {
      msgargs += "; 0";
   }

   msgargs += route_add ? "; add" : "; delete";

   if ( dflt_route && !dst_addr )
   {
       publish(NlType::dfltroute,msgargs);
   }

   runStateMachine(event,msgargs);
}
int NetLinkIfc::receiveNewMsg(struct nl_msg *msg, void *arg)
{
    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    int len = nlh->nlmsg_len;
    NetLinkIfc* inst = (NetLinkIfc*)arg;
    while (nlmsg_ok (nlh, len))
    {
        if (std::find(inst->monitoredmsgtypes.begin(),inst->monitoredmsgtypes.end(),nlh->nlmsg_type) == inst->monitoredmsgtypes.end())
        {
           if (nlh->nlmsg_type == NLMSG_DONE)
           {
              string args;
              inst->runStateMachine(eNETIFC_EVENT_DONE,args);
           }
           nlh = nlmsg_next (nlh, &len);
           continue;
        }
        if ((nlh->nlmsg_type == RTM_NEWLINK) || (nlh->nlmsg_type == RTM_DELLINK))
        {
           inst->processLinkMsg(nlh);
        }
        
        if ((nlh->nlmsg_type == RTM_NEWADDR) || (nlh->nlmsg_type == RTM_DELADDR))
        {
           inst->processAddrMsg(nlh);
        }

        if ((nlh->nlmsg_type == RTM_NEWROUTE) || (nlh->nlmsg_type == RTM_DELROUTE))
        {
           inst->processRouteMsg(nlh);
        }
        nlh = nlmsg_next (nlh, &len);
    }
    return 1;
}

void NetLinkIfc::deleteinterfaceip(string ifc, unsigned int family)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   struct rtnl_addr *addr;

   addr = nl_cli_addr_alloc();
   nl_cache_refill(m_clisocketId,m_link_cache);
   nl_cache_refill(m_clisocketId,m_addr_cache);

   int ifindex = 0;

   //1. populate index.
   if (!(ifindex = rtnl_link_name2i(m_link_cache, ifc.c_str())))
   {
#ifdef _DEBUG_
      cout <<ENOENT<<" Link "<<ifc<<" does not exist"<<endl;
#endif
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

bool NetLinkIfc::userdefinedrouteexists(string ifc, string gateway,unsigned int family)
{
   bool retCode = false;
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   struct rtnl_route *route = nl_cli_route_alloc();

   nl_cache_refill(m_clisocketId,m_link_cache);
   nl_cache_refill(m_clisocketId,m_route_cache);

   int ifindex = 0;

   //1. populate index.
   if (!(ifindex = rtnl_link_name2i(m_link_cache, ifc.c_str())))
   {
      cout <<ENOENT<<" Link "<<ifc<<" does not exist"<<endl;
      return retCode;
   }

   //2. Set the Family
   rtnl_route_set_family(route, family);

   struct rtnl_nexthop *nh = rtnl_route_nh_alloc();
   rtnl_route_nh_set_ifindex(nh,ifindex);

   struct nl_addr * gwAddr = NULL; 
   if (!nl_addr_parse(gateway.c_str(),family,&gwAddr))
   {
      rtnl_route_nh_set_gateway(nh,gwAddr);
   }
   rtnl_route_add_nexthop(route, nh);

   nlargs routeArgs;
   routeArgs.socketId = NULL;
   routeArgs.linkInfo = &retCode;
   nl_cache_foreach_filter(m_route_cache, (struct nl_object *)route, update_user_route_present, &routeArgs);
   rtnl_route_put(route);
   nl_addr_put(gwAddr);
   return retCode;
}
bool NetLinkIfc::routeexists(string ifc, string gateway,unsigned int family,unsigned int priority)
{
   bool retCode = false;
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   struct rtnl_route *route;

   route = nl_cli_route_alloc();
   nl_cache_refill(m_clisocketId,m_link_cache);
   nl_cache_refill(m_clisocketId,m_route_cache);

   int ifindex = 0;

   //1. populate index.
   if (!(ifindex = rtnl_link_name2i(m_link_cache, ifc.c_str())))
   {
      cout <<ENOENT<<" Link "<<ifc<<" does not exist"<<endl;
      return retCode;
   }

   //2. Set the Family
   rtnl_route_set_family(route, family);

   //3. Set priority.
   rtnl_route_set_priority(route,priority);

   //4. Set the device.
   struct rtnl_nexthop *nh = rtnl_route_nh_alloc();
   rtnl_route_nh_set_ifindex(nh,ifindex);
   rtnl_route_add_nexthop(route, nh);

   nlargs routeArgs;
   routeArgs.socketId = NULL;
   routeArgs.linkInfo = &retCode;

   nl_cache_foreach_filter(m_route_cache, (struct nl_object *)route, update_route_present, &routeArgs);
   rtnl_route_put(route);
   return retCode;
}
void NetLinkIfc::changedefaultroutepriority(string ifc, string gateway,unsigned int family,unsigned int priority,unsigned int setpriority)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);
   struct rtnl_route *route;
   int nf = 0;

   route = nl_cli_route_alloc();
   nl_cache_refill(m_clisocketId,m_link_cache);

   int ifindex = 0;
   cout <<"Entered Function: "<<__FUNCTION__<<", line number: "<<__LINE__<<endl;

   //1. populate index.
   if (!(ifindex = rtnl_link_name2i(m_link_cache, ifc.c_str())))
   {
      cout <<ENOENT<<" Link "<<ifc<<" does not exist"<<endl;
      return;
   }

   //2. Set the Family
   rtnl_route_set_family(route, family);

   //3. Set priority.
   rtnl_route_set_priority(route,priority);

   //4. Set the device.
   struct rtnl_nexthop *nh = rtnl_route_nh_alloc();
   rtnl_route_nh_set_ifindex(nh,ifindex);
   struct nl_addr * gwAddr = NULL; 
   if (!nl_addr_parse(gateway.c_str(),family,&gwAddr))
   {
      rtnl_route_nh_set_gateway(nh,gwAddr);
   }
   rtnl_route_add_nexthop(route, nh);

   nlargs routeArgs;
   routeArgs.socketId = m_clisocketId;
   routeArgs.linkInfo = &setpriority;
   routeArgs.ifaceName = ifc;

   //We are deleting all the routes now.
   //We will have to find an efficient way to clean up unneeded routes.
   //The rules of the game:
   //1. find out all the routes which doesnot match the current gateway ip.
   //2. Delete all of them in a single go.(perhaps using some scoping rules).
   //
   force_del_default_route(ifc);
   nl_cache_refill(m_clisocketId,m_route_cache);

   if (routeexists(ifc, gateway,family,priority))
   {
      nl_cache_foreach_filter(m_route_cache, (struct nl_object *)route, modify_route_cb, &routeArgs);
   }
   else
   {
      cout<<"Default route is not visible, but invoked to change priority. Adding default route with gateway : "
	  <<gateway.c_str()
	  <<", changing priority to: " 
	  <<setpriority<<endl;

      rtnl_route_set_priority(route,setpriority);
      struct nl_addr * dstAddr = NULL; 
      string addrStr = "::/0";
      if (family == AF_INET)
      {
         addrStr = "0.0.0.0";
      }
      nl_addr_parse(addrStr.c_str(),family,&dstAddr);
      rtnl_route_set_dst(route,dstAddr);
      int err = rtnl_route_add(m_clisocketId,route,0);
      if (err < 0)
      {
         cout <<"Unable to add route: "<<nl_geterror(err)<<endl;
      }
      nl_addr_put(dstAddr);
   }
   rtnl_route_put(route);
   nl_addr_put(gwAddr);
}
void NetLinkIfc::deleteinterfaceroutes(string ifc, unsigned int family)
{
   std::lock_guard<std::recursive_mutex> guard(g_state_mutex);

   struct rtnl_route *route;
   int nf = 0;

   route = nl_cli_route_alloc();
   nl_cache_refill(m_clisocketId,m_link_cache);
   nl_cache_refill(m_clisocketId,m_route_cache);

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

static void force_del_default_route(string ifaceName) {

    cout << "Inside force_del_default_route for interface " << ifaceName.c_str() << endl;
    std::string cmd = "/sbin/ip -6 route del default dev " + ifaceName;
    cout << "Switching to fallback route delete with ip commands" << endl;
    cout << "Exec command: " << cmd.c_str() << endl;
    system(cmd.c_str());
}

void modify_route_cb(struct nl_object *obj, void *arg)
{
   cout<<"ENTERED MODIFY ROUTE CB"<<endl;
   struct rtnl_route *route = (struct rtnl_route*)nl_object_priv(obj);
   nlargs* pargs = (nlargs*) arg;


   rtnl_route_set_priority(route,*(unsigned int*)pargs->linkInfo);
   int err = rtnl_route_add((struct nl_sock *)pargs->socketId, route, 0);
   if (err < 0)
   {
      cout <<"Unable to add route: "<<nl_geterror(err)<<endl;
   }
}
void update_route_present(struct nl_object *obj, void *arg)
{
   cout<<"ENTERED ROUTE PRESENT CB"<<endl;
   nlargs* pargs = (nlargs*) arg;
   if (pargs->linkInfo != NULL)
   {
      *(bool*)pargs->linkInfo = true;
   }
}
void update_user_route_present(struct nl_object *obj, void *arg)
{
   cout<<"ENTERED USER ROUTE PRESENT CB"<<endl;

   const int cDEFAULT_PRIORITY = 1024;
   struct rtnl_route *route = (struct rtnl_route*)nl_object_priv(obj);
   if (rtnl_route_get_priority(route) != cDEFAULT_PRIORITY )
   {
      nlargs* pargs = (nlargs*) arg;
      if (pargs->linkInfo != NULL)
      {
         *(bool*)pargs->linkInfo = true;
      }
   }
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
   nl_cache_refill(m_clisocketId,m_link_cache);

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
    nl_cache_refill(m_clisocketId,m_link_cache);
    nl_cache_refill(m_clisocketId,m_addr_cache);
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
    nl_cache_refill(m_clisocketId, m_link_cache);
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

    nl_cache_refill(m_clisocketId, m_route_cache);
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

    auto it = m_interfaceMap.find(r.interface_index);
    if (it != m_interfaceMap.end())
    {
        interface = it->second;
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
