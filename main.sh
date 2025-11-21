#!/bin/bash
mkdir -p build
cd build
cmake ..
make clean
make -j4

# Run with the user's example
# We expect this to fail or take a long time if the range is wrong.
# We'll set a timeout.
echo "Running Silikangaroo on GPU..."

# 20
# ./silikangaroo 033c4a45cbd643ff97d77f41ea37e843648d50fd894b864b0d52febc62f6454f7c 524288 1048575 8 --gpu
# 20_private_key = 00000000000000000000000000000000000000000000000000000000000d2c55

# 30
# ./silikangaroo 030d282cf2ff536d2c42f105d0b8588821a915dc3f9a05bd98bb23af67a2e92a5b 536870912 1073741823 8 --gpu
# 30_private_key = 000000000000000000000000000000000000000000000000000000003d94cd64

# 40
# Range size: ~5.5e11. Sqrt(N): ~7.4e5.
# Expected ops: ~1.5M.
# GPU Batch 16384 * 100 steps is enough to cover the whole range.
# DP Bits: Auto (or small, e.g. 10). Do NOT use 32.
# ./silikangaroo 03a2efa402fd5268400c77c20e574ba86409ededee7c4020e4b9f0edbee53de0d4 549755813888 1099511627775 --gpu --batch 16384 --steps 256 --checkpoint kangaroo_40.ckpt --resume kangaroo_40.ckpt
# 40_private_key = 000000000000000000000000000000000000000000000000000000e9ae4933d6

# 60
# ./silikangaroo 0348e843dc5b1bd246e6309b4924b81543d02b16c8083df973a89ce2c7eb89a10d 576460752303423488 1152921504606846975 --gpu --batch 16384 --steps 256 --checkpoint kangaroo_60.ckpt --resume kangaroo_60.ckpt
# 60_private_key = 0000000000000000000000000000000000000000000000000fc07a1825367bbe


# 135
# Range size: 2^135. Huge.
# Needs optimized settings: DP=32, Large Batch, Large Steps.
./silikangaroo 02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16 21778071482940061661655974875633165533184 43556142965880123323311949751266331066367 --gpu --dp 32 --batch 16384 --steps 1024 --checkpoint kangaroo_135.ckpt --resume kangaroo_135.ckpt
# 135_private_key = ?

