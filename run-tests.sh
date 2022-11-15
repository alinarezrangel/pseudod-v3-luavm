#!/usr/bin/env lunash

alset all_tests fib arit envs par procs str2num txtbuscar fmt boole sumador spush inittonull proccont contbase tailcall retn einit procargs procorder arreglo_comoTexto arreglo_en arreglo_fijarEn arreglo_redimensionar arreglo_clonar variadic variadic_slice objs_creacion objs_atributos modulos cmprefeq cmprefeq-unspec tail-pila
alset tests

if $(numeq (arrlen args) 0) [
    aset tests @all_tests
] [
    aset tests @args
]

for test_name in @tests [
    echo Running $test_name
    local status
    !>status[] ./run.sh ./tests/$test_name.pdasm ./tests/$test_name.expected.txt
    if $(numeq status 0) [
        echo Success
    ] [
        echo Failure
        if $(numeq status 2) [
            echo Recording
            rr record ./sample
        ]
        : @(exit 1)
    ]
]
