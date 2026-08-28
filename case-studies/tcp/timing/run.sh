#!/bin/bash

ITERS=100000

SUM=0
for i in `seq ${ITERS}`; do
    t1=$(./timing)
    t2=$(./timing)
    res=$((${t2} - ${t1}))
    SUM=$((${SUM} + ${res}))
    printf "[%06d] %llu - %llu = %llu\n" ${i} ${t2} ${t1} ${res}
done
AVG=$((${SUM}/${ITERS}))
echo "-- Average = ${AVG} --"
