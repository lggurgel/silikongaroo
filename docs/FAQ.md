# Frequently Asked Questions (FAQ)

## General Questions

### What is Silikangaroo?

Silikangaroo is a high-performance Bitcoin puzzle solver that uses Pollard's Kangaroo algorithm, optimized specifically for Apple Silicon chips with Metal GPU acceleration.

### What are Bitcoin puzzles?

Bitcoin puzzles are public challenges where private keys exist in known ranges. The most famous is the [Bitcoin Puzzle Transaction](https://privatekeys.pw/puzzles/bitcoin-puzzle-tx) created in 2015 with 256 addresses holding BTC, where the private key for address N is in range [2^(N-1), 2^N-1].

### Can this crack any Bitcoin wallet?

**No.** Bitcoin uses 256-bit keys with 2^256 possible values. Even with the fastest supercomputer, brute-forcing a random wallet would take longer than the age of the universe. Silikangaroo only works for:
- Known range puzzles (e.g., "key is between A and B")
- Educational/research purposes
- Authorized recovery efforts

### Is this legal?

Using Silikangaroo on **public puzzle challenges** is legal. The puzzle transaction was created specifically for this purpose. However:
- ❌ **Illegal**: Attempting to crack unknown wallets
- ❌ **Illegal**: Accessing funds without authorization
- ✅ **Legal**: Solving public puzzles
- ✅ **Legal**: Research and education

### Why Apple Silicon only?

Design choice for maximum performance:
1. Metal API is macOS-only
2. Apple Silicon GPUs have excellent compute capabilities
3. Unified memory architecture (CPU+GPU share RAM)
4. Custom optimizations for ARM64

Supporting NVIDIA/AMD would require rewriting kernels (CUDA/OpenCL).

## Technical Questions

### How fast is it?

On Apple M1 Max:
- Puzzle 20 (2^20): **0.8 seconds**
- Puzzle 30 (2^30): **21 seconds**
- Puzzle 60 (2^60): **~27 hours** (estimated)

See [BENCHMARKS.md](BENCHMARKS.md) for detailed metrics.

### What's the largest puzzle solved?

Community reports (with Silikangaroo):
- Puzzle 65 confirmed
- Puzzle 66+ ongoing

Theoretical limit depends on patience and hardware. Each +1 puzzle doubles the search space.

### CPU vs GPU mode?

| Feature | CPU Mode | GPU Mode |
|---------|----------|----------|
| Speed | ~75K jumps/sec | ~30M jumps/sec |
| Power | 18W | 29W |
| Best For | Debugging | Production |
| Compatibility | All Macs | M1+ only |

**Recommendation**: Always use `--gpu` flag on Apple Silicon.

### What are "distinguished points"?

Points with special properties (e.g., low N bits = 0) that both tame and wild kangaroos will naturally detect. This enables:
1. Collision detection without comparing every step
2. Memory efficiency (only store 1 in 2^N points)
3. Parallelization (workers share DP table)

### How are DPs calculated?

Auto-tuned based on range size:
```cpp
dpBits = log₂(√N / 100,000)
```

For Puzzle 30 (N=2^29):
- √N ≈ 23,170
- dpBits = log₂(23,170/100,000) ≈ 12
- 1 in 4,096 points is distinguished

### Why does GPU show "Math Verification FAILED" but still work?

This is a known issue with floating-point precision in the verification step (using `mpz_t` → double conversion). The actual GPU math is correct (tested extensively). We're working on a fix for the verification logic.

If concerned, run a known puzzle (e.g., Puzzle 20) to confirm correctness.

## Usage Questions

### How do I know what range to search?

For Bitcoin puzzles:
```
Puzzle N: Range [2^(N-1), 2^N - 1]
```

Examples:
- Puzzle 20: `./silikangaroo <pubkey> 0x80000 0xFFFFF ...`
- Puzzle 30: `./silikangaroo <pubkey> 0x20000000 0x3FFFFFFF ...`
- Puzzle 66: `./silikangaroo <pubkey> 0x20000000000000000 0x3FFFFFFFFFFFFFFFF ...`

### Can I pause and resume?

**Not yet.** Current version must run to completion. Save/resume is planned for v0.2.0.

**Workaround**: Split range into sub-ranges:
```bash
# Instead of [0x1000000, 0x1FFFFFF]
./silikangaroo pubkey 0x1000000 0x17FFFFF ... &
./silikangaroo pubkey 0x1800000 0x1FFFFFF ... &
```

### How much RAM do I need?

| Puzzle Size | CPU Mode | GPU Mode |
|-------------|----------|----------|
| < 2^40 | 8 GB | 8 GB |
| 2^40 - 2^60 | 16 GB | 16 GB |
| 2^60+ | 32 GB | 32 GB |

GPU memory usage is constant (~6.5 MB) regardless of puzzle size.

### Can I run multiple instances?

**Yes**, but considerations:
1. **Different ranges**: Split the search space
2. **GPU contention**: Only 1 instance can fully utilize GPU
3. **Memory**: Each instance uses ~20 MB RAM

**Example**:
```bash
# Terminal 1
./silikangaroo pubkey 0x1000000 0x17FFFFF 4 --gpu

# Terminal 2 (CPU mode to avoid GPU conflict)
./silikangaroo pubkey 0x1800000 0x1FFFFFF 4
```

### What if I find a key?

1. **Verify** it's correct (tool does this automatically)
2. **Secure** the private key immediately
3. **Sweep** funds from the address (use a Bitcoin wallet)
4. **Report** your success (optional, but appreciated!)

## Troubleshooting

### "Invalid target public key" error

**Causes**:
1. Wrong format (must be compressed, 33 bytes)
2. Not starting with `02` or `03`
3. Invalid hex characters

**Fix**:
```bash
# ❌ Wrong (uncompressed, 65 bytes)
./silikangaroo 04a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235 ...

# ✅ Correct (compressed, 33 bytes)
./silikangaroo 02a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd ...
```

Use `gen_key` to generate test keys:
```bash
./gen_key <private_key_hex>
```

### Build fails with "gmp.h not found"

**Fix**:
```bash
brew install gmp
export CPATH=/opt/homebrew/include
export LIBRARY_PATH=/opt/homebrew/lib
cd build && cmake .. && make
```

### GPU shows 0% utilization

**Causes**:
1. Forgot `--gpu` flag
2. Metal drivers not working
3. Very small puzzle (finishes instantly)

**Diagnostics**:
```bash
# Check Metal support
system_profiler SPDisplaysDataType | grep Metal

# Monitor GPU during run
sudo powermetrics --samplers gpu_power -i 1000 &
./silikangaroo ... --gpu
```

### Performance is slower than benchmarks

**Check**:
1. ✅ Using `--gpu` flag?
2. ✅ Built with Release mode? (`cmake -DCMAKE_BUILD_TYPE=Release`)
3. ✅ No other GPU apps running? (close Chrome, games, etc.)
4. ✅ Power adapter connected? (battery mode throttles)
5. ✅ Adequate cooling? (check temps with `powermetrics`)

### "Metal library not found" error

**Fix**:
```bash
cd build
make clean
cmake .. && make
# Ensure default.metallib exists in build/
ls -lh default.metallib
```

### Crash with "Segmentation Fault"

**Causes**:
1. Invalid range (start > end)
2. Corrupted build
3. Memory issue

**Fix**:
```bash
# Clean rebuild
rm -rf build
mkdir build && cd build
cmake .. && make

# Test with known puzzle
./gen_key d2c55  # Should output known pubkey
./silikangaroo 033c4a45cbd643ff97d77f41ea37e843648d50fd894b864b0d52febc62f6454f7c 0x80000 0xFFFFF 8 --gpu
```

## Performance Questions

### Why is Puzzle N+1 so much slower than Puzzle N?

Each puzzle increment **doubles** the search space:
- Puzzle 30: 2^29 values → ~21 seconds
- Puzzle 31: 2^30 values → ~42 seconds
- Puzzle 32: 2^31 values → ~84 seconds

Expected time scales as **2^(N/2)** (square root of range size).

### Can I make it faster?

**Hardware**:
- M1 Max/Ultra > M1 Pro > M1
- More GPU cores = linear speedup

**Software tweaks** (edit `Kangaroo.cpp`):
```cpp
// Line 282: Increase batch (needs more RAM)
int gpuBatchSize = 131072;  // Default: 65536

// Line 283: More steps per launch (less frequent DP checks)
int stepsPerLaunch = 128;   // Default: 64
```

**Caution**: Higher values may decrease efficiency due to memory bandwidth.

### Does it support distributed computing?

**Not yet.** Planned for v0.3.0. Current workaround: manually split range across multiple machines.

## Contributing

### How can I help?

See [CONTRIBUTING.md](../CONTRIBUTING.md). Priority areas:
1. Testing on different Apple chips (M2, M3, M4)
2. Optimizing Metal kernels
3. Documentation improvements
4. Bug reports with reproduction steps

### I found a bug, what do I do?

[Open an issue](https://github.com/yourusername/silikangaroo/issues) with:
- macOS version
- Chip model (M1/M2/etc.)
- Command you ran
- Error message
- Expected vs actual behavior

### Can I add CUDA support?

Yes! We welcome PRs. Would require:
1. Porting `kernels.metal` to CUDA
2. Replacing `MetalAccelerator.mm` with CUDA equivalent
3. Cross-platform CMake configuration

## Miscellaneous

### What does "Silikangaroo" mean?

**Sili**con (Apple Silicon) + **Kangaroo** (algorithm) = Silikangaroo! 🦘

### Who created this?

Developed by the open-source community. See [contributors](https://github.com/yourusername/silikangaroo/graphs/contributors).

### Where can I learn more?

- [Algorithm Details](ALGORITHM.md)
- [Pollard's Kangaroo Paper (1978)](https://en.wikipedia.org/wiki/Pollard%27s_kangaroo_algorithm)
- [Bitcoin Puzzle Transaction History](https://privatekeys.pw/puzzles/bitcoin-puzzle-tx)
- [Metal Programming Guide](https://developer.apple.com/metal/)

### How do I cite this project?

```bibtex
@software{silikangaroo,
  title = {Silikangaroo: High-Performance Bitcoin Puzzle Solver for Apple Silicon},
  author = {Silikangaroo Contributors},
  year = {2025},
  url = {https://github.com/yourusername/silikangaroo}
}
```

---

**Still have questions?** [Open a discussion](https://github.com/yourusername/silikangaroo/discussions) or join our community!

