# test
RDMA test code
## Source Code
```bash
git clone https://github.com/rlawjd10/test.git
```
## Environment Setup
1. install package
```bash
sudo su
chsh -s /bin/bash
apt-get update
apt-get install rdma-core ibverbs-utils perftest
```
2. mellanox NIC Firmware update
</br> note : It doesn't matter to see "Failed to update Firmware". This takes about 8 minutes
```
sh ./script/installMLNX.sh
```
```
sh ./script/installMLNX2.sh
```
3. checking
```
ibv_devinfo
ibv_devices
rdma link
ibstat
ip addr show


# server
ib_write_bw

# client
ib_write_bw <server IP>
```
