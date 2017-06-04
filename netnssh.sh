#!/bin/bash
#
# Script unshares network namespace and runs bash as $USER

if [[ -z $1 ]]; then
	echo "USAGE: ./netnssh.sh NETWORK_NS_NAME"
	exit 1
fi

echo "Unsharing to [$1] netns..." && \
sudo ip netns exec $1 sudo -u $USER NOTMUX=y /bin/bash
