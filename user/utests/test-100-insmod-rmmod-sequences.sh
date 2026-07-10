#!/bin/sh

for i in $(seq 1 100); do
    echo "Iteration $i"

    if ! insmod build/kmod/ufedm.ko mtds=0; then
        echo "insmod failed at iteration $i"
        exit 1
    fi

    sleep 2

    if ! rmmod ufedm; then
        echo "rmmod failed at iteration $i"
        exit 1
    fi

done
