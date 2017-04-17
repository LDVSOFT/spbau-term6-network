#!/bin/bash
#
# Script, which sets up network namespace with 
# impairment imitation on the loopback device

if [[ -z $1 ]]; then
	echo "USAGE: ./netnsct.sh NEW_NETWORK_NS_NAME"\
	     "[DELAYms] [DROP_PROBABILITYms] [REORDER_PROBABILITYms]"
	exit 1
fi

DELAY="${2:-100ms}"
DROP_PROB="${3:-30%}"
REORDER_PROB="${4:-30%}"

echo "--> Deleting netns [$1] if exists..." && \
sudo ip netns del $1 2> /dev/null || true && \
echo "OK. Creating network namespace with name [$1]..." && \
sudo ip netns add $1 && \
echo "OK. Setting loopback UP..." && \
sudo ip netns exec $1 ip link set dev lo up && \
echo "OK. Configuring delay = [$DELAY]; " && \
sudo ip netns exec $1 tc qdisc del dev lo root 2> /dev/null || true && \
sudo ip netns exec $1 tc qdisc add dev lo root handle 1: netem delay $DELAY 10ms && \
echo "OK. Configuring drop prob: [$DROP_PROB]; reorder prob: [$REORDER_PROB]" && \
sudo ip netns exec $1 tc qdisc add dev lo parent 1: handle 2: netem \
    delay 10ms reorder $REORDER_PROB loss $DROP_PROB && \
echo "OK!"
