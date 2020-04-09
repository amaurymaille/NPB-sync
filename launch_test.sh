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

if [[ -f ${FILENAME}.log ]]
then
	./sync > ${FILENAME}.log.$(expr 1 + $(ls -l ${FILENAME}.log.* | wc -l))
else
	./sync > ${FILENAME}.log
fi
