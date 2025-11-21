# Installation Guide

This guide provides detailed instructions for installing and setting up Silikangaroo on your Apple Silicon Mac.

## System Requirements

### Hardware
- **Required**: Mac with Apple Silicon (M1, M1 Pro, M1 Max, M1 Ultra, M2, M3, M4 series)
- **Recommended RAM**: 16 GB or more (8 GB minimum)
- **Storage**: ~100 MB for source + build

### Software
- **macOS**: Big Sur (11.0) or later (Sonoma recommended)
- **Xcode Command Line Tools**: Required for Metal compiler
- **Homebrew**: Package manager for dependencies

## Step 1: Install Prerequisites

### 1.1 Install Xcode Command Line Tools

Open Terminal and run:

```bash
xcode-select --install
```

Follow the prompts to complete installation. Verify:

```bash
xcode-select -p
# Should output: /Library/Developer/CommandLineTools
```

### 1.2 Install Homebrew

If you don't have Homebrew installed:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

For Apple Silicon, add Homebrew to your PATH (if not done automatically):

```bash
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"
```

Verify:

```bash
brew --version
# Should output: Homebrew 4.x.x or later
```

### 1.3 Install Dependencies

```bash
brew install libomp gmp cmake
```

**What these are:**
- **libomp**: OpenMP support for multi-threading (Apple Clang doesn't include it by default)
- **gmp**: GNU Multiple Precision library for arbitrary precision arithmetic
- **cmake**: Build system generator

Verify installations:

```bash
brew list libomp gmp cmake
```

## Step 2: Download Silikangaroo

### Option A: Clone from GitHub

```bash
cd ~/  # Or wherever you want to install
git clone https://github.com/yourusername/silikangaroo.git
cd silikangaroo
```

### Option B: Download ZIP

1. Go to [GitHub repository](https://github.com/yourusername/silikangaroo)
2. Click "Code" → "Download ZIP"
3. Extract and navigate to folder:

```bash
cd ~/Downloads/silikangaroo-main  # Adjust path as needed
```

## Step 3: Build

### 3.1 Create Build Directory

```bash
mkdir build
cd build
```

### 3.2 Configure with CMake

**Release build (optimized, recommended):**

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

**Debug build (for development):**

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

### 3.3 Compile

Use all available CPU cores for faster compilation:

```bash
make -j$(sysctl -n hw.ncpu)
```

This will take 1-3 minutes depending on your Mac.

### 3.4 Verify Build

Check that executables were created:

```bash
ls -lh silikangaroo gen_key default.metallib
```

You should see:
- `silikangaroo` (~500 KB)
- `gen_key` (~400 KB)
- `default.metallib` (~50 KB)

## Step 4: Test Installation

### 4.1 Test Helper Tool

```bash
./gen_key d2c55
```

**Expected output:**
```
Private: 00000000000000000000000000000000000000000000000000000000000d2c55
Public:  033c4a45cbd643ff97d77f41ea37e843648d50fd894b864b0d52febc62f6454f7c
```

### 4.2 Test Solver (Quick Test)

Run Puzzle 20 (should complete in < 5 seconds):

```bash
./silikangaroo \
  033c4a45cbd643ff97d77f41ea37e843648d50fd894b864b0d52febc62f6454f7c \
  0x80000 \
  0xFFFFF \
  8 \
  --gpu
```

**Expected output (may vary):**
```
Silikangaroo v0.1.0 - M1 Optimized
Target: 033c4a45cbd643ff97d77f41ea37e843648d50fd894b864b0d52febc62f6454f7c
Range: [80000, fffff]
Mode: GPU Accelerated (Metal)
...
SUCCESS! Private Key Found!
Private Key: d2c55
```

If you see this, **installation is successful!** 🎉

## Step 5: Optional Setup

### 5.1 Add to PATH

To run `silikangaroo` from anywhere:

```bash
# Add to your shell config (~/.zshrc or ~/.bash_profile)
echo 'export PATH="$HOME/silikangaroo/build:$PATH"' >> ~/.zshrc
source ~/.zshrc

# Test
silikangaroo --help  # Should work from any directory
```

### 5.2 Create Alias

```bash
echo 'alias skg="silikangaroo"' >> ~/.zshrc
source ~/.zshrc

# Now you can use: skg <pubkey> <start> <end> --gpu
```

## Troubleshooting

### Issue: "gmp.h: No such file or directory"

**Solution:**

```bash
brew install gmp
export CPATH=/opt/homebrew/include
export LIBRARY_PATH=/opt/homebrew/lib
cd build && cmake .. && make
```

### Issue: "xcrun: error: unable to find utility 'metal'"

**Solution:**

```bash
xcode-select --install
sudo xcode-select --reset
# Restart Terminal
```

### Issue: "ld: library not found for -lomp"

**Solution:**

```bash
brew install libomp
# Add to ~/.zshrc:
export LDFLAGS="-L/opt/homebrew/opt/libomp/lib"
export CPPFLAGS="-I/opt/homebrew/opt/libomp/include"
# Rebuild
cd build && cmake .. && make
```

### Issue: Build succeeds but "Illegal instruction: 4" when running

**Cause**: Running on Intel Mac (not supported)

**Solution**: Silikangaroo requires Apple Silicon. Check your chip:

```bash
uname -m
# Should output: arm64 (not x86_64)
```

### Issue: GPU shows 0% utilization

**Check:**

1. Did you use `--gpu` flag?
2. Is another app using the GPU? (Close Chrome, games, etc.)
3. Monitor GPU during run:

```bash
sudo powermetrics --samplers gpu_power -i 1000
```

### Issue: Permission denied

**Solution:**

```bash
chmod +x silikangaroo gen_key
```

## Updating

To update to a newer version:

```bash
cd ~/silikangaroo  # Adjust path
git pull origin main
cd build
make clean
cmake .. && make -j$(sysctl -n hw.ncpu)
```

## Uninstalling

To completely remove Silikangaroo:

```bash
cd ~
rm -rf silikangaroo/

# Optional: Remove dependencies if not needed by other apps
brew uninstall libomp gmp cmake
```

## System Compatibility

| macOS Version | Status | Notes |
|---------------|--------|-------|
| Monterey 12.x | ✅ Supported | Tested |
| Ventura 13.x  | ✅ Supported | Tested |
| Sonoma 14.x   | ✅ Supported | Recommended |
| Sequoia 15.x  | ✅ Supported | Latest |
| Big Sur 11.x  | ⚠️ Limited | Metal 2.4 required |

| Chip | Status | Performance |
|------|--------|-------------|
| M1 (8 GPU cores) | ✅ Supported | ~7 M jumps/sec |
| M1 Pro (16 cores) | ✅ Supported | ~14 M jumps/sec |
| M1 Max (32 cores) | ✅ Supported | ~28 M jumps/sec |
| M1 Ultra (64 cores) | ✅ Supported | ~54 M jumps/sec* |
| M2 series | ✅ Supported | Similar to M1 |
| M3 series | ✅ Supported | ~10% faster |
| M4 series | ✅ Supported | ~15% faster |

*Estimated based on linear scaling

## Getting Help

- **Documentation**: See [README.md](README.md) and [docs/FAQ.md](docs/FAQ.md)
- **Issues**: [GitHub Issues](https://github.com/yourusername/silikangaroo/issues)
- **Discussions**: [GitHub Discussions](https://github.com/yourusername/silikangaroo/discussions)

## Next Steps

- Read the [Usage Guide](README.md#usage) for examples
- Check [ALGORITHM.md](docs/ALGORITHM.md) to understand how it works
- Review [BENCHMARKS.md](docs/BENCHMARKS.md) for performance tuning
- Try solving known puzzles from the README

Happy puzzle solving! 🦘

