#/bin/bash

. /lib/rdk/utils.sh

LOG_FILE=$LOG_PATH/net_monitor_log.txt

Uptime_Threshold=600

if [ "$2" = "ipv6" ] && [ "$3" = "wlan0" ] && [ "$5" = "global" ]; then
  echo "`/bin/timestamp` Action:"$1 >> $LOG_FILE
  echo "`/bin/timestamp` Addr_type:"$2 >> $LOG_FILE
  echo "`/bin/timestamp` Interface:"$3 >> $LOG_FILE
  echo "`/bin/timestamp` Address:"$4 >> $LOG_FILE
  echo "`/bin/timestamp` Scope:"$5 >> $LOG_FILE
fi

if [ "$1" = "delete" ] && [ "$2" = "ipv6" ] && [ "$3" = "wlan0" ] && [ "$5" = "global" ]; then
    Device_Uptime=$(Uptime)
    if [ $Device_Uptime -ge $Uptime_Threshold ]; then
        echo "`/bin/timestamp` After ten minutes executing netmonitor recovery script file to recover ipv6 address" >> $LOG_FILE
        touch /opt/recover_ipv6
        sh /lib/rdk/netmonitor_recovery.sh &
    fi
fi

if [ -f "/opt/recover_ipv6" ] && [ "$1" = "add" ] && [ "$2" = "ipv6" ] && [ "$3" = "wlan0" ] && [ "$5" = "global" ]; then
    echo "`/bin/timestamp` Delete recoveripv6 file after adding the ipv6 address" >> $LOG_FILE
    rm /opt/recover_ipv6
fi
