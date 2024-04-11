#!/bin/sh

if [ $# -lt 2 ]
then
	echo "Too few arguments, aborting."
    exit 1
fi

mkdir -p $(dirname $1)
echo $2 > $1
