#!/bin/sh

for test_name in fib arit
do
    echo "Running $test_name"
    ./run.sh "./tests/$test_name.pdasm" "./tests/$test_name.expected.txt"
done
