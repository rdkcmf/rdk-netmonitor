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

(/bin/busybox kill -STOP $$; /bin/busybox kill -CONT $$)
if [ "x$cmd" == "xadd" ] && [ "x$flags" == "xglobal" ]; then
   if [ ! -f /tmp/estb_ipv4 ] && [ ! -f /tmp/estb_ipv6 ];
   then
      if [ -f /lib/rdk/logMilestone.sh ]; then
        sh /lib/rdk/logMilestone.sh "IP_ACQUISTION_COMPLETED:$ifc"
      fi
   fi
   if [ $mode == "ipv6" ]; then
     if [[ $addr == fd* ]] || [[ $addr == fc* ]]; then
       echo " It is ULA address, no need to create IPv6 flags"
     else
       if [ $SKY_EPG_SUPPORT == "true" ]; then
          if [ $ifc != "dobby0" ]; then
              echo " Creating IPv6 flags for $ifc"
              touch /tmp/estb_ipv6
              touch /tmp/addressaquired_ipv6
          fi
       else
          echo " Creating IPv6 flags for $ifc"
          touch /tmp/estb_ipv6
          touch /tmp/addressaquired_ipv6
       fi
     fi
   else
     if [[ $addr == 169.254* ]] || [[ $addr == 192.168.18.10 ]]; then
	     echo " Ignoring zero config IP or default ip assigned for  $ifc"
     else
	     echo " Creating IPv4 flags for $ifc"
	     touch /tmp/estb_ipv4
	     touch /tmp/addressaquired_ipv4
     fi
  fi
fi

if [ "x$cmd" == "xdelete" ] && [ "x$flags" == "xglobal" ]; then
   if [ $mode == "ipv6" ]; then
     if [[ $addr == fd* ]] || [[ $addr == fc* ]]; then
       echo " It is ULA address, no need to clear IPv6 flags"
     else
         pd=`pwd`
         cd /sys/class/net
         globalip=""
         for i in `ls`
         do
             if [ "$i" != "dobby0" ]; then
                 lglobal=`/sbin/ip -6 addr show dev $i | grep global`
             fi
             globalip="$globalip $lglobal"
         done
         cd $pd

       # cleaning up all the spaces
       gIp=`echo $globalip | xargs`
       if [ "x$gIp" = "x" ]; then
          echo " Box was previously in IPv6 mode and now in IPv4 mode. Clearing IPv6 flags for $ifc"
          rm -f  /tmp/estb_ipv6
          rm -f /tmp/addressaquired_ipv6
       fi
     fi
   else
       pd=`pwd`
       cd /sys/class/net
       globalip=""
       for i in `ls`
       do
           if [ "$i" != "dobby0" ]; then
               lglobal=`/sbin/ip -4 addr show dev $i | grep global`
           fi
           globalip="$globalip $lglobal"
       done
       cd $pd
       # cleaning up all the spaces
       gIp=`echo $globalip | xargs`
       if [ "x$gIp" = "x" ]; then
          echo " Box was previously in IPv4 mode and now in IPv6 mode. Clearing IPv4 flags for $ifc"
          rm -f /tmp/estb_ipv4
          rm -f /tmp/addressaquired_ipv4
       fi
  fi
fi

echo "Received address Notification, cmd = $cmd, mode = $mode,  $IFC= $ifc, addr = $addr, flags = $flags"

uptime=`cat /proc/uptime | awk '{print $1}'`

if [ $ifc == "$WIFI_INTERFACE" ] || [ $ifc == "$MOCA_INTERFACE" ] || [ $ifc == "$LAN_INTERFACE" ] || [ $ifc == "${WIFI_INTERFACE}:0" ] || [ $ifc == "${MOCA_INTERFACE}:0" ] || [ $ifc == "${LAN_INTERFACE}:0" ]; then

   if [ "x$cmd" == "xadd" ] && [ "x$flags" == "xglobal" ]; then
     # Check for ESTB_INTERFACE from device.properties is matching with IP acquired interface.
     echo "Received global $mode address for $ifc interface, uptime is $uptime milliseconds"
     touch "/tmp/${mode}_${flags}"

     $IPTABLE_CMD -I INPUT -s $addr -p tcp --dport 22 -j ACCEPT
     $IPTABLE_CMD -I OUTPUT -o lo -p tcp -s $addr -d $addr -j ACCEPT
     systemctl reset-failed tr69agent.service
     systemctl restart tr69agent.service
   fi

   if [ "x$cmd" == "xdelete" ] && [ "x$flags" == "xglobal" ]; then
     $IPTABLE_CMD -D INPUT -s $addr -p tcp --dport 22 -j ACCEPT
     $IPTABLE_CMD -D OUTPUT -o lo -p tcp -s $addr -d $addr -j ACCEPT
     if [ -f "/tmp/${mode}_${flags}" ]; then
         rm -rf "/tmp/${mode}_${flags}"
     fi
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

if [ $ifc == "$WIFI_INTERFACE" ] || [ $ifc == "$MOCA_INTERFACE" ] || [ $ifc == "${WIFI_INTERFACE}:0" ] || [ $ifc == "${MOCA_INTERFACE}:0" ]; then

  if [ "x$flags" == "xglobal" ]; then
       if [ ! -f /tmp/Dropbear_restart_disabled ]; then
            echo "Restarting Dropbear due to global ip address changes" >> /opt/logs/dropbear.log
            systemctl reset-failed dropbear.service
            systemctl restart dropbear.service &
       else
            echo "Preventing Dropbear restarts" >> /opt/logs/dropbear.log
       fi
  fi
fi
