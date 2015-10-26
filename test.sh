#!/bin/bash

generate_threads_list ()
{
    echo 1 2 4 8 16 32 64
#   echo 1 `seq 4 4 40 `
}

gold=measure-$1-list-P`hostname | cut -c11`-`date +%H-%M@%d-%m-%y`

# how many times to repeat each test
num_runs=10
if [ $# -ge 2 ];
    then num_runs=$2
fi

num_initial_keys=5000
if [ $# -ge 3 ];
    then num_initial_keys=$3
fi
((key_range=num_initial_keys*2))

test_file_AOA=./test-AOA
test_file_MOA=./test-MOA
test_file_NoRecl=./test-NoRecl
test_file_RC=./test-RC
test_file_NoReclMalloc=./test-NoReclMalloc

log_file_AOA=$gold-AOA
log_file_MOA=$gold-MOA
log_file_RC=$gold-RC
log_file_NoReclMalloc=$gold-NoReclMalloc
log_file_NoRecl=$gold-NoRecl


if [ ! -f $test_file_AOA ]; then
    echo "Test file $test_file_AOA does not exist. Exiting"
    exit
fi
if [ ! -f $test_file_MOA ]; then
    echo "Test file $test_file_AOA does not exist. Exiting"
    exit
fi
if [ ! -f $test_file_NoRecl ]; then
    echo "Test file $test_file_NoRecl does not exist. Exiting"
    exit
fi
if [ ! -f $test_file_RC ]; then
    echo "Test file $test_file_RC does not exist. Exiting"
    exit
fi
if [ -f $log_file_AOA ]; then
    echo "Log file $log_file_RoF_IF already exists. Exiting"
    exit
fi
if [ -f $log_file_NoRecl ]; then
    echo "Log file $log_file_NoReclPalloc already exists. Exiting"
    exit
fi



for f in 0.8
do

    for i in $(generate_threads_list)
    do

        args="$num_initial_keys $f 0"

        for j in `seq $num_runs`
        do
            $test_file_AOA $i $args 0 $j >> $log_file_AOA 2>&1
            $test_file_MOA $i $args 0 $j >> $log_file_MOA 2>&1
            $test_file_RC $i $args 0 $j >> $log_file_RC 2>&1
            $test_file_NoRecl $i $args $j >> $log_file_NoRecl 2>&1
#                $test_file_NoReclMalloc $i $args >> $log_file_NoReclMalloc 2>&1
        done
        printf "$i "
    done
    printf " finish frac $f"
    printf "\n"
done

printf "\n"
