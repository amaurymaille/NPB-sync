#¡/bin/bash

if [[ "$#" -lt 1 ]]
then
	echo "Missing filename"
	exit 1
fi

if [[ ! -f "$1" ]]
then
	echo "Invalid filename"
	exit 1
fi

FILENAME="$1"
shift
grep -v '^//' "$FILENAME" | python3 parse_results.py "$@"
