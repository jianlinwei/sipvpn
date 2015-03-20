#!/bin/sh

# sample client down script

if ! [[ $(ip route get $server | grep local) ]]; then
	ip route del $(ip route get $server | sed -n 1p)
fi
#ip route del 0.0.0.0/1 via 10.12.5.2 dev $tunif
#ip route del 128.0.0.0/1 via 10.12.5.2 dev $tunif

ip addr flush dev $tunif
ip link set $tunif down
