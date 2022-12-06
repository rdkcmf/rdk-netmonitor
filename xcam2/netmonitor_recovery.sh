#/bin/bash

. /lib/rdk/utils.sh
. /lib/rdk/t2Shared_api.sh

LOG_FILE=$LOG_PATH/net_monitor_log.txt

#we need to ensure to have more sleep time so that we can see whether we get RA from gateway or not .
#If camera misses to get the RA within time period ,
#we can have xw3_monitor being called

sleep 60
if [ -f "/opt/recover_ipv6" ]; then
    echo "`/bin/timestamp` Reloading xw3_monitor to recover ipv6 address " >> $LOG_FILE
    t2CountNotify "SYS_INFO_XW3MonReload"
    xw3_monitor reload
fi
