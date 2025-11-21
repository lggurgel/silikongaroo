#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "Kangaroo.hpp"
#include "Utils.hpp"

void signalHandler(int signum) {
  std::cout << "\nInterrupt signal (" << signum << ") received.\n";
  exit(signum);
}

void printUsage() {
  std::cout << "Usage: silikangaroo <public_key_hex> <start_range_hex> "
               "<end_range_hex> [num_threads] [--gpu]\n";
}

int main(int argc, char* argv[]) {
  signal(SIGINT, signalHandler);

  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::string targetPubHex = argv[1];
  std::string startHex = argv[2];
  std::string endHex = argv[3];
  int threads = -1;
  bool useGPU = false;

  // Parse optional args
  for (int i = 4; i < argc; ++i) {
    if (strcmp(argv[i], "--gpu") == 0) {
      useGPU = true;
    } else {
      // Assume it's thread count if it's a number
      try {
        threads = std::stoi(argv[i]);
      } catch (...) {
        // ignore
      }
    }
  }

  mpz_class start, end;
  // Auto-detect base (0 handles 0x prefix for hex, otherwise decimal)
  if (start.set_str(startHex, 0) != 0) {
    std::cerr << "Error parsing start range: " << startHex << std::endl;
    return 1;
  }
  if (end.set_str(endHex, 0) != 0) {
    std::cerr << "Error parsing end range: " << endHex << std::endl;
    return 1;
  }

  std::cout << "Silikangaroo v0.1.0 - M1 Optimized" << std::endl;
  std::cout << "Target: " << targetPubHex << std::endl;
  std::cout << "Range: [" << start.get_str(16) << ", " << end.get_str(16) << "]"
            << std::endl;
  if (useGPU)
    std::cout << "Mode: GPU Accelerated (Metal)" << std::endl;

  try {
    Kangaroo kangaroo(start, end, targetPubHex, threads);
    kangaroo.setUseGPU(useGPU);

    // Monitor thread
    std::thread monitor([&]() {
      // auto startTime = std::chrono::steady_clock::now();
      while (!kangaroo.isFound()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        double duration = kangaroo.getDuration();
        uint64_t jumps = kangaroo.getTotalJumps();
        double rate = duration > 0 ? jumps / duration : 0;

        std::cout << "\rTime: " << (int)duration << "s | Jumps: " << jumps
                  << " | Rate: " << std::fixed << std::setprecision(2)
                  << rate / 1000000.0 << " M/jumps/s" << std::flush;

        // Check if we should exit? run() will return if found.
        // But we are in a loop. We need to know if run finished.
        // isFound is true.
      }
    });
    monitor.detach();  // Simple detach for MVP

    kangaroo.run();

    if (kangaroo.isFound()) {
      std::cout << "\n\nSUCCESS! Private Key Found!" << std::endl;
      std::cout << "Private Key: " << kangaroo.getPrivateKey().get_str(16)
                << std::endl;

      // Verification is implicit in the collision check, but good to double
      // check.
    } else {
      std::cout << "\n\nSearch finished without finding key (or stopped)."
                << std::endl;
    }

  } catch (const std::exception& e) {
    std::cerr << "\nError: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
