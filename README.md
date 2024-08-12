# RDMA-test
RDMA test code

### The [cloudlab](https://docs.cloudlab.us/hardware.html) cluster

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
3. RDMA Device Status and Configuration
```shell
# To check the status and configuration of RDMA devices, use the following commands:

# Display information about the RDMA devices
ibv_devinfo

# List the RDMA devices on the system
ibv_devices

# Display the InfiniBand device status
ibstat

# Show the link status of RDMA devices
rdma link
```
```shell
root@node-0:/users/Jeongeun# ibv_devices
    device          	   node GUID
    ------          	----------------
    mlx5_0          	1c34da030041cab4
    mlx5_1          	1c34da030041cab5


# Bring up the first port enp65s0f0np0
root@node-0:/users/Jeongeun# ip link set enp65s0f0np0 up

```

3. Benchmarking (BW Test)
```shell
# server
ib_send_bw -d mlx5_0 -i 1 -F --report_gbits

# client
ib_send_bw -d mlx5_0 -i 1 -F --report_gbits <server IP>
```
