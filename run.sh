#!/usr/bin/env lunash

local status
lset file $(nth args 1)
lset expfile $(nth args 2)
echo lua5.4 main.lua -W all -o sample.c $file
lua5.4 main.lua -W all -o sample.c $file
make
./sample | ?[0,1] grep -vE '^[|]' | lunash-tool write-file test-output.txt
echo diff $expfile test-output.txt
?>status[0,1] diff $expfile test-output.txt
: @(exit status)
