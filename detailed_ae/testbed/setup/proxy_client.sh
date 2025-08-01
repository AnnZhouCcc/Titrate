#!/bin/bash

# fwd_client.sh
# Forward all non-SSH traffic through a switch node while preserving SSH connectivity.
# Usage: sudo ./fwd_client.sh <switch_vm_ip>

# Update system
sudo apt-get update -y

set -e  # Exit on any error

# Check if run as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (use sudo)"
  exit 1
fi

# Check for required argument
if [ $# -ne 1 ]; then
  echo "Usage: $0 <switch_vm_ip>"
  echo "Example: $0 192.168.1.2"
  exit 1
fi

SWITCH_IP="$1"
SSH_IP=$(who am i | awk '{print $NF}' | tr -d '()')
SSH_DEV=$(ip route get $SSH_IP | grep -oP 'dev \K[^ ]+' || echo "")
SSH_GW=$(ip route get $SSH_IP | grep -oP 'via \K[^ ]+' || ip route | grep "default" | head -n1 | awk '{print $3}')

echo "=== Network Configuration Setup ==="
echo "Switch VM IP: $SWITCH_IP"
echo "SSH Client IP: $SSH_IP"
echo "SSH Device: $SSH_DEV"
echo "SSH Gateway: $SSH_GW"

# Verify connectivity to the switch VM
echo "Testing connectivity to Switch VM..."
if ! ping -c 3 $SWITCH_IP > /dev/null 2>&1; then
  echo "ERROR: Cannot ping Switch VM at $SWITCH_IP"
  echo "Please verify the IP address and connectivity between VMs"
  exit 1
fi
echo "Connectivity to Switch VM confirmed."

# Determine the interface connected to the switch VM
SWITCH_DEV=$(ip route get $SWITCH_IP | grep -oP 'dev \K[^ ]+')
echo "Interface connected to Switch VM: $SWITCH_DEV"

# Create SSH routing table
echo "Setting up SSH preservation routes..."
grep -q "^200 ssh$" /etc/iproute2/rt_tables || echo "200 ssh" >> /etc/iproute2/rt_tables

# Clear any existing rules for this table
ip rule show | grep "lookup ssh" | while read rule; do
  ip rule del ${rule#*:}
done

# Add rule to use the SSH table for SSH connections
ip rule add from all to $SSH_IP table ssh

# Add routes to the SSH table to ensure SSH connectivity
if ip route show table ssh &>/dev/null; then
  ip route flush table ssh
fi
ip route add default via $SSH_GW dev $SSH_DEV table ssh

# Set up routes for all other traffic
echo "Setting up default routing through Switch VM..."
ip route add default via $SWITCH_IP dev $SWITCH_DEV || true

# Create persistence script
echo "Creating persistence script..."
mkdir -p /etc/networkd-dispatcher/routable.d/

cat > /etc/networkd-dispatcher/routable.d/50-preserve-ssh << EOF
#!/bin/bash
# This script preserves SSH connectivity during network changes

SSH_IP="$SSH_IP"
SSH_DEV="$SSH_DEV"
SSH_GW="$SSH_GW"
SWITCH_IP="$SWITCH_IP"
SWITCH_DEV="$SWITCH_DEV"

# Restore SSH rule
ip rule add from all to \$SSH_IP table ssh 2>/dev/null || true

# Restore SSH route
ip route flush table ssh 2>/dev/null
ip route add default via \$SSH_GW dev \$SSH_DEV table ssh 2>/dev/null

# Restore default route through Switch VM
ip route add default via \$SWITCH_IP dev \$SWITCH_DEV 2>/dev/null || true
EOF

chmod +x /etc/networkd-dispatcher/routable.d/50-preserve-ssh

# Create a systemd service for persistence across reboots
cat > /etc/systemd/system/routing-setup.service << EOF
[Unit]
Description=Setup routing through switch VM
After=network.target

[Service]
Type=oneshot
ExecStart=/etc/networkd-dispatcher/routable.d/50-preserve-ssh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl enable routing-setup.service
systemctl start routing-setup.service

echo ""
echo "=== Setup Complete ==="
echo "All non-SSH traffic is now routed through $SWITCH_IP"
echo "SSH connectivity has been preserved via $SSH_GW"
echo "Configuration will persist across reboots"