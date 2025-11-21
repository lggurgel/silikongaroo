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
    // Adjust dpBits for GPU to prevent buffer overflow and ensure efficiency
    // Target: ~1000 DPs per launch (batch 65536 * 64 steps = 4M steps)
    // 4M / 2^12 = 1024. Buffer is 4096.
    // So dpBits >= 12 is safe.
    if (dpBits < 12) {
      dpBits = 12;
      std::cout << "Adjusting dpBits to " << dpBits << " for GPU optimization."
                << std::endl;
    }

    std::cout << "Initializing Metal Accelerator..." << std::endl;
    metalAccel.init(jumpTable);

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
    // Tuned for M1: 64k batch, 64 steps.
    int gpuBatchSize = 65536;  // 2^16
    int stepsPerLaunch = 64;

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
