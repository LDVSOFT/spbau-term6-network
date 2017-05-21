#!/bin/bash

if [[ `whoami` != 'root' ]] ; then
	echo 'You will be prompted with sudo to add credentials.'
	exec sudo $0
	exit 1
fi

if type setcap &> /dev/null ; then
	echo 'Found setcap; using libcap backend.'
	for f in `find bin/ -type f` ; do
		setcap CAP_NET_RAW+ep "$f" || exit 1
	done
else
	echo 'No setcap found; falling back to SUID backend.'
	for f in `find bin/ -type f` ; do
		 chown root "$f" || exit 1
		 chmod u+s "$f" || exit 1
	done
fi
