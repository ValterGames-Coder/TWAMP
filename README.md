# TWAMP - Two-Way Active Measurement Protocol
A lightweight implementation of the Two-Way Active Measurement Protocol (TWAMP) for network latency and performance measurement.

## Features
- **RFC 5357 Compliant**: Full implementation of TWAMP as specified
- **Bidirectional Measurements**: Accurate round-trip time and one-way delay measurements
- **Low Overhead**: Minimal system resource usage for continuous monitoring
- **Systemd Integration**: Easy service management with systemd
- **Cross-Platform**: Supports major Linux distributions (Debian/Ubuntu, Fedora/RHEL/CentOS)
- **Configurable**: Flexible configuration options for various network environments

## Prerequisites
### System Requirements
- Linux-based operating system (Debian/Ubuntu 18.04+ or Fedora/RHEL/CentOS 7+)
- Root or sudo privileges for installation
- Network connectivity for measurements
- **Time synchronization enabled** (NTP/chrony) for accurate measurements

Tested on: Fedora 41, Ubuntu 22.04, Ubuntu 24.04

### Development Dependencies
#### Debian/Ubuntu
```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config libsystemd-dev
```

#### Fedora/RHEL/CentOS
```bash
# Fedora / RHEL / CentOS 8+
sudo dnf install -y gcc gcc-c++ cmake git pkg-config systemd-devel
# RHEL / CentOS 7
sudo yum install -y gcc gcc-c++ cmake3 git pkg-config systemd-devel
```

## Installation
### Download the RPM/DEB packages from the [Releases page](https://github.com/ValterGames-Coder/TWAMP/releases):

### Or build:
```bash
git clone https://github.com/ValterGames-Coder/TWAMP.git
cd TWAMP
```

#### Server Installation
```bash
cd server
mkdir build && cd build
cmake ..
sudo make install
sudo systemctl start twamp-server.service
sudo systemctl status twamp-server.service
```

#### Client Installation
```bash
cd client
mkdir build && cd build
cmake ..
sudo make install
```

## Time Synchronization Setup
⚠️ **Important**: Both TWAMP client and server must have properly synchronized clocks for accurate timestamp measurements. Invalid or identical timestamps indicate synchronization issues.

### Enable Time Synchronization
#### Option 1: Using systemd-timesyncd (recommended for most systems)
```bash
sudo systemctl enable systemd-timesyncd
sudo systemctl start systemd-timesyncd
sudo timedatectl set-ntp true
```

#### Option 2: Using chrony (alternative)
```bash
# Install chrony
sudo apt install chrony  # Debian/Ubuntu
sudo dnf install chrony  # Fedora/RHEL/CentOS

# Stop systemd-timesyncd first
sudo systemctl stop systemd-timesyncd
sudo systemctl disable systemd-timesyncd

# Enable chrony
sudo systemctl enable chronyd
sudo systemctl start chronyd
```

### Verify Time Synchronization
```bash
# Check synchronization status
timedatectl status

# For chrony users - check NTP sources
sudo chronyc sources -v

# For systemd-timesyncd users
timedatectl show-timesync --all
```

**Expected output should show:**
- `System clock synchronized: yes`
- `NTP service: active`
- Multiple NTP sources with low offset values

### Troubleshooting Time Sync Issues
If you see "Invalid timestamps detected" errors:

1. **Check if time sync is running:**
   ```bash
   timedatectl status
   ```

2. **Force synchronization (if needed):**
   ```bash
   # For chrony
   sudo chronyc makestep
   
   # For systemd-timesyncd
   sudo systemctl restart systemd-timesyncd
   ```

3. **Monitor synchronization process:**
   ```bash
   # For chrony
   sudo chronyc tracking
   
   # For systemd-timesyncd
   journalctl -u systemd-timesyncd -f
   ```

## Configuration
### Server Configuration
The server configuration file is typically located at `/etc/twamp/twamp-server.conf`. Key configuration options include:
```ini
# TWAMP Server Configuration
# Control port (default: 862)
control_port = 862
# Test port (default: 863)
test_port = 863
# Maximum sessions (default: 100)
max_sessions = 100
# Session timeout in minutes (default: 5)
session_timeout = 5
```

### Firewall Configuration
Ensure the TWAMP port (default 862) is open:

#### Debian/Ubuntu (ufw)
```bash
sudo ufw allow 862/tcp
sudo ufw allow 863/udp
```

#### Fedora/RHEL/CentOS (firewalld)
```bash
sudo firewall-cmd --permanent --add-port=862/tcp
sudo firewall-cmd --permanent --add-port=863/udp
sudo firewall-cmd --reload
```

## Running
### Server Operations
```bash
sudo systemctl start/stop/restart twamp-server.service
```

**View server logs:**
```bash
sudo journalctl -u twamp-server.service -f
```

### Client Usage
**Basic measurement:**
```bash
twamp-client <server_ip>:<port>
```

**Advanced options:**
```bash
twamp-client <server_ip>:<port> -c 5 -i 500 -s
```

**Command line options:**
- `-c <count>`: Number of test packets to send (default: 10)
- `-i <interval>`: Interval between packets in ms (default: 1000)
- `-s`: Short output (only summary after all packets)

### Common Issues and Solutions
- **"Invalid timestamps detected"**: Ensure both client and server have time synchronization enabled
- **Connection refused**: Check if server is running and firewall ports are open
- **Identical timestamp values**: Increase packet interval or check system clock resolution

## References
- [RFC 5357 - A Two-Way Active Measurement Protocol (TWAMP)](https://tools.ietf.org/html/rfc5357)
- [RFC 4656 - A One-way Active Measurement Protocol (OWAMP)](https://tools.ietf.org/html/rfc4656)
