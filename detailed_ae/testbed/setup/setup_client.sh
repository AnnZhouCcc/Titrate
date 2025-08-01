#!/bin/bash

# Update system
sudo apt-get update -y

# Install requirements
sudo apt install -y python3-pip python3-dev pipx
pipx install gdown 
pipx ensurepath
source ~/.bashrc

# Set up Chromium
wget https://dl.google.com/linux/chrome/deb/pool/main/g/google-chrome-stable/google-chrome-stable_128.0.6613.84-1_amd64.deb
sudo dpkg -i google-chrome-stable_128.0.6613.84-1_amd64.deb
sudo apt-get install -f -y

# Download and set up chromedriver
wget https://storage.googleapis.com/chrome-for-testing-public/128.0.6613.84/linux64/chromedriver-linux64.zip
unzip chromedriver-linux64.zip
sudo mv chromedriver-linux64/chromedriver /usr/bin/chromedriver
sudo chown root:root /usr/bin/chromedriver
sudo chmod +x /usr/bin/chromedriver

echo "Selenium setup complete!"