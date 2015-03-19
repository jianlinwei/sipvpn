#!/bin/sh

# sample client up script

ip link set $tunif up
ip link set $tunif mtu $mtu
ip addr add 10.12.5.2/24 dev $tunif

if ! [[ $(ip route get $server | grep local) ]]; then
	ip route replace $(ip route get $server | grep via)
fi
ip route add 0.0.0.0/1 via 10.12.5.2 dev $tunif
ip route add 128.0.0.0/1 via 10.12.5.2 dev $tunif
