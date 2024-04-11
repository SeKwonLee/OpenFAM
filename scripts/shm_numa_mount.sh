#!/bin/bash

sudo mount -t tmpfs -o size=64g,mpol=bind:0 tmpfs /dev/shm
sudo chmod -R 777 /dev/shm
