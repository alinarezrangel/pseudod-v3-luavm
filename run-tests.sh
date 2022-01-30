#!/usr/bin/env lunash

alset tests fib arit envs par procs str2num txtbuscar fmt boole sumador spush inittonull proccont contbase

for test_name in @tests [
    echo 'Running' $test_name
    local status
    !>status[] ./run.sh ./tests/$test_name.pdasm ./tests/$test_name.expected.txt
    if $(numeq status 0) [
        echo Success
    ] [
        echo Failure
        : @(exit 1)
    ]
]
