# Algorithm Details

## Pollard's Kangaroo (Lambda) Algorithm

### Overview

The kangaroo algorithm solves the **Discrete Logarithm Problem** (DLP) in a known interval. Given:
- A generator point `G` on an elliptic curve
- A target point `Q = kG` where `k` is unknown
- A known range `[a, b]` where `k` exists

We want to find `k`.

### The Metaphor

Imagine two kangaroos hopping on a number line:

1. **Tame Kangaroo**: Starts at a known position (end of range), knows its position
2. **Wild Kangaroo**: Starts at the unknown target, doesn't know its absolute position

Both hop according to the same rules (deterministic jumps based on current position). Eventually, they'll land on the same point - **collision**!

### Algorithm Steps

#### 1. Initialization

```
Range: [a, b]
N = b - a  (range size)
Average jump size: ≈ √N

Create jump table: {(d₁, P₁), (d₂, P₂), ..., (dₙ, Pₙ)}
  where dᵢ = random distance, Pᵢ = dᵢ·G
```

#### 2. Tame Kangaroo

```python
position = b + random_offset  # Start ahead
point = position · G
distance_traveled = 0

while not found:
    jump_index = hash(point) mod table_size
    point += jump_table[jump_index].point
    distance_traveled += jump_table[jump_index].distance

    if is_distinguished_point(point):
        store(point, distance_traveled, type=TAME)
```

#### 3. Wild Kangaroo

```python
position = Q  # Target (unknown absolute position)
distance_traveled = 0

while not found:
    jump_index = hash(position) mod table_size
    position += jump_table[jump_index].point
    distance_traveled += jump_table[jump_index].distance

    if is_distinguished_point(position):
        store(position, distance_traveled, type=WILD)
```

#### 4. Collision Detection

When a distinguished point is found:

```python
if point in distinguished_points:
    other = distinguished_points[point]

    if other.type != current.type:  # Tame meets Wild
        # Tame: T₀ + d_tame = collision point
        # Wild: Q + d_wild = collision point
        # Therefore: T₀ + d_tame = Q + d_wild
        # Q = kG, so: k = T₀ + d_tame - d_wild

        k = tame_distance - wild_distance
        verify k is in range [a, b]
        return k
```

### Distinguished Points

A **distinguished point** is one with special properties (e.g., low N bits are zero). This allows:

1. **Memory efficiency**: Only store 1 in 2^N points
2. **Collision detection**: Both kangaroos will naturally stop at same DP
3. **Parallelization**: Multiple workers can share DP storage

**Trade-off**: Too few DPs = slow collision, too many DPs = memory explosion

### Expected Complexity

- **Time**: O(√N) point operations
- **Space**: O(√N / DP_ratio) stored points

For a 2^60 range:
- Expected operations: ~2^30 (1 billion)
- With DP bits = 20: ~2^10 (1,024) DPs stored
- On M1 Max @ 30M jumps/sec: ~35 seconds

## Metal GPU Implementation

### Parallelization Strategy

Instead of 2 kangaroos (1 tame, 1 wild), we run **65,536 kangaroos** in parallel:
- 32,768 tame
- 32,768 wild

Each kangaroo is independent, increasing collision probability.

### SIMD Optimization

Apple GPUs execute 32 threads (SIMD group) in lockstep. Key optimizations:

#### Batched Modular Inversion

Instead of inverting `Z` coordinate individually (expensive):

```metal
// Montgomery's Trick: Invert 32 values simultaneously
// Cost: 1 inversion + 6 log₂(32) = 31 multiplications per 32 elements
void mod_inv_batched(thread uint256& r, thread const uint256& a, ushort lane) {
    // 1. Parallel prefix product
    uint256 L = a;
    for (int d = 1; d < 32; d *= 2) {
        uint256 t = shuffle_up(L, d);
        if (lane >= d) L = L * t;
    }

    // 2. Invert total product (only lane 31)
    if (lane == 31) total_inv = mod_inv_single(L);
    total_inv = broadcast(total_inv, 31);

    // 3. Calculate individual inverses
    r = total_inv;
    if (lane > 0) r *= shuffle_up(L, 1);
    // ... (see kernels.metal for full implementation)
}
```

**Speedup**: 10-15x compared to individual inversions

#### Threadgroup Memory

Jump table (32 entries × 64 bytes) is loaded into shared memory:

```metal
threadgroup uint256 sharedTableX[32];
threadgroup uint256 sharedTableY[32];

// Load once per threadgroup (512 threads)
if (tid < 32) {
    sharedTableX[tid] = jumpTableX[tid];
    sharedTableY[tid] = jumpTableY[tid];
}
threadgroup_barrier();

// Fast access for all threads
uint256 jump = sharedTableX[index];
```

**Benefit**: ~5x faster than global memory access

### Point Representation

We use **Jacobian coordinates** to avoid expensive inversions during addition:

- **Affine**: (x, y) - requires inversion for addition
- **Jacobian**: (X, Y, Z) where x = X/Z², y = Y/Z³

Point addition in Jacobian is ~10x faster. Only convert to affine when needed (checking DP condition).

### Memory Layout

```
GPU Buffers:
┌─────────────┬──────────────────────────┐
│ pointsX     │ [65536 × 32 bytes] 2MB   │
├─────────────┼──────────────────────────┤
│ pointsY     │ [65536 × 32 bytes] 2MB   │
├─────────────┼──────────────────────────┤
│ distances   │ [65536 × 32 bytes] 2MB   │
├─────────────┼──────────────────────────┤
│ jumpTable   │ [32 × 64 bytes] 2KB      │
├─────────────┼──────────────────────────┤
│ foundDPs    │ [4096 × 96 bytes] 384KB  │
└─────────────┴──────────────────────────┘
Total: ~6.5 MB per launch
```

### Kernel Execution

```metal
kernel void kangaroo_step(
    device uint256* pointsX [[buffer(0)]],
    device uint256* pointsY [[buffer(1)]],
    device uint256* distances [[buffer(2)]],
    ...
    uint id [[thread_position_in_grid]],
    ushort lid [[thread_index_in_simdgroup]]
) {
    // Each thread = 1 kangaroo
    Point p = {pointsX[id], pointsY[id], Z=1};
    uint256 dist = distances[id];

    for (step = 0; step < 64; step++) {
        // Convert to affine (batched inverse)
        uint256 invZ = mod_inv_batched(p.z, lid);
        uint256 x_aff = p.x * invZ²;

        // Check DP
        if ((x_aff & dpMask) == 0) {
            store_distinguished_point(x_aff, dist, id);
        }

        // Jump
        int idx = x_aff % 32;
        p += jumpTable[idx];
        dist += jumpDist[idx];
    }

    // Write back
    pointsX[id] = p.x;
    pointsY[id] = p.y;
    distances[id] = dist;
}
```

## Challenges & Solutions

### Challenge 1: Modular Arithmetic

**Problem**: No native 256-bit support in Metal

**Solution**: Implement `uint256` struct with carry/borrow propagation
```metal
struct uint256 {
    uint32_t v[8];  // Little-endian
};
```

### Challenge 2: secp256k1 Prime Reduction

**Problem**: Efficient reduction modulo P = 2^256 - 2^32 - 977

**Solution**: Use special form to avoid full division
```
P = 2^256 - X, where X = 2^32 + 977
If a = 2^256·q + r, then a mod P = r + q·X mod P
```

### Challenge 3: Avoiding Divergence

**Problem**: Different threads finding DPs at different steps

**Solution**: Check DP at every step (predication), continue all threads
```metal
if (is_dp) store_dp();  // Only some threads execute, others masked
// All threads continue to next jump (no divergence)
```

### Challenge 4: Atomic Operations

**Problem**: Multiple threads finding DPs simultaneously

**Solution**: Atomic counter for DP buffer
```metal
uint idx = atomic_fetch_add_explicit(foundCount, 1, memory_order_relaxed);
if (idx < maxFound) foundDPs[idx] = ...;
```

## Future Optimizations

1. **Precomputed Multiples**: Store 2P, 4P, 8P for faster scalar multiplication
2. **Adaptive Jump Table**: Adjust sizes based on collision rate
3. **Multi-GPU**: Split range across multiple GPUs (M1 Ultra)
4. **Checkpointing**: Save state every N jumps for resume capability
5. **Distinguished Point Chains**: Allow kangaroos to continue past DP

## References

1. Pollard, J.M. (1978). "Monte Carlo methods for index computation (mod p)"
2. "Kangaroo: A Fast Algorithm for Solving the Discrete Logarithm Problem on Elliptic Curves" - Bernstein et al.
3. Joppe W. Bos, "Montgomery's Trick for Batch Inversion"
4. Metal Shading Language Specification v2.4

