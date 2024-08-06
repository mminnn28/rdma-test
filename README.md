# test
RDMA test code

### The [cloudlab](https://docs.cloudlab.us/hardware.html#%28part._apt-cluster%29) cluster

- (d6515 nodes) Dual-port Mellanox ConnectX-5 100 GB NIC (PCIe v4.0)
- Software Requirements: Ubuntu 22.04, Mellanox OFED 5.8-5.1.1.2

## Source Code
```bash
git clone https://github.com/rlawjd10/rdma-test.git
```
## Environment Setup
1. Set bash as the default shell
```bash
sudo su
cd rdma-test
```
2. Install [Mellanox-OFED driver](https://network.nvidia.com/products/infiniband-drivers/linux/mlnx_ofed/)
```
sh ./script/installMLNX.sh
```
3. Resize disk partition
   
   Since the d6515 nodes remain a large unallocated disk partition by default, you should resize the disk partition using the following command:

```shell
sh ./script/resizePartition.sh
```
```shell
resize2fs /dev/sda1
```
4. benchmarking
```
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
