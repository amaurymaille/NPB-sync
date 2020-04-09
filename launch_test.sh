#!/bin/bash

if [[ -z "$OMP_NUM_THREADS" ]]
then
	export OMP_NUM_THREADS=8
fi

./sync > $(date "+%F_%T").log
