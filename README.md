# Silikangaroo 🦘

<div align="center">

**High-Performance Bitcoin Puzzle Solver for Apple Silicon**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-macOS%20(Apple%20Silicon)-lightgrey)](https://www.apple.com/mac/)
[![Language](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)](https://isocpp.org/)

*Harnessing the power of Metal GPU acceleration to solve Bitcoin puzzles using Pollard's Kangaroo algorithm*

</div>

---

## 💰 Support This Project

**If this solver helps you find a key, consider donating!**

Building and maintaining this solver takes significant time and computational resources.
Your support helps keep this project alive and improving.

### Bitcoin Donation Address

```
bc1qwh9k7rlkgj0qfw8wgvqmj86h5m5ddphph2lzg3af0x5p5h9s6qms7a6aqp
```

<img src="https://api.qrserver.com/v1/create-qr-code/?size=200x200&data=bitcoin:bc1qwh9k7rlkgj0qfw8wgvqmj86h5m5ddphph2lzg3af0x5p5h9s6qms7a6aqp" alt="Bitcoin Donation QR Code" width="200"/>

**Every satoshi counts! Thank you for your support! 🙏**

---

## 🎯 What is Silikangaroo?

Silikangaroo is a specialized Bitcoin private key recovery tool that implements **Pollard's Kangaroo (Lambda) algorithm** with aggressive optimizations for **Apple Silicon chips** (M1/M2/M3/M4). It's designed to solve the famous [Bitcoin Puzzle Transaction](https://privatekeys.pw/puzzles/bitcoin-puzzle-tx) challenges by leveraging Metal GPU compute capabilities.

### Key Features

- 🚀 **Apple Silicon Native**: Compiled with `-mcpu=apple-m1` for maximum ARM64 performance
- ⚡ **Metal GPU Acceleration**: Custom Metal shaders with SIMD optimization for parallel point operations
- 🧮 **Advanced Arithmetic**: Batched modular inversion using Montgomery's algorithm (32-way SIMD)
- 🎲 **Pollard's Kangaroo**: Efficient collision-based search with distinguished points
- 🔐 **libsecp256k1**: Industry-standard secp256k1 curve operations
- 🧵 **Multi-threaded CPU Fallback**: OpenMP parallel workers for non-GPU mode
- 📊 **Real-time Statistics**: Live performance monitoring (jumps/sec, time elapsed)

## 🏗️ Architecture

```
┌─────────────────┐
│   Main Solver   │
└────────┬────────┘
         │
    ┌────┴────┐
    ▼         ▼
┌───────┐  ┌──────────┐
│  CPU  │  │   GPU    │
│ Mode  │  │  Metal   │
└───┬───┘  └────┬─────┘
    │           │
    ▼           ▼
┌─────────────────────┐
│  Kangaroo Workers   │
│  (Tame + Wild)      │
└──────────┬──────────┘
           ▼
    ┌──────────────┐
    │ Distinguished│
    │   Points     │
    └──────┬───────┘
           ▼
     ┌─────────┐
     │Collision│
     │  Check  │
     └────┬────┘
          ▼
     🎉 Solution!
```

## 📋 Prerequisites

### Hardware
- **Apple Silicon Mac** (M1, M1 Pro/Max/Ultra, M2, M3, M4, etc.)
- Recommended: 16GB+ RAM for larger puzzles

### Software
- **macOS**: Big Sur (11.0) or later
- **Xcode Command Line Tools**: `xcode-select --install`
- **Homebrew**: [Install here](https://brew.sh/)

### Dependencies

Install required libraries via Homebrew:

```bash
brew install libomp gmp cmake
```

## 🔨 Build Instructions

```bash
# Clone the repository
git clone https://github.com/yourusername/silikangaroo.git
cd silikangaroo

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Compile (use all available cores)
make -j$(sysctl -n hw.ncpu)
```

**Build outputs:**
- `silikangaroo` → Main solver executable
- `gen_key` → Key generation utility
- `default.metallib` → Compiled Metal kernels

## 🚀 Usage

### Basic Syntax

```bash
./silikangaroo <public_key> <start_range> <end_range> [threads] [--gpu]
```

**Parameters:**
- `public_key`: Compressed public key (33 bytes, hex)
- `start_range`: Search range start (hex or decimal)
- `end_range`: Search range end (hex or decimal)
- `threads`: (Optional) Number of CPU threads (default: all cores)
- `--gpu`: (Recommended) Enable Metal GPU acceleration

### Example: Puzzle 20

```bash
cd build
./silikangaroo \
  033c4a45cbd643ff97d77f41ea37e843648d50fd894b864b0d52febc62f6454f7c \
  0x80000 \
  0xFFFFF \
  8 \
  --gpu
```

**Expected Output:**
```
Silikangaroo v0.1.0 - M1 Optimized
Target: 033c4a45cbd643ff97d77f41ea37e843648d50fd894b864b0d52febc62f6454f7c
Range: [80000, fffff]
Mode: GPU Accelerated (Metal)
Range size: 524288
Sqrt(N): 724
DP Bits: 12 (1 in 4096)
...
Time: 42s | Jumps: 1048576 | Rate: 24.97 M/jumps/s

SUCCESS! Private Key Found!
Private Key: d2c55
```

### More Examples

**Puzzle 30:**
```bash
./silikangaroo \
  030d282cf2ff536d2c42f105d0b8588821a915dc3f9a05bd98bb23af67a2e92a5b \
  0x20000000 \
  0x3FFFFFFF \
  8 --gpu
# Expected key: 3d94cd64
```

**Puzzle 60:**
```bash
./silikangaroo \
  0348e843dc5b1bd246e6309b4924b81543d02b16c8083df973a89ce2c7eb89a10d \
  0x800000000000000 \
  0xFFFFFFFFFFFFFFF \
  8 --gpu
# Expected key: fc07a1825367bbe
```

## 🛠️ Helper Tool: gen_key

Generate a public key from a private key (useful for testing):

```bash
./gen_key <private_key_hex>
```

**Example:**
```bash
./gen_key d2c55

# Output:
# Private: 00000000000000000000000000000000000000000000000000000000000d2c55
# Public:  033c4a45cbd643ff97d77f41ea37e843648d50fd894b864b0d52febc62f6454f7c
```

## ⚙️ How It Works

### Pollard's Kangaroo Algorithm

The solver uses the **lambda method** for solving the Discrete Logarithm Problem (DLP) on the secp256k1 elliptic curve:

1. **Tame Kangaroos**: Start at the end of the range, jump forward
2. **Wild Kangaroos**: Start at the target public key, jump forward
3. **Distinguished Points**: Both types mark special points (low bits = 0)
4. **Collision Detection**: When DP sets intersect, calculate the private key

### Metal GPU Optimization

The Metal kernel (`src/kernels.metal`) implements:

- **256-bit Modular Arithmetic**: Custom uint256 type with mod operations
- **Point Addition**: Jacobian coordinate formulas for efficiency
- **Batched Inversion**: 32-way parallel Montgomery inversion (SIMD)
- **Shared Memory**: Jump table cached in threadgroup memory
- **Coalesced Access**: Optimized memory patterns for Apple GPU architecture

**Performance**: On M1 Max, achieves **~25-50 million jumps/sec** (varies by puzzle size).

## 📊 Performance Tuning

### GPU Batch Size
Modify in `Kangaroo.cpp` line 282:
```cpp
int gpuBatchSize = 65536;  // 2^16 parallel kangaroos
int stepsPerLaunch = 64;   // Steps before syncing
```

### Distinguished Point Bits
Auto-calculated, but can be manually set in `Kangaroo.cpp` line 168:
```cpp
dpBits = 12;  // 1 in 4096 points is distinguished
```

Higher = fewer collisions (more memory efficient), but slower detection.

## 🐛 Troubleshooting

### Build Errors

**Error: `gmp.h` not found**
```bash
brew install gmp
# If still failing:
export CPATH=/opt/homebrew/include
export LIBRARY_PATH=/opt/homebrew/lib
```

**Error: Metal compiler failed**
```bash
xcode-select --install
xcrun --show-sdk-path  # Should return a valid SDK path
```

### Runtime Issues

**Slow performance on GPU**
- Check Activity Monitor → GPU History (should show high utilization)
- Try different batch sizes (powers of 2: 32768, 65536, 131072)
- Ensure no other GPU-intensive apps are running

**"Invalid target public key"**
- Verify the public key is 66 hex chars (33 bytes compressed)
- Must start with `02` or `03`

## 🤝 Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

**Areas for improvement:**
- [ ] Support for multiple GPUs (Apple Silicon multi-die)
- [ ] Save/resume functionality
- [ ] Network clustering (distributed solving)
- [ ] Advanced jump table strategies
- [ ] Support for uncompressed public keys

## 📜 License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

## ⚠️ Disclaimer

This tool is for **educational and research purposes only**. It's designed for solving public Bitcoin puzzle challenges where the existence of a private key in a known range is intentional.

**DO NOT** use this tool to:
- Attempt to crack unknown Bitcoin wallets (computationally infeasible)
- Perform any illegal activities
- Harm others or violate their privacy

The authors assume no liability for misuse of this software.

## 🙏 Acknowledgments

- **Bitcoin Core Team**: For libsecp256k1
- **Pollard (1978)**: For the kangaroo algorithm
- **Apple**: For the Metal framework and incredible Apple Silicon chips
- **Community**: All contributors and testers

## 📚 References

- [Pollard's Kangaroo Algorithm](https://en.wikipedia.org/wiki/Pollard%27s_kangaroo_algorithm)
- [Bitcoin Puzzle Transaction](https://privatekeys.pw/puzzles/bitcoin-puzzle-tx)
- [Metal Programming Guide](https://developer.apple.com/metal/)
- [secp256k1 Curve Specifications](https://www.secg.org/sec2-v2.pdf)

---

<div align="center">

**Made with ❤️ for the Apple Silicon community**

If you found a puzzle solution using Silikangaroo, consider sharing your success story!

[⭐ Star this repo](https://github.com/yourusername/silikangaroo) • [🐛 Report Bug](https://github.com/yourusername/silikangaroo/issues) • [💡 Request Feature](https://github.com/yourusername/silikangaroo/issues)

</div>

