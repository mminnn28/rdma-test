# test
RDMA test code
## Source Code
```bash
git clone https://github.com/rlawjd10/test.git
```
### cluster
[cloudlab](https://docs.cloudlab.us/hardware.html#%28part._apt-cluster%29)
</br>
- (r320) Mellanox MX354A Dual port FDR CX3 adapter (Apt cluster)
- (d6515) Dual-port Mellanox ConnectX-5 100 GB NIC (PCIe v4.0)
## Environment Setup
1. Set bash as the default shell
```bash
sudo su
chsh -s /bin/bash
cd test
```
2. Install [Mellanox-OFED driver](https://network.nvidia.com/products/infiniband-drivers/linux/mlnx_ofed/)
```
sh ./script/installMLNX.sh
```

```
sh ./script/installMLNX2.sh
```
4. network setting
```sh
sudo nano /etc/netplan/01-netcfg.yaml
```
```yaml
network:
  version: 2
  renderer: networkd
  ethernets:
    ib0:
      addresses:
        - 192.168.1.20/24
      gateway4: 192.168.1.1
      nameservers:
        addresses:
          - 8.8.8.8
          - 8.8.4.4

```
```
sudo netplan apply
```
6. checking
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
4. benchmarking
```
#server
ib_send_bw -d mlx4_0 -i 1 -F --report_gbits

#client
ib_send_bw -d mlx4_0 -i 1 -F --report_gbits 192.168.1.10
```
