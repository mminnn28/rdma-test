#!/bin/bash

# Function to prompt for network interface
prompt_interface() {
    echo "Available network interfaces:"
    ip -o link show | awk -F': ' '{print $2}' | sed 's/@.*$//'
    read -p "Enter the name of the network interface from the list above (e.g., eth0, enp0s25): " interface
}

# Function to verify if the interface exists
verify_interface() {
    if ! ip link show "$interface" &> /dev/null; then
        echo "Error: Network interface $interface does not exist."
        exit 1
    fi
}

# Function to install required packages
install_packages() {
    # Check if packages are already installed
    if ! dpkg -s rdma-core ibverbs-providers perftest infiniband-diags rxe &> /dev/null; then
        sudo apt-get update
        sudo apt-get install -y rdma-core ibverbs-providers perftest infiniband-diags rxe
    fi
}

# Function to clone and build RDMA core
build_rdma_core() {
    echo "Cloning and building RDMA core..."
    if [ ! -d "rdma-core" ]; then
        sudo apt-get install -y build-essential cmake gcc libudev-dev libnl-3-dev libnl-route-3-dev ninja-build pkg-config valgrind python3-dev cython3 python3-docutils pandoc
        git clone https://github.com/linux-rdma/rdma-core.git
        cd rdma-core
        bash build.sh
        sudo cp -r * /usr
        sudo ldconfig
    fi
}

# Main script starts here
prompt_interface
verify_interface

echo "Network interface $interface is valid. Proceeding with the script..."

install_packages
build_rdma_core

echo "Loading kernel modules for RDMA..."
sudo modprobe rdma_ucm
sudo modprobe rdma_rxe

ibv_devices

echo "Setup complete."
