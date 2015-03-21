#!/bin/sh

# available environment variables:
#   $mode
#   $server
#   $port
#   $tunif
#   $mtu
#   $up
#   $down

# delete route
if ! [[ $(ip route get $server | grep local) ]]; then
	ip route del $(ip route get $server | sed -n 1p)
fi
ip route del 0.0.0.0/1 dev $tunif
ip route del 128.0.0.0/1 dev $tunif

# clean up tun interface
ip addr flush dev $tunif
ip link set $tunif down
