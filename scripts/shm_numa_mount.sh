#!/bin/bash

NUMA_NODE=$1

rm -rf /dev/shm/*
sudo umount /dev/shm
sudo mount -t tmpfs -o size=64g,mpol=bind:${NUMA_NODE} tmpfs /dev/shm
sudo chmod -R 777 /dev/shm
