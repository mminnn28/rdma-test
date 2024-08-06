# test
RDMA test code

### The [cloudlab](https://docs.cloudlab.us/hardware.html#%28part._apt-cluster%29) cluster

- (d6515 nodes) Dual-port Mellanox ConnectX-5 100 GB NIC (PCIe v4.0)
- Software Requirements: Ubuntu 22.04, Mellanox OFED 5.8-5.1.1.2

## Source Code
```shell
git clone https://github.com/rlawjd10/rdma-test.git
```
## Environment Setup
1. Set bash as the default shell
```shell
sudo su
```
```shell
cd rdma-test
```
2. Install [Mellanox-OFED driver](https://network.nvidia.com/products/infiniband-drivers/linux/mlnx_ofed/)
```shell
sh ./script/installMLNX.sh
```
After reboot, use the following command:
```shell
sudo /etc/init.d/openibd restart
```

3. benchmarking
```shell
ibv_devinfo
ibv_devices
rdma link
ibstat
ip addr show


# server
ib_send_bw -d mlx4_0 -i 1 -F --report_gbits

# client
ib_send_bw -d mlx4_0 -i 1 -F --report_gbits <server IP>
```
