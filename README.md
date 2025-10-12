# Palladium Core

**Official Website:** [palladiumblockchain.net](https://palladiumblockchain.net)
## Overview

Palladium Core is a decentralized digital currency forked from Bitcoin, specifically designed to serve the palladium market ecosystem. Built upon the proven Bitcoin protocol foundation, Palladium Core delivers enhanced security, efficiency, and transparency for palladium-related transactions.

### Key Features

- **Security**: Advanced cryptographic techniques ensure transaction security and fund protection
- **Efficiency**: Optimized blockchain parameters provide fast and reliable transaction processing
- **Transparency**: Open-source architecture enables community inspection and contribution
- **Market-Focused**: Tailored features specifically designed for palladium industry requirements
- **Decentralized**: Peer-to-peer network with no central authority

## Quick Start

### Installation

1. **Download and Install**: Get the latest Palladium Core wallet from our [releases page](https://github.com/davide3011/palladiumcore/releases/tag/v1.3.0)
2. **Configure**: Create the `palladium.conf` configuration file (see [Configuration](#advanced-configuration) section below)
3. **Launch the Core**: Start the Palladium Core application (includes automatic network synchronization)

### Configuration

For enhanced connectivity and performance, you can create a configuration file:

**Windows**: Navigate to `%appdata%/Palladium/`  
**Linux**: Navigate to `/home/[username]/.palladium/`  
**macOS**: Navigate to `~/Library/Application Support/Palladium/`

Create a file named `palladium.conf`. For users running a full node or requiring advanced functionality, we recommend the following comprehensive configuration:

```conf
# Core Settings
txindex=1
server=1
listen=1
daemon=1
discover=1

# RPC Configuration
rpcuser=your_username_here
rpcpassword=your_secure_password_here

# Network Ports
port=2333
rpcport=2332

# Connection Settings
maxconnections=50
fallbackfee=0.0001
rpcallowip=192.168.0.0/16
rpcbind=192.168.0.0/16

# Trusted Nodes
addnode=89.117.149.130:2333
addnode=66.94.115.80:2333
addnode=173.212.224.67:2333

# ZeroMQ Configuration
zmqpubrawblock=tcp://0.0.0.0:28334
zmqpubrawtx=tcp://0.0.0.0:28335
zmqpubhashblock=tcp://0.0.0.0:28332
```

**Security Warning**: Replace `your_username_here` and `your_secure_password_here` with strong, unique credentials.

### Configuration Parameters Explained

| Parameter | Description | Recommended Value |
|-----------|-------------|-------------------|
| `txindex` | Maintains full transaction index | `1` (enabled) |
| `server` | Enables RPC server | `1` (enabled) |
| `listen` | Accept incoming connections | `1` (enabled) |
| `daemon` | Run in background mode | `1` (enabled) |
| `discover` | Enable peer discovery | `1` (enabled) |
| `maxconnections` | Maximum peer connections | `50` |
| `fallbackfee` | Default transaction fee | `0.0001` PLM/kB |
| `port` | P2P network port | `2333` |
| `rpcport` | RPC server port | `2332` |

## Building from Source

### Dependencies

- **C++ Compiler**: GCC 7+ or Clang 5+
- **Build Tools**: Make, Autotools
- **Libraries**: Boost, OpenSSL, libevent, ZeroMQ

### Build Instructions

```bash
# Clone repository
git clone https://github.com/davide3011/palladiumcore.git
cd palladiumcore

# Generate build files
./autogen.sh

# Configure build
./configure

# Compile
make -j$(nproc)

# Install (optional)
sudo make install
```

## Contributing

We welcome contributions from the community! Please read our [Contributing Guidelines](CONTRIBUTING.md) before submitting pull requests.

### Development Process

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## Security

Security is paramount in cryptocurrency development. Please report security vulnerabilities privately to our security team. See [SECURITY.md](SECURITY.md) for details.

## License

Palladium Core is released under the terms of the MIT license. See [COPYING](COPYING) for more information.

## Support

- **Documentation**: [Wiki](https://github.com/palladium-coin/palladium/wiki)
- **Issues**: [GitHub Issues](https://github.com/palladium-coin/palladium/issues)
- **Community**: [Discord](https://discord.gg/palladium) | [Telegram](https://t.me/palladiumcoin)
- **Website**: [palladium-coin.com](https://palladium-coin.com)

## Acknowledgments

Palladium Core is built upon the Bitcoin Core codebase. We thank the Bitcoin Core developers and the broader cryptocurrency community for their foundational work.

---

**Disclaimer**: Cryptocurrency investments carry risk. Please do your own research and invest responsibly.
