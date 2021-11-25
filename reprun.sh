#!/usr/bin/sh

s=0
while [ "$s" != 1 ]; do
    echo "Trying"
    sleep 1
    ./sample
    s="$?"
done
echo "Found crash!"
