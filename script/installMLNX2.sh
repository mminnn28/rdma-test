#!/bin/bash

if [ ! -d "tmp" ]; then
	mkdir tmp
fi

sudo su
chsh -s /bin/bash
apt-get update
apt-get install rdma-core ibverbs-utils perftest

cd tmp
wget https://content.mellanox.com/ofed/MLNX_OFED-4.9-7.1.0.0/MLNX_OFED_LINUX-4.9-7.1.0.0-ubuntu18.04-x86_64.tgz
tar -xzvf MLNX_OFED_LINUX-4.9-7.1.0.0-ubuntu18.04-x86_64.tgz
cd MLNX_OFED_LINUX-4.9-7.1.0.0-ubuntu18.04-x86_64

sudo ./mlnxofedinstall --force
sudo /etc/init.d/openibd restart
sudo /etc/init.d/opensmd restart
