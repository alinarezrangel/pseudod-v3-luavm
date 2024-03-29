#!/usr/bin/env lunash

local status
lset file $(nth args 1)
lset expfile $(nth args 2)
alset asm lua5.4 main.lua -W all -o sample.c -C prevent_warnings -C target_compiler=gcc $file
echo @asm
!>status[] @asm
if $(not (numeq status 0)) [
    : @(exit 3)
]
make
!>status[] ./sample | ?[0,1] grep -vE '^[|]' | lunash-tool write-file test-output.txt
if $(not (numeq status 0)) [
    : @(exit 2)
]
echo diff $expfile test-output.txt
?>status[0,1] diff $expfile test-output.txt
: @(exit status)
