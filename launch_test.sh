#!/bin/bash

if [[ -z "$OMP_NUM_THREADS" ]]
then
	if [[ -z $1 ]]
	then
		export OMP_NUM_THREADS=8
	else
		export OMP_NUM_THREADS=$1
	fi
fi

FILENAME=$(date "+%F_%T")
FILENAME=${FILENAME//:/}

FULL_FILENAME=${FILENAME}.$(hostname).log

if [[ -f $FULL_FILENAME ]]
then
	FULL_FILENAME=$FULL_FILENAME.$(expr 1 + $(ls -l $FULL_FILENAME.* | wc -l))
fi


echo "// OMP_NUM_THREADS=$OMP_NUM_THREADS" > $FULL_FILENAME
./sync >> $FULL_FILENAME
