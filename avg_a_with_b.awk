BEGIN {
	tot = 0;
	FS="[ :]";
}

{
	tot += $6 * 1000000000 + $7;
}

END {
	print tot / 1000000000 / NR;
}
