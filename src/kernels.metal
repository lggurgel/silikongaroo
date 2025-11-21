#include <metal_stdlib>
using namespace metal;

typedef uint uint32_t;
typedef ulong uint64_t;

// 256-bit integer structure
struct uint256 {
    uint32_t v[8];
};

// Point in Jacobian coordinates
struct Point {
    uint256 x;
    uint256 y;
    uint256 z;
};

constant uint32_t SECP_P[8] = {
    0xFFFFFC2F, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
};

// --- Basic Comparison & Arithmetic ---

bool gte(thread const uint256& a, constant uint32_t* b) {
    for(int i=7; i>=0; i--) {
        if(a.v[i] > b[i]) return true;
        if(a.v[i] < b[i]) return false;
    }
    return true;
}

bool gte_t(thread const uint256& a, thread const uint256& b) {
    for(int i=7; i>=0; i--) {
        if(a.v[i] > b.v[i]) return true;
        if(a.v[i] < b.v[i]) return false;
    }
    return true;
}

void add(thread uint256& c, thread const uint256& a, thread const uint256& b) {
    uint64_t carry = 0;
    for(int i=0; i<8; i++) {
        uint64_t sum = (uint64_t)a.v[i] + b.v[i] + carry;
        c.v[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
}

void sub(thread uint256& c, thread const uint256& a, thread const uint256& b) {
    uint64_t borrow = 0;
    for(int i=0; i<8; i++) {
        uint64_t diff = (uint64_t)a.v[i] - b.v[i] - borrow;
        c.v[i] = (uint32_t)diff;
        borrow = (diff >> 63) & 1;
    }
}

void sub_c(thread uint256& c, thread const uint256& a, constant uint32_t* b) {
    uint64_t borrow = 0;
    for(int i=0; i<8; i++) {
        uint64_t diff = (uint64_t)a.v[i] - b[i] - borrow;
        c.v[i] = (uint32_t)diff;
        borrow = (diff >> 63) & 1;
    }
}

void mod_add(thread uint256& c, thread const uint256& a, thread const uint256& b) {
    uint64_t carry = 0;
    for(int i=0; i<8; i++) {
        uint64_t sum = (uint64_t)a.v[i] + b.v[i] + carry;
        c.v[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    if (carry) {
        // Add 2^32 + 977
        uint64_t c2 = 0;
        uint64_t sum = (uint64_t)c.v[0] + 977 + c2;
        c.v[0] = (uint32_t)sum;
        c2 = sum >> 32;

        sum = (uint64_t)c.v[1] + 1 + c2;
        c.v[1] = (uint32_t)sum;
        c2 = sum >> 32;

        for(int i=2; i<8; i++) {
            sum = (uint64_t)c.v[i] + c2;
            c.v[i] = (uint32_t)sum;
            c2 = sum >> 32;
        }
    }
    if(gte(c, SECP_P)) sub_c(c, c, SECP_P);
}

void mod_sub(thread uint256& c, thread const uint256& a, thread const uint256& b) {
    if(gte_t(a, b)) {
        sub(c, a, b);
    } else {
        sub(c, a, b);
        // c = 2^256 - (b-a). We want P - (b-a) = c - X.
        uint32_t X[8] = {977, 1, 0, 0, 0, 0, 0, 0};
        // Inline sub_c logic for thread memory
        uint64_t borrow = 0;
        for(int i=0; i<8; i++) {
            uint64_t diff = (uint64_t)c.v[i] - X[i] - borrow;
            c.v[i] = (uint32_t)diff;
            borrow = (diff >> 63) & 1;
        }
    }
}

// --- Multiplication ---

void mul256_safe(thread uint32_t* r, thread const uint256& a, thread const uint256& b) {
    for(int i=0; i<16; i++) r[i] = 0;

    for(int i=0; i<8; i++) {
        uint64_t carry = 0;
        for(int j=0; j<8; j++) {
            uint64_t val = (uint64_t)a.v[i] * b.v[j] + r[i+j] + carry;
            r[i+j] = (uint32_t)val;
            carry = val >> 32;
        }
        // Propagate carry
        int k = i + 8;
        while (carry > 0 && k < 16) {
            uint64_t val = (uint64_t)r[k] + carry;
            r[k] = (uint32_t)val;
            carry = val >> 32;
            k++;
        }
    }
}

void reduce512(thread uint256& c, thread uint32_t* r) {
    // Initial low part
    uint256 low;
    for(int i=0; i<8; i++) low.v[i] = r[i];

    // High part
    uint256 high;
    for(int i=0; i<8; i++) high.v[i] = r[8+i];

    // We want low + (high << 32) + (high * 977)

    // 1. high * 977
    uint256 h977;
    uint64_t carry = 0;
    for(int i=0; i<8; i++) {
        uint64_t val = (uint64_t)high.v[i] * 977 + carry;
        h977.v[i] = (uint32_t)val;
        carry = val >> 32;
    }
    uint32_t overflow1 = (uint32_t)carry;

    // 2. high << 32
    uint32_t overflow2 = high.v[7];
    uint256 hShift;
    hShift.v[0] = 0;
    for(int i=1; i<8; i++) hShift.v[i] = high.v[i-1];

    // 3. Add everything to low
    uint64_t sum_carry = 0;
    for(int i=0; i<8; i++) {
        uint64_t sum = (uint64_t)low.v[i] + h977.v[i] + hShift.v[i] + sum_carry;
        low.v[i] = (uint32_t)sum;
        sum_carry = sum >> 32;
    }

    uint64_t total_overflow = overflow1 + (uint64_t)overflow2 + sum_carry;

    // Reduce total_overflow
    int loopLimit = 0;
    while (total_overflow > 0 && loopLimit < 20) {
        loopLimit++;
        uint64_t ov = total_overflow;
        total_overflow = 0;

        // 1. Add ov * 977
        uint64_t p = ov * 977;
        uint64_t c = 0;

        uint64_t sum = (uint64_t)low.v[0] + (p & 0xFFFFFFFF) + c;
        low.v[0] = (uint32_t)sum;
        c = sum >> 32;

        sum = (uint64_t)low.v[1] + (p >> 32) + c;
        low.v[1] = (uint32_t)sum;
        c = sum >> 32;

        for(int i=2; i<8; i++) {
             sum = (uint64_t)low.v[i] + c;
             low.v[i] = (uint32_t)sum;
             c = sum >> 32;
        }
        total_overflow += c;

        // 2. Add ov << 32
        c = 0;
        sum = (uint64_t)low.v[1] + (ov & 0xFFFFFFFF) + c;
        low.v[1] = (uint32_t)sum;
        c = sum >> 32;

        sum = (uint64_t)low.v[2] + (ov >> 32) + c;
        low.v[2] = (uint32_t)sum;
        c = sum >> 32;

        for(int i=3; i<8; i++) {
             sum = (uint64_t)low.v[i] + c;
             low.v[i] = (uint32_t)sum;
             c = sum >> 32;
        }
        total_overflow += c;
    }

    c = low;
    int limit = 0;
    while(gte(c, SECP_P) && limit < 4) {
        sub_c(c, c, SECP_P);
        limit++;
    }
}

void mod_mul(thread uint256& c, thread const uint256& a, thread const uint256& b) {
    uint32_t wide[16];
    mul256_safe(wide, a, b);
    reduce512(c, wide);
}

void mod_sqr(thread uint256& c, thread const uint256& a) {
    mod_mul(c, a, a);
}

// Single thread inverse (slow)
void mod_inv_single(thread uint256& r, thread const uint256& a) {
    uint256 e; for(int i=0; i<8; i++) e.v[i] = SECP_P[i];
    e.v[0] -= 2;
    for(int i=0; i<8; i++) r.v[i] = (i==0 ? 1 : 0);
    uint256 base = a;
    for(int i=0; i<8; i++) {
        uint32_t w = e.v[i];
        for(int j=0; j<32; j++) {
            if((w >> j) & 1) mod_mul(r, r, base);
            mod_sqr(base, base);
        }
    }
}

// --- Batched Inverse Helpers ---

uint256 shuffle_up_256(thread const uint256& v, ushort delta) {
    uint256 r;
    for(int i=0; i<8; i++) r.v[i] = simd_shuffle_up(v.v[i], delta);
    return r;
}

uint256 shuffle_down_256(thread const uint256& v, ushort delta) {
    uint256 r;
    for(int i=0; i<8; i++) r.v[i] = simd_shuffle_down(v.v[i], delta);
    return r;
}

uint256 broadcast_256(thread const uint256& v, ushort idx) {
    uint256 r;
    for(int i=0; i<8; i++) r.v[i] = simd_broadcast(v.v[i], idx);
    return r;
}

// Batched Inverse (SIMD Group of 32 threads)
// Computes r = a^-1 mod P for all 32 threads
void mod_inv_batched(thread uint256& r, thread const uint256& a, ushort lane) {
    // Montgomery's Simultaneous Inversion
    // 1. Parallel Prefix Product: L[i] = a[0] * ... * a[i]
    uint256 L = a;

    // No need to call simd_lane_id() here, passed as argument

    uint256 t = shuffle_up_256(L, 1);
    if (lane >= 1) mod_mul(L, L, t);

    t = shuffle_up_256(L, 2);
    if (lane >= 2) mod_mul(L, L, t);

    t = shuffle_up_256(L, 4);
    if (lane >= 4) mod_mul(L, L, t);

    t = shuffle_up_256(L, 8);
    if (lane >= 8) mod_mul(L, L, t);

    t = shuffle_up_256(L, 16);
    if (lane >= 16) mod_mul(L, L, t);

    // 2. Parallel Suffix Product: R[i] = a[i] * ... * a[31]
    // We can compute this by shuffling down
    uint256 R = a;

    t = shuffle_down_256(R, 1);
    if (lane < 31) mod_mul(R, R, t);

    t = shuffle_down_256(R, 2);
    if (lane < 30) mod_mul(R, R, t); // 32-2=30

    t = shuffle_down_256(R, 4);
    if (lane < 28) mod_mul(R, R, t);

    t = shuffle_down_256(R, 8);
    if (lane < 24) mod_mul(R, R, t);

    t = shuffle_down_256(R, 16);
    if (lane < 16) mod_mul(R, R, t);

    // 3. Invert Total Product (L[31])
    uint256 total = broadcast_256(L, 31);
    uint256 totalInv;

    // Only one thread needs to compute, but all need result.
    // Divergence is okay here since others wait (or mask).
    // To be safe, let last thread compute.
    if (lane == 31) {
        mod_inv_single(totalInv, total);
    }
    totalInv = broadcast_256(totalInv, 31);

    // 4. Calculate Individual Inverse
    // inv(a[i]) = L[i-1] * R[i+1] * totalInv
    // Handle boundaries

    uint256 acc = totalInv;

    if (lane > 0) {
        uint256 l_prev = shuffle_up_256(L, 1); // Gets L[lane-1]
        mod_mul(acc, acc, l_prev);
    }

    if (lane < 31) {
        uint256 r_next = shuffle_down_256(R, 1); // Gets R[lane+1]
        mod_mul(acc, acc, r_next);
    }

    r = acc;
}

// --- Point Arithmetic ---

void point_add_mixed(thread Point& r, thread const Point& p, thread const uint256& qx, thread const uint256& qy) {
    bool p_inf = true;
    for(int i=0; i<8; i++) if(p.z.v[i]!=0) p_inf = false;
    if(p_inf) {
        r.x = qx; r.y = qy; for(int i=0; i<8; i++) r.z.v[i] = (i==0?1:0);
        return;
    }
    uint256 z1z1; mod_sqr(z1z1, p.z);
    uint256 u2; mod_mul(u2, qx, z1z1);
    uint256 s2; mod_mul(s2, p.z, z1z1); mod_mul(s2, s2, qy);
    uint256 h; mod_sub(h, u2, p.x);
    uint256 hh; mod_sqr(hh, h);
    uint256 i; mod_add(i, hh, hh); mod_add(i, i, i);
    uint256 j; mod_mul(j, h, i);
    uint256 rr; mod_sub(rr, s2, p.y); mod_add(rr, rr, rr);
    uint256 v; mod_mul(v, p.x, i);
    uint256 x3; mod_sqr(x3, rr); mod_sub(x3, x3, j);
    uint256 v2; mod_add(v2, v, v); mod_sub(x3, x3, v2);
    r.x = x3;
    uint256 y3; mod_sub(y3, v, x3); mod_mul(y3, rr, y3);
    uint256 t; mod_mul(t, p.y, j); mod_add(t, t, t); mod_sub(y3, y3, t);
    r.y = y3;
    uint256 z3; mod_add(z3, p.z, h); mod_sqr(z3, z3); mod_sub(z3, z3, z1z1); mod_sub(z3, z3, hh);
    r.z = z3;
}

constant uint32_t SECP_N[8] = {
    0xD0364141, 0xBFD25E8C, 0xAF48A03B, 0xBAAEDCE6,
    0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
};

void scalar_add(thread uint256& c, thread const uint256& a, thread const uint256& b) {
    uint64_t carry = 0;
    for(int i=0; i<8; i++) {
        uint64_t sum = (uint64_t)a.v[i] + b.v[i] + carry;
        c.v[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    // If overflow or >= N, subtract N
    // Note: N is very close to 2^256, so carry might happen.
    // SECP_N is less than 2^256.
    // If carry is set, we definitely need to subtract N.
    // If no carry, we still might be >= N.

    bool ge = false;
    if (carry) {
        ge = true;
    } else {
        ge = gte(c, SECP_N);
    }

    if (ge) {
        // c = c - N
        // If carry was set, c is actually (2^256 + real_c).
        // We want (2^256 + real_c) - N.
        // This is equivalent to: real_c + (2^256 - N).
        // 2^256 - N = 0x14551231950B75FC4402DA1732FC9BEBF
        // Let's just do sub_c with N.
        // If carry was 1, the sub_c will produce a borrow that "cancels" the 2^256.
        // Wait, standard sub_c assumes inputs are 256-bit.
        // If we had a carry bit, we are effectively 257-bit.

        // Simpler approach:
        // 2^256 - N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
        // Wait, N = FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
        // 2^256 - N = 000000000000000000000000000000014551231950B75FC4402DA1732FC9BEBF

        // 2^256 - N = 000000000000000000000000000000014551231950B75FC4402DA1732FC9BEBF

        // Actually let's just use sub_c logic but handle the implicit high bit if carry=1.

        uint64_t borrow = 0;
        for(int i=0; i<8; i++) {
            uint64_t diff = (uint64_t)c.v[i] - SECP_N[i] - borrow;
            c.v[i] = (uint32_t)diff;
            borrow = (diff >> 63) & 1;
        }
        // If carry was 1, then (2^256 + c_old) - N.
        // The loop computed c_old - N.
        // If c_old < N, borrow is 1. Result is 2^256 + c_old - N (which fits in 256).
        // If c_old >= N, borrow is 0. Result is c_old - N.
        // If carry was 1, we effectively have 2^256.
        // If borrow is 1, it means we borrowed from 2^256. 2^256 - 2^256 = 0. Correct.
        // If borrow is 0, it means we didn't borrow. We still have that 2^256?
        // No, if carry=1, c_old is the lower 256 bits.
        // We want (1<<256 | c_old) - N.
        // We computed (c_old - N).
        // If c_old < N, we got (c_old - N) + 2^256 (borrow=1). This is exactly what we want.
        // If c_old >= N, we got (c_old - N) (borrow=0). But we had a carry!
        // So we should have result + 2^256. But result + 2^256 > 2^256 (overflow).
        // But wait, max value is (N-1) + (N-1) = 2N - 2.
        // 2N < 2^257.
        // So result fits in 256 bits.
        // If carry=1, then a+b >= 2^256 > N. So we MUST subtract N.
        // So yes, just subtract N.
        // If borrow occurs, it cancels the carry.
        // If borrow doesn't occur (impossible if carry=1 because c_old would need to be >= N, and a,b < N... wait)
        // Max a+b = 2N-2.
        // 2N = 2 * (2^256 - epsilon) = 2^257 - 2epsilon.
        // So a+b can have bit 256 set.
        // If bit 256 is set (carry=1), then value is 2^256 + c_low.
        // We want 2^256 + c_low - N.
        // We computed c_low - N.
        // If c_low < N, we get 2^256 + c_low - N (borrow=1). Correct.
        // If c_low >= N, we get c_low - N (borrow=0).
        // But we need 2^256 + c_low - N.
        // Is it possible that c_low >= N when carry=1?
        // a < N, b < N. a+b < 2N.
        // If a+b >= 2^256 (carry=1), then a+b = 2^256 + c_low.
        // Since a+b < 2N, then 2^256 + c_low < 2N.
        // c_low < 2N - 2^256.
        // N approx 2^256. 2N approx 2^257.
        // 2N - 2^256 = N + (N - 2^256).
        // Since N < 2^256, N - 2^256 is negative.
        // So c_low < N.
        // So c_low is ALWAYS < N if carry=1.
        // So borrow will ALWAYS be 1 if carry=1.
        // So the logic holds.
    }
}

kernel void kangaroo_step(
    device uint256* pointsX [[ buffer(0) ]],
    device uint256* pointsY [[ buffer(1) ]],
    device uint256* distances [[ buffer(2) ]],
    constant uint& numSteps [[ buffer(3) ]],
    device uint256* jumpTableX [[ buffer(4) ]],
    device uint256* jumpTableY [[ buffer(5) ]],
    device uint256* jumpTableDist [[ buffer(6) ]],
    constant uint& tableSize [[ buffer(7) ]],
    device uint256* foundX [[ buffer(8) ]],
    device uint256* foundY [[ buffer(9) ]],
    device uint256* foundDist [[ buffer(10) ]],
    device uint* foundIds [[ buffer(11) ]],
    device atomic_uint* foundCount [[ buffer(12) ]],
    constant uint& dpBits [[ buffer(13) ]],
    constant uint& maxFound [[ buffer(14) ]],
    uint id [[ thread_position_in_grid ]],
    uint tid [[ thread_index_in_threadgroup ]],
    ushort lid [[ thread_index_in_simdgroup ]]
) {
    // Shared memory for jump table to reduce global memory bandwidth
    // Assumes tableSize <= 32. Metal threadgroup memory is fast.
    threadgroup uint256 sharedTableX[32];
    threadgroup uint256 sharedTableY[32];
    threadgroup uint256 sharedTableDist[32];

    // Cooperative load of jump table
    if (tid < 32 && tid < tableSize) {
        sharedTableX[tid] = jumpTableX[tid];
        sharedTableY[tid] = jumpTableY[tid];
        sharedTableDist[tid] = jumpTableDist[tid];
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    Point p;
    p.x = pointsX[id];
    p.y = pointsY[id];
    for(int i=0; i<8; i++) p.z.v[i] = (i==0?1:0);
    uint256 dist = distances[id];

    uint32_t dpMask = (1 << dpBits) - 1;

    for(uint i=0; i<numSteps; ++i) {
        // Compute Affine X to determine next jump index
        // X_aff = X * Z^-2
        // We need Z^-1. Use Batched Inversion.

        uint256 invZ;
        mod_inv_batched(invZ, p.z, lid); // Pass lane ID

        uint256 invZ2; mod_sqr(invZ2, invZ);
        uint256 x_aff; mod_mul(x_aff, p.x, invZ2);

        // Check DP on Affine X
        // Only check if we haven't found too many (to avoid buffer overflow)
        // We check the lowest bits of x_aff.v[0]
        if ((x_aff.v[0] & dpMask) == 0) {
            // Found DP!
            uint idx = atomic_fetch_add_explicit(foundCount, 1, memory_order_relaxed);
            if (idx < maxFound) {
                // Compute Affine Y for storage
                // Y_aff = Y * Z^-3
                uint256 invZ3; mod_mul(invZ3, invZ2, invZ);
                uint256 y_aff; mod_mul(y_aff, p.y, invZ3);

                foundX[idx] = x_aff;
                foundY[idx] = y_aff;
                foundDist[idx] = dist;
                foundIds[idx] = id;
            }
        }

        uint32_t idx = x_aff.v[0] % tableSize;

        // Use shared memory
        uint256 jx = sharedTableX[idx];
        uint256 jy = sharedTableY[idx];
        uint256 jd = sharedTableDist[idx];

        scalar_add(dist, dist, jd);
        point_add_mixed(p, p, jx, jy);
    }

    // Final normalization to Affine for output
    // We can batch this too!
    uint256 invZ;
    mod_inv_batched(invZ, p.z, lid);

    uint256 invZ2; mod_sqr(invZ2, invZ);
    uint256 invZ3; mod_mul(invZ3, invZ2, invZ);
    uint256 x_out; mod_mul(x_out, p.x, invZ2);
    uint256 y_out; mod_mul(y_out, p.y, invZ3);

    pointsX[id] = x_out;
    pointsY[id] = y_out;
    distances[id] = dist;
}

kernel void check_dp(
    device uint256* pointsX [[ buffer(0) ]],
    device uint* foundIndices [[ buffer(1) ]],
    device atomic_uint* foundCount [[ buffer(2) ]],
    constant uint& dpBits [[ buffer(3) ]],
    uint id [[ thread_position_in_grid ]]
) {
    uint32_t val = pointsX[id].v[0];
    uint32_t mask = (1 << dpBits) - 1;
    if((val & mask) == 0) {
        uint idx = atomic_fetch_add_explicit(foundCount, 1, memory_order_relaxed);
        foundIndices[idx] = id;
    }
}

kernel void test_math(
    device uint256* inputA [[ buffer(0) ]],
    device uint256* inputB [[ buffer(1) ]],
    device uint256* output [[ buffer(2) ]],
    constant uint& op [[ buffer(3) ]],
    uint id [[ thread_position_in_grid ]]
) {
    if (id > 0) return;
    uint256 a = inputA[0];
    uint256 b = inputB[0];
    uint256 r;
    if (op == 0) { mod_add(r, a, b); output[0] = r; }
    else if (op == 1) { mod_mul(r, a, b); output[0] = r; }
    else if (op == 2) { mod_inv_single(r, a); output[0] = r; }
    else if (op == 3) { scalar_add(r, a, b); output[0] = r; }
}
