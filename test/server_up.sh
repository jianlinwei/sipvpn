#!/bin/sh

# sample server up script

sysctl -w net.ipv4.ip_forward=1 >/dev/null

ip link set $tunif up
ip link set $tunif mtu $mtu
ip addr add 10.12.5.1/24 dev $tunif

iptables -t nat -A POSTROUTING -s 10.12.5.0/24 -j MASQUERADE
iptables -A FORWARD -s 10.12.5.0/24 -j ACCEPT
iptables -A FORWARD -d 10.12.5.0/24 -j ACCEPT
iptables -t mangle -A FORWARD -p tcp -m tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu
