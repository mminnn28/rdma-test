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


root@node-0:/users/Jeongeun# rdma -d link 
link mlx5_0/1 state DOWN physical_state DISABLED netdev enp65s0f0np0 netdev_index 8 
link mlx5_1/1 state DOWN physical_state DISABLED netdev enp65s0f1np1 netdev_index 9


# Bring up the first port enp65s0f0np0
root@node-0:/users/Jeongeun# ip link set enp65s0f0np0 up
root@node-0:/users/Jeongeun# rdma -d link 
link mlx5_0/1 state ACTIVE physical_state LINK_UP netdev enp65s0f0np0 netdev_index 8 
link mlx5_1/1 state DOWN physical_state DISABLED netdev enp65s0f1np1 netdev_index 9


# After the system reboot, the IP address 10.10.1.x has been assigned to this interface
root@node-1:/users/Jeongeun# ifconfig
enp65s0f0np0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 9000
        inet 10.10.1.1  netmask 255.255.255.0  broadcast 10.10.1.255
        inet6 fe80::1e34:daff:fe41:cb5c  prefixlen 64  scopeid 0x20<link>
        ether 1c:34:da:41:cb:5c  txqueuelen 1000  (Ethernet)
        RX packets 0  bytes 0 (0.0 B)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 12  bytes 976 (976.0 B)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0

```

3. benchmarking
```shell
# server
ib_send_bw -d mlx4_0 -i 1 -F --report_gbits

# client
ib_send_bw -d mlx4_0 -i 1 -F --report_gbits <server IP>
```
