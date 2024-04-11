#!/bin/sh

if [ $# -lt 2 ]
then
	echo "Too few arguments, aborting."
    exit 1
fi

if [ ! -d $1 ]
then
	echo "Arg1 is not a directory, aborting."
    exit 1
fi

NUMFILES=$(find $1 -type  f 2>/dev/null | wc -l)
NUMLINES=$(grep -r $2 $1 2>/dev/null | wc -l)
echo "The number of files are ${NUMFILES} and the number of matching lines are ${NUMLINES}"
