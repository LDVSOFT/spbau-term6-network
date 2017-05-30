#!/bin/bash

PS4=

if [[ `whoami` != 'root' ]] ; then
	exec sudo -p '[sudo] To add capabilities root rights are required; password for %u: ' $0
	exit 1
fi

if type setcap &> /dev/null ; then
	echo 'Found setcap; using libcap backend.'
	for f in `find bin/ -type f` ; do
		(set -x; setcap CAP_NET_RAW+ep "$f") || exit 1
	done
else
	echo 'No setcap found; falling back to SUID backend.'
	for f in `find bin/ -type f` ; do
		(set -x; chown root "$f" && chmod u+s "$f") || exit 1
	done
fi
