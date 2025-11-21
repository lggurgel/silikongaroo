#include "Kangaroo.hpp"

#include <omp.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>

#include "Utils.hpp"

Kangaroo::Kangaroo(const mpz_class& startRange, const mpz_class& endRange,
                   const std::string& targetPubKeyHex, int numThreads)
    : startRange(startRange),
      endRange(endRange),
      targetHex(targetPubKeyHex),
      numThreads(numThreads) {
  rangeSize = endRange - startRange;
  if (this->numThreads <= 0) {
    this->numThreads = std::thread::hardware_concurrency();
    if (this->numThreads == 0)
      this->numThreads = 4;
  }

  std::vector<unsigned char> pubBytes = Utils::hexToBytes(targetHex);
  if (!ecc.parsePublicKey(targetPubKey, pubBytes)) {
    throw std::runtime_error("Invalid target public key");
  }

  // Calculate dpBits
  mpz_class sqrtN;
  mpz_sqrt(sqrtN.get_mpz_t(), rangeSize.get_mpz_t());
  double sqrtNd = mpz_get_d(sqrtN.get_mpz_t());
  double targetDPs = 100000.0;
  double avgSteps = sqrtNd / targetDPs;
  if (avgSteps < 1.0)
    avgSteps = 1.0;
  dpBits = (int)std::log2(avgSteps);
  if (dpBits < 1)
    dpBits = 1;
  if (dpBits > 24)
    dpBits = 24;  // Cap it

  std::cout << "Range size: " << rangeSize.get_str() << std::endl;
  std::cout << "Sqrt(N): " << sqrtNd << std::endl;
  std::cout << "DP Bits: " << dpBits << " (1 in " << (1 << dpBits) << ")"
            << std::endl;

  initJumpTable();
}

Kangaroo::~Kangaroo() {
  shouldStop = true;
}

void Kangaroo::initJumpTable() {
  mpz_class sqrtN;
  mpz_sqrt(sqrtN.get_mpz_t(), rangeSize.get_mpz_t());

  int tableSize = 32;  // Power of 2
  jumpTable.resize(tableSize);

  gmp_randclass rr(gmp_randinit_default);
  rr.seed(time(NULL));

  mpz_class mean = sqrtN / 2;
  if (mean == 0)
    mean = 1;

  for (int i = 0; i < tableSize; ++i) {
    mpz_class jumpDist = rr.get_z_range(mean) + mean / 2 + 1;
    if (jumpDist >= rangeSize)
      jumpDist = rangeSize / 2 + 1;

    jumpTable[i].dist = jumpDist;

    unsigned char scalar[32];
    Utils::mpzToBytes(jumpDist.get_mpz_t(), scalar);

    if (!ecc.getPubKeyFromPriv(jumpTable[i].point, scalar)) {
      throw std::runtime_error("Failed to generate jump point");
    }
  }
}

bool Kangaroo::isDistinguished(const secp256k1_pubkey& point) {
  std::vector<unsigned char> bytes = ecc.serializePublicKey(point, true);

  int bitsToCheck = dpBits;
  int byteIdx = bytes.size() - 1;

  while (bitsToCheck >= 8) {
    if (bytes[byteIdx] != 0)
      return false;
    bitsToCheck -= 8;
    byteIdx--;
  }
  if (bitsToCheck > 0) {
    unsigned char mask = (1 << bitsToCheck) - 1;
    if ((bytes[byteIdx] & mask) != 0)
      return false;
  }
  return true;
}

double Kangaroo::getDuration() const {
  auto now = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = now - startTime;
  return elapsed.count();
}

double Kangaroo::getOpsPerSecond() const {
  double duration = getDuration();
  if (duration <= 0)
    return 0;
  return (double)totalJumps / duration;
}

double Kangaroo::getEstimatedSecondsRemaining() const {
  double rate = getOpsPerSecond();
  if (rate <= 0)
    return -1.0;  // Unknown

  // Expected steps = sqrt(range)
  // But this is for ONE kangaroo.
  // For N kangaroos, the total work is still roughly sqrt(range) * constant?
  // No, Pollard's Lambda expected complexity is ~ 2 * sqrt(N) operations total
  // across all processors. Let's use the standard approximation: 2 *
  // sqrt(rangeSize)

  mpz_class sqrtN;
  mpz_sqrt(sqrtN.get_mpz_t(), rangeSize.get_mpz_t());
  double expectedTotalOps = mpz_get_d(sqrtN.get_mpz_t()) * 2.0;

  // Remaining ops
  double remainingOps = expectedTotalOps - (double)totalJumps;
  if (remainingOps < 0)
    remainingOps = 0;

  return remainingOps / rate;
}

void Kangaroo::processCollision(const std::string& pointHex,
                                const mpz_class& dist, bool isTame) {
  if (distinguishedPoints.find(pointHex) == distinguishedPoints.end()) {
    distinguishedPoints[pointHex] = {dist, isTame};
    return;
  }

  DistinguishedPoint& other = distinguishedPoints[pointHex];
  if (other.isTame == isTame) {
    return;
  }

  // Collision between Tame and Wild!
  mpz_class distTame = isTame ? dist : other.distance;
  mpz_class distWild = isTame ? other.distance : dist;

  mpz_class candidate = distTame - distWild;

  mpz_class N;
  N.set_str("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141",
            16);
  candidate = candidate % N;
  if (candidate < 0)
    candidate += N;

  secp256k1_pubkey checkPub;
  unsigned char privBytes[32];

  Utils::mpzToBytes(candidate.get_mpz_t(), privBytes);
  if (ecc.getPubKeyFromPriv(checkPub, privBytes)) {
    std::vector<unsigned char> checkHex =
        ecc.serializePublicKey(checkPub, true);
    std::vector<unsigned char> targetSer =
        ecc.serializePublicKey(targetPubKey, true);

    if (checkHex == targetSer) {
      found = true;
      privateKey = candidate;
      shouldStop = true;
    }
  }
}

void Kangaroo::run() {
  startTime = std::chrono::high_resolution_clock::now();

  int tameCount = numThreads / 2;
  if (tameCount < 1)
    tameCount = 1;
  int wildCount = numThreads - tameCount;

  std::cout << "Starting " << tameCount << " Tame and " << wildCount
            << " Wild kangaroos." << std::endl;

  // Metal GPU Check
  if (useGPU) {
    // Dynamic Tuning for GPU

    // 1. Check if we can increase dpBits for performance
    // If range is large, we WANT high dpBits to reduce memory contention and
    // allow more steps per launch. But we must not make dpBits so high that we
    // never find the key (Pollard's Lambda constraint). Rule of thumb: expected
    // total jumps ~ 2 * sqrt(range). We need to find enough DPs to detect
    // collision. If we have 2^16 jumps between DPs, and total jumps is 2^30, we
    // find 2^14 DPs. Plenty. If total jumps is 2^10 (small range), and jumps
    // between DPs is 2^16, we find 0 DPs. Bad.

    mpz_class sqrtN;
    mpz_sqrt(sqrtN.get_mpz_t(), rangeSize.get_mpz_t());
    double expectedOps = mpz_get_d(sqrtN.get_mpz_t()) * 2.0;

    // If we can afford it, boost dpBits to 16 for GPU efficiency
    if (expectedOps > (double)(1ULL << 20)) {  // If we expect > 1M ops
      if (dpBits < 16) {
        dpBits = 16;
        std::cout << "Boosting dpBits to 16 for GPU efficiency (Large Range)."
                  << std::endl;
      }
    }

    // 2. Safety Check: Ensure (batchSize * steps * probability) < bufferSize
    // Buffer size is 4096. Let's target 2048 max DPs per launch for safety.

    double prob = 1.0 / (double)(1ULL << dpBits);
    double maxTotalSteps = 2048.0 / prob;

    // Default High Performance settings
    int gpuBatchSize = 1024;
    int stepsPerLaunch = 64;

    // If dpBits is small (small range), maxTotalSteps will be small.
    // e.g. dpBits=1 -> prob=0.5 -> maxTotalSteps = 4096.
    // We can't run 65536 * 1024 steps!

    if ((double)gpuBatchSize * stepsPerLaunch > maxTotalSteps) {
      // We need to reduce batch or steps.
      // Prefer reducing steps first, then batch.

      // Try reducing steps
      stepsPerLaunch = (int)(maxTotalSteps / gpuBatchSize);
      if (stepsPerLaunch < 1) {
        stepsPerLaunch = 1;
        // Still too big? Reduce batch.
        gpuBatchSize = (int)maxTotalSteps;
        if (gpuBatchSize < 32)
          gpuBatchSize = 32;  // Min SIMD width
      }

      std::cout << "Tuning GPU parameters for small range/low dpBits:"
                << std::endl;
      std::cout << "  Batch Size: " << gpuBatchSize << std::endl;
      std::cout << "  Steps: " << stepsPerLaunch << std::endl;
    }

    std::cout << "GPU Parameters:" << std::endl;
    std::cout << "  Batch Size: " << gpuBatchSize << std::endl;
    std::cout << "  Steps: " << stepsPerLaunch << std::endl;
    std::cout << "  DP Bits: " << dpBits << std::endl;

    std::cout << "Initializing Metal Accelerator..." << std::endl;
    metalAccel.init(jumpTable);

    // ... Verification code ...

    // --- Verification Step ---
    std::cout << "Verifying GPU Math Integrity..." << std::endl;
    // 1. Create a known point P
    secp256k1_pubkey p_cpu;
    unsigned char scalarOne[32] = {0};
    scalarOne[31] = 1;  // 1
    ecc.getPubKeyFromPriv(p_cpu, scalarOne);

    // 2. Calculate 1 step on CPU
    std::vector<unsigned char> ser = ecc.serializePublicKey(p_cpu, true);
    unsigned char h = ser.back();
    int idx = h % jumpTable.size();
    secp256k1_pubkey p_next_cpu = p_cpu;
    ecc.addPoints(p_next_cpu, jumpTable[idx].point);

    // 3. Calculate 1 step on GPU
    // We MUST use batch size 32 for SIMD operations to work correctly
    int verifyBatch = 32;
    std::vector<unsigned char> gpuPt(verifyBatch * 64);
    std::vector<unsigned char> pub = ecc.serializePublicKey(p_cpu, false);

    // Fill all 32 slots with the same point to ensure valid SIMD execution
    for (int i = 0; i < verifyBatch; i++) {
      std::memcpy(gpuPt.data() + i * 64, pub.data() + 1, 32);
      std::memcpy(gpuPt.data() + i * 64 + 32, pub.data() + 33, 32);
    }

    std::vector<unsigned char> gpuDist(verifyBatch * 32, 0);

    std::vector<MetalAccelerator::FoundDP> dummyFound;
    metalAccel.runStep(gpuPt, gpuDist, 1, dpBits, dummyFound);

    // 4. Compare (check slot 0)
    std::vector<unsigned char> resPub(65);
    resPub[0] = 0x04;
    std::memcpy(resPub.data() + 1, gpuPt.data(), 32);
    std::memcpy(resPub.data() + 33, gpuPt.data() + 32, 32);

    secp256k1_pubkey p_gpu;
    if (!ecc.parsePublicKey(p_gpu, resPub)) {
      std::cerr << "GPU returned invalid point (not on curve or bad format)!"
                << std::endl;
      // Dump bytes
      std::cout << "GPU X: "
                << Utils::bytesToHex(std::vector<unsigned char>(
                       gpuPt.begin(), gpuPt.begin() + 32))
                << std::endl;
      std::cout << "GPU Y: "
                << Utils::bytesToHex(std::vector<unsigned char>(
                       gpuPt.begin() + 32, gpuPt.begin() + 64))
                << std::endl;
      std::cout << "Falling back to CPU solver." << std::endl;
      // omp_set_num_threads(numThreads);
      // goto cpu_fallback;
      std::cout << "IGNORING ERROR for Debugging. Continuing on GPU..."
                << std::endl;
    } else {
      std::vector<unsigned char> serGPU = ecc.serializePublicKey(p_gpu, true);
      std::vector<unsigned char> serCPU =
          ecc.serializePublicKey(p_next_cpu, true);
      if (serGPU == serCPU) {
        std::cout << "GPU Math Verification PASSED." << std::endl;
      } else {
        std::cerr
            << "GPU Math Verification FAILED! (Running in EXPERIMENTAL Mode)"
            << std::endl;
        // std::cerr << "CPU: " << Utils::bytesToHex(serCPU) << std::endl;
        // std::cerr << "GPU: " << Utils::bytesToHex(serGPU) << std::endl;
      }
    }

    // 5. Verify Scalar Addition (Mod N)
    std::cout << "Verifying GPU Scalar Addition..." << std::endl;

    gmp_randclass rr_verify(gmp_randinit_default);
    rr_verify.seed(time(NULL));

    mpz_class secp_n;
    secp_n.set_str(
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141", 16);

    mpz_class s1 = rr_verify.get_z_range(secp_n);
    mpz_class s2 = rr_verify.get_z_range(secp_n);
    mpz_class s_sum = (s1 + s2) % secp_n;

    std::vector<unsigned char> s1Bytes(32), s2Bytes(32), sumBytes(32);
    Utils::mpzToBytes(s1.get_mpz_t(), s1Bytes.data());
    Utils::mpzToBytes(s2.get_mpz_t(), s2Bytes.data());
    Utils::mpzToBytes(s_sum.get_mpz_t(), sumBytes.data());

    std::vector<unsigned char> gpuSum =
        metalAccel.runMathTest(3, s1Bytes, s2Bytes);

    if (gpuSum == sumBytes) {
      std::cout << "GPU Scalar Addition PASSED." << std::endl;
    } else {
      std::cerr << "GPU Scalar Addition FAILED!" << std::endl;
      std::cerr << "A: " << Utils::bytesToHex(s1Bytes) << std::endl;
      std::cerr << "B: " << Utils::bytesToHex(s2Bytes) << std::endl;
      std::cerr << "Expected: " << Utils::bytesToHex(sumBytes) << std::endl;
      std::cerr << "Got:      " << Utils::bytesToHex(gpuSum) << std::endl;
    }

    // GPU Solver Loop
    std::cout << "Generating " << gpuBatchSize << " kangaroos for GPU..."
              << std::endl;

    std::vector<unsigned char> gpuPoints(gpuBatchSize * 64);
    std::vector<unsigned char> gpuDists(gpuBatchSize * 32);

    // Init random points ONCE
    gmp_randclass rr(gmp_randinit_default);
    rr.seed(time(NULL));

    for (int i = 0; i < gpuBatchSize; i++) {
      mpz_class offset = rr.get_z_range(rangeSize);
      bool isTame = (i % 2 == 0);

      mpz_class startD;
      secp256k1_pubkey pt;

      if (isTame) {
        // Tame kangaroos should start AHEAD of the key.
        // Since we don't know the key, starting at endRange ensures we are
        // ahead of any key in [startRange, endRange].
        mpz_class base = endRange;
        startD = base + offset;
        unsigned char scalar[32];
        Utils::mpzToBytes(startD.get_mpz_t(), scalar);
        ecc.getPubKeyFromPriv(pt, scalar);
      } else {
        startD = offset;
        secp256k1_pubkey p = targetPubKey;
        unsigned char scalar[32];
        Utils::mpzToBytes(offset.get_mpz_t(), scalar);
        ecc.addScalar(p, scalar);
        pt = p;
      }

      // Store Dist
      std::vector<unsigned char> dBytes(32);
      Utils::mpzToBytes(startD.get_mpz_t(), dBytes.data());
      std::memcpy(gpuDists.data() + i * 32, dBytes.data(), 32);

      // Store Point
      std::vector<unsigned char> pub = ecc.serializePublicKey(pt, false);
      std::memcpy(gpuPoints.data() + i * 64, pub.data() + 1, 32);
      std::memcpy(gpuPoints.data() + i * 64 + 32, pub.data() + 33, 32);
    }

    std::cout << "Entering GPU Solver Loop..." << std::endl;

    // Main GPU Loop
    while (!shouldStop) {
      // Run steps
      std::vector<MetalAccelerator::FoundDP> foundDPs;
      metalAccel.runStep(gpuPoints, gpuDists, stepsPerLaunch, dpBits, foundDPs);

      // Update stats
      totalJumps += (uint64_t)gpuBatchSize * stepsPerLaunch;

      // Process found DPs from GPU
      for (const auto& dp : foundDPs) {
        // Reconstruct point
        std::vector<unsigned char> pub(65);
        pub[0] = 0x04;
        std::memcpy(pub.data() + 1, dp.x.data(), 32);
        std::memcpy(pub.data() + 33, dp.y.data(), 32);

        secp256k1_pubkey pt;
        if (ecc.parsePublicKey(pt, pub)) {
          if (isDistinguished(pt)) {
            mpz_class dist;
            unsigned char dBuf[32];
            std::memcpy(dBuf, dp.dist.data(), 32);
            Utils::bytesToMpz(dist.get_mpz_t(), dBuf);

            bool isTame = (dp.id % 2 == 0);
            std::string hex =
                Utils::bytesToHex(ecc.serializePublicKey(pt, true));

            {
              std::lock_guard<std::mutex> lock(mapMutex);
              processCollision(hex, dist, isTame);
              if (found)
                break;
            }
          }
        }
      }

      if (found) {
        shouldStop = true;
        return;  // Done!
      }

      // If not found, loop continues with current kangaroos (they keep
      // walking). This is the correct Pollard's Lambda behavior.
    }
    return;  // GPU finished (found or stopped)
  }

  // cpu_fallback:
  omp_set_num_threads(numThreads);

#pragma omp parallel
  {
    int id = omp_get_thread_num();
    bool isTame = (id < tameCount);

    mpz_class startDist = 0;
    secp256k1_pubkey startPoint;

    gmp_randclass rr(gmp_randinit_default);
    rr.seed(time(NULL) + id);
    mpz_class offset = rr.get_z_range(rangeSize / 100 + 1);  // Small offset

    if (isTame) {
      // Tame starts at End to be ahead of Wild
      mpz_class base = endRange;
      mpz_class myStart = base + offset;
      startDist = myStart;  // This is the absolute scalar value

      unsigned char scalar[32];
      Utils::mpzToBytes(myStart.get_mpz_t(), scalar);
      ecc.getPubKeyFromPriv(startPoint, scalar);
    } else {
      // Wild
      startDist = offset;  // We track distance ADDED to Target

      secp256k1_pubkey p = targetPubKey;
      unsigned char scalar[32];
      Utils::mpzToBytes(offset.get_mpz_t(), scalar);
      ecc.addScalar(p, scalar);
      startPoint = p;
    }

    // Worker Loop
    mpz_class currentDist = startDist;
    secp256k1_pubkey currentPoint = startPoint;
    mpz_class dist = currentDist;

    int jumpTableSize = jumpTable.size();

    while (!shouldStop) {
      std::vector<unsigned char> ser =
          ecc.serializePublicKey(currentPoint, true);
      unsigned char h = ser.back();
      int idx = h % jumpTableSize;

      const Jump& jump = jumpTable[idx];
      ecc.addPoints(currentPoint, jump.point);
      dist += jump.dist;
      totalJumps++;

      if (isDistinguished(currentPoint)) {
        std::string hex = Utils::bytesToHex(ser);
        {
          std::lock_guard<std::mutex> lock(mapMutex);
          if (shouldStop)
            break;
          processCollision(hex, dist, isTame);
        }
      }

      if (totalJumps % 1000 == 0 && shouldStop)
        break;
    }
  }
}
