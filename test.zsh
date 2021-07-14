#!/usr/bin/env zsh

set -euC
trap 'rm -f test.jpg' EXIT
export TIMEFMT=$'%*E total \t %J'
for f in images/*; do
	time imlib2_conv "$f"       "test.jpg"
	time imlib2_conv "test.jpg" "/tmp/$(basename "$f")"
	rm "test.jpg" "/tmp/$(basename "$f")"
	echo
done
