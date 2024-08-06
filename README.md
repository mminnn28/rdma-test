# test
RDMA test code

### cluster [cloudlab](https://docs.cloudlab.us/hardware.html#%28part._apt-cluster%29)

- (d6515) Dual-port Mellanox ConnectX-5 100 GB NIC (PCIe v4.0)

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
```
sh ./script/resizePartition.sh
```
```
resize2fs /dev/sda1
```
5. benchmarking
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
