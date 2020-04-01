BEGIN {
	tot = 0;
	FS="[ :]"
}

{
	tot += $4 * 1000000000 + $5;
}

END {
	print tot / 1000000000 / NR
}
