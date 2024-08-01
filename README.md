# test

- install package
```bash
sudo apt-get update
sudo apt-get install rdma-core ibverbs-utils perftest
```
- mellanox NIC Firmware update
```
wget http://www.mellanox.com/downloads/ofed/MLNX_OFED-<version>/MLNX_OFED_LINUX-<version>-<platform>.tgz
tar -xzvf MLNX_OFED_LINUX-<version>-<platform>.tgz
cd MLNX_OFED_LINUX-<version>-<platform>
sudo ./mlnxofedinstall --add-kernel-support
sudo /etc/init.d/openibd start
```
- test
```
ibv_devinfo

# server
ib_write_bw

# client
ib_write_bw <server IP>
```
