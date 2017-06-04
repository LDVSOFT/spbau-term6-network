#!/bin/bash
#
# Script, which creates empty network namespce with loopback UP

if [[ -z $1 ]]; then
	echo "USAGE: ./netnsct.sh NEW_NETWORK_NS_NAME"
	exit 1
fi


echo "--> Deleting netns [$1] if exists..." && \
sudo ip netns del $1 2> /dev/null || true && \
echo "OK. Creating network namespace with name [$1]..." && \
sudo ip netns add $1 && \
echo "OK. Setting loopback UP..." && \
sudo ip netns exec $1 ip link set dev lo up && \
echo "OK!"
