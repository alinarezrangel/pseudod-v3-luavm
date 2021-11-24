#!/bin/sh

for test_name in fib arit envs par procs str2num txtbuscar fmt boole
do
    echo "Running $test_name"
    if ./run.sh "./tests/$test_name.pdasm" "./tests/$test_name.expected.txt"; then
        echo "Success"
    else
        echo "Failure"
        exit 1
    fi
done
