#!/bin/bash

huge_page_size=$[2*1024*1024]

num_pages=$[($1*1024*1024*1024)/$huge_page_size]

hugetlbfs_size=$1

node_id=$2

#hugeadm --pool-pages-min 2MB:8192
echo ${num_pages} > /sys/devices/system/node/node${node_id}/hugepages/hugepages-2048kB/nr_hugepages
mkdir -p /mnt/hugetlbfs; mount -t hugetlbfs -o size=${hugetlbfs_size}G none /mnt/hugetlbfs
chmod -R 777 /mnt/hugetlbfs
cat /sys/devices/system/node/node*/meminfo | fgrep Huge
