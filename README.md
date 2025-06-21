# TWAMP - Two-Way Active Measurement Protocol

A lightweight implementation of the Two-Way Active Measurement Protocol (TWAMP) for network latency and performance measurement.

## Features

- **RFC 5357 Compliant**: Full implementation of TWAMP as specified in RFC 5357
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

## References

- [RFC 5357 - A Two-Way Active Measurement Protocol (TWAMP)](https://tools.ietf.org/html/rfc5357)
- [RFC 4656 - A One-way Active Measurement Protocol (OWAMP)](https://tools.ietf.org/html/rfc4656)
