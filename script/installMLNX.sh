#!/bin/bash

if [ ! -d "tmp" ]; then
	mkdir tmp
fi

cd tmp
wget http://www.mellanox.com/downloads/ofed/MLNX_OFED-5.8-5.1.1.2/MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu18.04-x86_64.tgz
tar -xvf MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu18.04-x86_64.tgz
cd MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu18.04-x86_64

sudo ./mlnxofedinstall  --force
sudo /etc/init.d/openibd restart
sudo /etc/init.d/opensmd restart
