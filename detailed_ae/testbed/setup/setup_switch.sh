#!/bin/bash

# Titrate Switch Setup Script
# This script sets up the environment for the Titrate switch.
# Intended to be run on Ubuntu 22.04 LTS.

# Update system
sudo apt-get update -y

# Install go
wget https://go.dev/dl/go1.24.2.linux-amd64.tar.gz
sudo tar -C /usr/local -xzf go1.24.2.linux-amd64.tar.gz
sudo rm go1.24.2.linux-amd64.tar.gz

# Add Go to PATH
echo 'export PATH=$PATH:/usr/local/go/bin' >> ~/.bashrc
echo 'export GOPATH=$HOME/go' >> ~/.bashrc
echo 'export PATH=$PATH:$GOPATH/bin' >> ~/.bashrc
source ~/.bashrc
go version

# Install Clang
sudo apt-get install -y clang build-essential llvm-dev \
  linux-headers-$(uname -r) libbpf-dev llvm libelf-dev \
  linux-tools-common linux-tools-generic bpfcc-tools \
  gcc-multilib
clang --version 

# Setup the network interfaces
routes=(
  "192.168.6.1 enX1"    # client1
  "192.168.6.11 enX2"   # server
  "192.168.6.3 enX3"    # client2
  "192.168.6.5 enX4"    # client3
  "192.168.6.7 enX5"    # client4
  "192.168.6.9 enX6"    # client5
)

for pair in "${routes[@]}"; do
  sudo ip route add ${pair% *} dev ${pair#* }
done

echo "Titrate Switch setup complete!"