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
#ifndef _NETLINKIFC_H_
#define _NETLINKIFC_H_

#include <map>
#include <thread>
#include <mutex>
#include <string>
#include <list>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <arpa/inet.h>
#include <vector>
#include <algorithm>
#include "subscribers.h"
extern "C" 
{
#include <netlink/cli/utils.h>
#include <netlink/cli/addr.h>
#include <netlink/cli/route.h>
#include <netlink/cli/link.h>
}



using namespace std;
typedef enum 
{
    eNETIFC_STATE_INIT =0,
    eNETIFC_STATE_POPULATE_IFC,
    eNETIFC_STATE_POPULATE_ADDRESS,
    eNETIFC_STATE_RUNNING
} netifcState;

typedef enum
{
   eNETIFC_EVENT_ADD_IFC =0,
   eNETIFC_EVENT_DELETE_IFC,
   eNETIFC_EVENT_ADD_IPADDR,
   eNETIFC_EVENT_DELETE_IPADDR,
   eNETIFC_EVENT_ADD_IP6ADDR,
   eNETIFC_EVENT_DELETE_IP6ADDR,
   eNETIFC_EVENT_ADD_IP6ROUTE,
   eNETIFC_EVENT_ADD_IPROUTE,
   eNETIFC_EVENT_DELETE_IP6ROUTE,
   eNETIFC_EVENT_DELETE_IPROUTE,
   eNETIFC_EVENT_ADD_LINK,
   eNETIFC_EVENT_DELETE_LINK,
   eNETIFC_EVENT_REINITIALIZE,
   eNETIFC_EVENT_DONE,
   eNETIFC_EVENT_UNKNOWN = -1,
} netifcEvent;

typedef struct ipaddr
{
   string address;
   bool global;
   unsigned int family;
   unsigned short prefix;
   
   bool operator ==(const ipaddr& rhs)
   {
      if ((address == rhs.address) && (global == rhs.global) && (family == rhs.family) && (prefix == rhs.prefix))
      {
         return true;
      }
      return false;
   }
} ipaddr;

typedef struct linkargs
{
   struct nl_sock* socketId;
   void* linkInfo;
} linkargs;
    
   

class NetLinkIfc
{
private:
	typedef void (NetLinkIfc::*memfunc)(string);
	map<netifcState,map<netifcEvent,memfunc> > stateMachine;
        netifcState m_state;
        netifcEvent m_event;

        map<int,string> m_interfaceMap;
	multimap<int,ipaddr> m_ipAddrMap;
        NetLinkIfc ();
        void populateinterfacecompleted(string);
        void populateaddrescompleted(string);
        void addipaddr(string);
        void deleteipaddr(string);
        void addip6addr(string);
        void deleteip6addr(string);
        void addiproute(string);
        void addip6route(string);
        void deleteiproute(string);
        void deleteip6route(string);
        void addlink(string);
        void deletelink(string);
        void processAddrMsg(struct nlmsghdr* nlh);
        void processLinkMsg(struct nlmsghdr* nlh);
        void processRouteMsg(struct nlmsghdr* nlh);
        void tokenize(string& inputStr, vector<string>& tokens);
        bool addipaddrentry(multimap<int,ipaddr>& mmap,int ifindex,ipaddr& addr);
        bool deleteaddrentry(multimap<int,ipaddr>& mmap,int ifindex,ipaddr& addr);
        //void reinitialize(string);
        void publish(NlType type,string args);


        static recursive_mutex g_state_mutex;
        static mutex g_instance_mutex;
        static NetLinkIfc* pInstance;
        static void receiveMsg(NetLinkIfc* instance);
        static int receiveNewMsg(struct nl_msg *msg, void *arg);
        

        // LIstening socket and its related addresses etc.
        struct nl_sock * m_socketId;
        struct nl_sock * m_clisocketId;
        struct nl_cache * m_link_cache; 
        struct nl_cache * m_route_cache;
        struct nl_cache * m_addr_cache;
        list<int> monitoredmsgtypes;
        list<Subscriber*> m_subscribers;
public:
        virtual ~NetLinkIfc();
        void runStateMachine(netifcEvent event,string args);
        void initialize();
        static NetLinkIfc* get_instance();
        void run(bool forever=true);

        //Clean up of ipaddresses bound to the interface.
        void deleteinterfaceip(string ifc, unsigned int family);
        void deleteinterfaceroutes(string ifc, unsigned int family);
        void activatelink(string ifc);
        bool getIpaddr(string ifc,unsigned int family,vector<string>& ipaddr);

        inline void addSubscriber(Subscriber* s){ m_subscribers.push_back(s); }
        inline void deleteSubscriber(Subscriber* s)
        { 
          auto it = std::find_if(m_subscribers.begin(),m_subscribers.end(),
                                 [&]( const Subscriber* v ){ return v == s;} );

          if (it != m_subscribers.end())
          {
             m_subscribers.erase(it);
          }
        }
};

extern "C" {
void delete_addr_cb(struct nl_object *obj, void *arg);
void delete_route_cb(struct nl_object *obj, void *arg);
void modify_link_cb(struct nl_object *obj, void *arg);
void get_ip_addr_cb(struct nl_object *obj, void *arg);
}

#endif// _NETLINKIFC_H_
