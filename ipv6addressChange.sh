#!/bin/sh
##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2018 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################
## invocation format
## add ipv4 interfacename address global
. /etc/device.properties

cmd=$1
mode=$2
ifc=$3
addr=$4
flags=$5

IPTABLE_CMD="/usr/sbin/iptables -w "

if [ $mode == "ipv6" ]; then
     IPTABLE_CMD="/usr/sbin/ip6tables -w "
fi

if [ "x$cmd" == "xadd" ] && [ "x$flags" == "xglobal" ]; then
   if [ $mode == "ipv6" ]; then
     touch /tmp/estb_ipv6
   else
     touch /tmp/estb_ipv4
  fi
fi

if [ "x$cmd" == "xdelete" ] && [ "x$flags" == "xglobal" ]; then
   if [ $mode == "ipv6" ]; then
     rm -f  /tmp/estb_ipv6
   else
     rm -f /tmp/estb_ipv4
  fi
fi

echo "Recived address Notificatio, cmd = $cmd, mode = $mode,  $IFC= $ifc, addr = $addr, flags = $flags"

if [ $ifc == "$WIFI_INTERFACE" ] || [ $ifc == "$MOCA_INTERFACE" ] || [ $ifc == "$LAN_INTERFACE" ] || [ $ifc == "${WIFI_INTERFACE}:0" ] || [ $ifc == "$[MOCA_INTERFACE]:0" ] || [ $ifc == "$[LAN_INTERFACE]:0" ]; then

   if [ "x$cmd" == "xadd" ] && [ "x$flags" == "xglobal" ]; then
     $IPTABLE_CMD -I INPUT -s $addr -p tcp --dport 22 -j ACCEPT
     $IPTABLE_CMD -I OUTPUT -o lo -p tcp -s $addr -d $addr -j ACCEPT
     tr69agent_startup_script_pid=`ps -ef | grep '\/usr\/bin\/start.sh' | tr -s " " | cut -d ' ' -f2`
     if [ "x$tr69agent_startup_script_pid" != "x" ]; then
        kill -9 $tr69agent_startup_script_pid
     fi
     tr69agent_pid=`pidof dimclient`
     if [ "x$tr69agent_pid" != "x" ]; then
        kill -9 $tr69agent_pid
     fi
   fi

   if [ "x$cmd" == "xdelete" ] && [ "x$flags" == "xglobal" ]; then
     $IPTABLE_CMD -D INPUT -s $addr -p tcp --dport 22 -j ACCEPT
     $IPTABLE_CMD -D OUTPUT -o lo -p tcp -s $addr -d $addr -j ACCEPT
   fi
   
   if [ -d /opt/logs ] && [ $mode == "ipv6" ]; then
      ra_enabled=`sysctl -n net.ipv6.conf.$ifc.accept_ra`
      if [ "$ra_enabled" == "1" ]; then
       echo "Address : $addr, is $cmd ed using SLAAC(RA) for interface $ifc" >> /opt/logs/netsrvmgr.log
      else
       echo "Address : $addr, is $cmd ed for interface $ifc" >> /opt/logs/netsrvmgr.log
      fi
   fi

   # Refresh device cache info
   if [ -f /lib/rdk/getDeviceDetails.sh ]; then
       sh /lib/rdk/getDeviceDetails.sh 'refresh' 'all' &
   fi
fi
