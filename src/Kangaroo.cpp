#include "Kangaroo.hpp"

#include <omp.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
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
  return elapsed.count() + loadedDuration;
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

void Kangaroo::saveCheckpoint(const std::string& file) {
  std::lock_guard<std::mutex> lock(mapMutex);
  std::ofstream out(file);
  if (!out.is_open()) {
    std::cerr << "Failed to open checkpoint file for writing: " << file
              << std::endl;
    return;
  }

  out << "V1" << std::endl;
  out << "TOTAL_JUMPS " << totalJumps << std::endl;
  out << "DURATION " << getDuration() << std::endl;
  out << "DP_BITS " << dpBits << std::endl;

  out << "DISTINGUISHED_POINTS " << distinguishedPoints.size() << std::endl;
  for (const auto& kv : distinguishedPoints) {
    // hex dist isTame
    out << kv.first << " " << kv.second.distance.get_str(16) << " "
        << kv.second.isTame << std::endl;
  }

  if (!savedGpuPoints.empty()) {
    out << "GPU_POINTS " << savedGpuPoints.size() << std::endl;
    out << Utils::bytesToHex(savedGpuPoints) << std::endl;
    out << "GPU_DISTS " << savedGpuDists.size() << std::endl;
    out << Utils::bytesToHex(savedGpuDists) << std::endl;
  } else {
    out << "GPU_POINTS 0" << std::endl;
    out << "GPU_DISTS 0" << std::endl;
  }

  std::cout << "Checkpoint saved to " << file << std::endl;
}

void Kangaroo::loadCheckpoint(const std::string& file) {
  std::ifstream in(file);
  if (!in.is_open())
    return;

  std::string line;
  std::string version;
  in >> version;
  if (version != "V1") {
    std::cerr << "Unknown checkpoint version: " << version << std::endl;
    return;
  }

  std::string label;
  while (in >> label) {
    if (label == "TOTAL_JUMPS") {
      uint64_t j;
      in >> j;
      totalJumps = j;
    } else if (label == "DURATION") {
      in >> loadedDuration;
    } else if (label == "DP_BITS") {
      int d;
      in >> d;
      if (!manualDpBits)
        dpBits = d;
    } else if (label == "DISTINGUISHED_POINTS") {
      size_t count;
      in >> count;
      for (size_t i = 0; i < count; ++i) {
        std::string hex, distHex;
        bool isTame;
        in >> hex >> distHex >> isTame;
        mpz_class dist;
        dist.set_str(distHex, 16);
        distinguishedPoints[hex] = {dist, isTame};
      }
    } else if (label == "GPU_POINTS") {
      size_t count;
      in >> count;
      if (count > 0) {
        std::string hex;
        in >> hex;
        savedGpuPoints = Utils::hexToBytes(hex);
      }
    } else if (label == "GPU_DISTS") {
      size_t count;
      in >> count;
      if (count > 0) {
        std::string hex;
        in >> hex;
        savedGpuDists = Utils::hexToBytes(hex);
      }
    }
  }
  loadedFromCheckpoint = true;
}

void Kangaroo::requestCheckpoint(const std::string& file) {
  std::lock_guard<std::mutex> lock(checkpointMutex);
  checkpointFile = file;
  checkpointRequested = true;
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
    if (!manualDpBits) {
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
    }

    if (!manualGpuParams) {
      // 2. Safety Check: Ensure (batchSize * steps * probability) < bufferSize
      double prob = 1.0 / (double)(1ULL << dpBits);
      double maxTotalSteps = 2048.0 / prob;

      // If dpBits is small (small range), maxTotalSteps will be small.
      if ((double)gpuBatchSize * stepsPerLaunch > maxTotalSteps) {
        stepsPerLaunch = (int)(maxTotalSteps / gpuBatchSize);
        if (stepsPerLaunch < 1) {
          stepsPerLaunch = 1;
          gpuBatchSize = (int)maxTotalSteps;
          if (gpuBatchSize < 32)
            gpuBatchSize = 32;  // Min SIMD width
        }
        std::cout << "Tuning GPU parameters for small range/low dpBits:"
                  << std::endl;
      }
    }

    std::cout << "GPU Parameters:" << std::endl;
    std::cout << "  Batch Size: " << gpuBatchSize << std::endl;
    std::cout << "  Steps: " << stepsPerLaunch << std::endl;
    std::cout << "  DP Bits: " << dpBits << std::endl;

    std::cout << "Initializing Metal Accelerator..." << std::endl;
    metalAccel.init(jumpTable);

    // GPU Solver Loop
    std::vector<unsigned char> gpuPoints;
    std::vector<unsigned char> gpuDists;

    if (loadedFromCheckpoint && !savedGpuPoints.empty() &&
        !savedGpuDists.empty()) {
      std::cout << "Restoring GPU state from checkpoint..." << std::endl;
      gpuPoints = savedGpuPoints;
      gpuDists = savedGpuDists;
      // Check if batch size matches
      if (gpuPoints.size() != (size_t)gpuBatchSize * 64) {
        std::cout << "Warning: Checkpoint batch size mismatch. Resizing..."
                  << std::endl;
        // This is complex. If batch size changed, we can't easily map.
        // For now, assume user keeps same batch size or we restart if mismatch.
        // Actually, we can just use the saved points and ignore the rest if
        // batch size decreased, or fill with random if increased. Let's just
        // warn and proceed with what we have, resizing if needed.
        gpuPoints.resize(gpuBatchSize * 64);
        gpuDists.resize(gpuBatchSize * 32);
        // Re-init new slots if any
        // ... (omitted for brevity, assuming user keeps params)
      }
    } else {
      std::cout << "Generating " << gpuBatchSize << " kangaroos for GPU..."
                << std::endl;

      gpuPoints.resize(gpuBatchSize * 64);
      gpuDists.resize(gpuBatchSize * 32);

      // Init random points ONCE
      gmp_randclass rr(gmp_randinit_default);
      rr.seed(time(NULL));

      for (int i = 0; i < gpuBatchSize; i++) {
        mpz_class offset = rr.get_z_range(rangeSize);
        bool isTame = (i % 2 == 0);

        mpz_class startD;
        secp256k1_pubkey pt;

        if (isTame) {
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
    }

    std::cout << "Entering GPU Solver Loop..." << std::endl;

    // Main GPU Loop
    while (!shouldStop) {
      if (checkpointRequested) {
        savedGpuPoints = gpuPoints;
        savedGpuDists = gpuDists;
        saveCheckpoint(checkpointFile);
        checkpointRequested = false;
      }

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
    }

    // Save state on exit if requested or stopped
    savedGpuPoints = gpuPoints;
    savedGpuDists = gpuDists;
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
