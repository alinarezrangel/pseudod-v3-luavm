#!/bin/sh

for test_name in fib arit envs par procs str2num
do
    echo "Running $test_name"
    ./run.sh "./tests/$test_name.pdasm" "./tests/$test_name.expected.txt"
    if [ "$?" = "0" ]; then
        echo "Success"
    else
        echo "Failure"
        exit 1
    fi
done
