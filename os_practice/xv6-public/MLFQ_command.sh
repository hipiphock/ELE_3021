#!/bin/bash
make clean
make SCHED_POLICY=MLFQ_SCHED CPUS=1 -j
make fs.img
qemu-system-i386 -nographic -serial mon:stdio -hdb fs.img xv6.img -smp 1 -m 512
