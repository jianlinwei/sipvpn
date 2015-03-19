#!/bin/sh

# sample server down script

iptables -t nat -D POSTROUTING -s 10.12.5.0/24 -j MASQUERADE
iptables -D FORWARD -s 10.12.5.0/24 -j ACCEPT
iptables -D FORWARD -d 10.12.5.0/24 -j ACCEPT
iptables -t mangle -D FORWARD -p tcp -m tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu

ip addr flush dev $tunif
ip link set $tunif down
