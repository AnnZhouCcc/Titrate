#!/bin/bash
# fwd_switch.sh - Configure Ubuntu switch VM for routing and NAT
# Usage: sudo ./fwd_switch.sh

# Update system
sudo apt-get update -y

# Exit on error
set -e

# Check if running as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (use sudo)"
  exit 1
fi

# Define interfaces
EXTERNAL_IF="enX0"
INTERNAL_IFS=("enX1" "enX2" "enX3" "enX4" "enX5" "enX6")

echo "=== Switch VM Router Configuration ==="
echo "External interface: $EXTERNAL_IF"
echo "Internal interfaces: ${INTERNAL_IFS[@]}"

# Enable IP forwarding
echo "Enabling IP forwarding..."
sysctl -w net.ipv4.ip_forward=1
echo "net.ipv4.ip_forward=1" > /etc/sysctl.d/99-forwarding.conf
sysctl -p /etc/sysctl.d/99-forwarding.conf

# Set up NAT and firewall rules
echo "Setting up NAT and firewall rules..."
# Clear existing rules
iptables -F
iptables -t nat -F
iptables -X

# Set default policies
iptables -P INPUT ACCEPT
iptables -P FORWARD ACCEPT
iptables -P OUTPUT ACCEPT

# Enable NAT
iptables -t nat -A POSTROUTING -o $EXTERNAL_IF -j MASQUERADE

# Allow established connections and traffic from all internal networks
for INTERNAL_IF in "${INTERNAL_IFS[@]}"; do
  echo "Setting up forwarding for $INTERNAL_IF..."
  iptables -A FORWARD -i $INTERNAL_IF -o $EXTERNAL_IF -j ACCEPT
done
iptables -A FORWARD -m state --state RELATED,ESTABLISHED -j ACCEPT

# Make iptables rules persistent
echo "Installing iptables-persistent to save rules..."
apt-get update -y
DEBIAN_FRONTEND=noninteractive apt-get install -y iptables-persistent

# Save the rules
echo "Saving iptables rules..."
netfilter-persistent save

# Create status script
cat > /usr/local/bin/router-status << EOF
#!/bin/bash
echo "==== Router Status ===="
echo "IP Forwarding: \$(cat /proc/sys/net/ipv4/ip_forward)"
echo ""
echo "Network Interfaces:"
echo "External: "
ip addr show $EXTERNAL_IF | grep -E "inet |$EXTERNAL_IF"
echo "Internal: "
for IF in ${INTERNAL_IFS[@]}; do
  ip addr show \$IF 2>/dev/null | grep -E "inet |\$IF" || echo "\$IF not found"
done
echo ""
echo "NAT Rules:"
iptables -t nat -L -n
echo ""
echo "Forward Rules:"
iptables -L FORWARD -n
EOF

chmod +x /usr/local/bin/router-status

echo ""
echo "=== Setup Complete ==="
echo "This switch is now configured as a router with NAT"
echo "Run '/usr/local/bin/router-status' to check status"
echo ""
/usr/local/bin/router-status