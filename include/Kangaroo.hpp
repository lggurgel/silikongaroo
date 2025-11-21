#pragma once

#include <gmpxx.h>
#include <secp256k1.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ECC.hpp"
#include "MetalAccelerator.hpp"

struct Jump {
  mpz_class dist;
  secp256k1_pubkey point;
};

struct DistinguishedPoint {
  mpz_class distance;
  bool isTame;  // true = tame, false = wild
                // potentially store starting info if multiple kangaroos
};

class Kangaroo {
 public:
  Kangaroo(const mpz_class& startRange, const mpz_class& endRange,
           const std::string& targetPubKeyHex, int numThreads = -1);
  ~Kangaroo();

  void run();

  mpz_class getPrivateKey() const {
    return privateKey;
  }
  bool isFound() const {
    return found;
  }

  // Statistics
  uint64_t getTotalJumps() const {
    return totalJumps;
  }
  double getDuration() const;
  double getOpsPerSecond() const;
  double getEstimatedSecondsRemaining() const;

  void setUseGPU(bool use) {
    useGPU = use;
  }

 private:
  mpz_class startRange;
  mpz_class endRange;
  mpz_class rangeSize;
  std::string targetHex;
  secp256k1_pubkey targetPubKey;

  int numThreads;
  bool useGPU = false;
  ECC ecc;  // Main ECC context

  MetalAccelerator metalAccel;

  std::vector<Jump> jumpTable;
  void initJumpTable();

  // Shared state
  std::unordered_map<std::string, DistinguishedPoint>
      distinguishedPoints;  // Key: compressed point (33 bytes)
  std::mutex mapMutex;

  std::atomic<bool> found{false};
  std::atomic<bool> shouldStop{false};
  mpz_class privateKey;

  std::atomic<uint64_t> totalJumps{0};
  std::chrono::time_point<std::chrono::high_resolution_clock> startTime;

  // Worker function
  void worker(int id, bool isTame, mpz_class startDist,
              secp256k1_pubkey startPoint);

  // DP condition: e.g. last N bits are zero
  int dpBits;
  bool isDistinguished(const secp256k1_pubkey& point);

  // Helper to process collision
  void processCollision(const std::string& pointHex, const mpz_class& dist,
                        bool isTame);
};
