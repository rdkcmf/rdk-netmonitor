#/bin/bash

. /lib/rdk/utils.sh
. /lib/rdk/t2Shared_api.sh

LOG_FILE=$LOG_PATH/net_monitor_log.txt

#we need to ensure to have more sleep time so that we can see whether we get RA from gateway or not .
#If camera misses to get the RA within time period ,
#we can have wpa_supplicant service to restart.

sleep 60
if [ -f "/opt/recover_ipv6" ]; then
    echo "`/bin/timestamp` Restarting netsrvmgr service to recover ipv6 address " >> $LOG_FILE
    t2CountNotify "WIFI_ERR_NetSrvMgr"
    /etc/init.d/netsrvmgr-service restart
fi
