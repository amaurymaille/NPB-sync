#¡/bin/bash

if [[ "$#" -ne 1 ]]
then
	echo "Missing filename"
	exit 1
fi

if [[ ! -f "$1" ]]
then
	echo "Invalid filename"
	exit 1
fi

grep -v '^//' "$1" | python3 parse_results.py --ratios --csv-dst "${1/.log/.csv}"
