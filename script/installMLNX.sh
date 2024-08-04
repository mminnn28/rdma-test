#!/bin/bash

apt-get update -y
apt-get install rdma-core ibverbs-utils perftest

if [ ! -d "tmp" ]; then
	mkdir tmp
fi

cd tmp
wget https://content.mellanox.com/ofed/MLNX_OFED-5.8-5.1.1.2/MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu18.04-x86_64.tgz
tar -xzvf MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu18.04-x86_64.tgz
cd MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu18.04-x86_64

sudo ./mlnxofedinstall --force
sudo /etc/init.d/openibd restart
sudo /etc/init.d/opensmd restart

# install cmake
cd install
if [ ! -f cmake-3.16.8.tar.gz ]; then
  wget https://cmake.org/files/v3.16/cmake-3.16.8.tar.gz
fi
if [ ! -d "./cmake-3.16.8" ]; then
  tar zxf cmake-3.16.8.tar.gz
  cd cmake-3.16.8 && ./configure && make -j 4 && sudo make install
fi
cd ..
