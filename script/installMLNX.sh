#!/bin/bash


mode="$1"
ubuntu_version=$(lsb_release -r -s)

# Update and install necessary packages
apt-get update -y
apt-get install rdma-core ibverbs-utils perftest -y

# Change the default shell to /bin/bash for the current user
chsh -s /bin/bash $USER

if [ ! -d "tmp" ]; then
	mkdir tmp
fi

cd tmp

# Download and extract the appropriate Mellanox OFED package
if [ "$ubuntu_version" = "18.04" ]; then
  wget https://content.mellanox.com/ofed/MLNX_OFED-5.8-5.1.1.2/MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu18.04-x86_64.tgz
  tar -xzvf MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu18.04-x86_64.tgz
  cd MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu18.04-x86_64
elif [ "$ubuntu_version" = "22.04" ]; then
  wget https://content.mellanox.com/ofed/MLNX_OFED-5.8-5.1.1.2/MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu22.04-x86_64.tgz
  tar -xzvf MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu22.04-x86_64.tgz
  cd MLNX_OFED_LINUX-5.8-5.1.1.2-ubuntu22.04-x86_64
else
  echo "Wrong ubuntu distribution for $mode!"
  exit 0
fi
echo $mode $ubuntu_version $ofed_fid


sudo ./mlnxofedinstall --force
sudo /etc/init.d/openibd restart
sudo /etc/init.d/opensmd restart

ibv_devices


