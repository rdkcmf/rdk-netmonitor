route -n
ip -6 route
echo "Input Arguments : $* "
echo "Route is available"
if [ ! -f /tmp/route_available ];then
    touch /tmp/route_available
fi
