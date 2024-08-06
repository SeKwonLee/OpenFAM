#!/bin/bash

rm -rf /mnt/hugetlbfs/*
hugeadm --pool-pages-max 2MB:0
umount /mnt/hugetlbfs
cat /sys/devices/system/node/node*/meminfo | fgrep Huge
