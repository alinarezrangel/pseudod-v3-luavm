#!/bin/sh

echo lua5.4 main.lua -W all -o sample.c "$1"
lua5.4 main.lua -W all -o sample.c "$1"
make
./sample | grep -vE '^[|]' > test-output.txt
echo "diff \"$2\" test-output.txt"
diff "$2" test-output.txt
exit $?
