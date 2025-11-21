#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "Kangaroo.hpp"
#include "Utils.hpp"

// Global pointer for signal handler
Kangaroo* globalKangaroo = nullptr;
int signalCount = 0;

void signalHandler(int signum) {
  std::cout << "\nInterrupt signal (" << signum << ") received.\n";
  signalCount++;
  if (signalCount >= 3) {
    std::cout << "Forcing exit...\n";
    exit(signum);
  }
  if (globalKangaroo) {
    std::cout
        << "Stopping gracefully... (Press Ctrl+C 3 times to force kill)\n";
    globalKangaroo->stop();
  } else {
    exit(signum);
  }
}

void printUsage() {
  std::cout << "Usage: silikangaroo <public_key_hex> <start_range_hex> "
               "<end_range_hex> [options]\n"
            << "Options:\n"
            << "  --threads <n>       Number of CPU threads (default: auto)\n"
            << "  --gpu               Enable GPU acceleration\n"
            << "  --dp <n>            DP Bits (default: auto)\n"
            << "  --batch <n>         GPU Batch size (default: 16384)\n"
            << "  --steps <n>         GPU Steps per launch (default: 256)\n"
            << "  --resume <file>     Resume from checkpoint file\n"
            << "  --checkpoint <file> Checkpoint file to save to (default: "
               "kangaroo.checkpoint)\n"
            << "  --help              Show this help\n";
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
  int dpBits = -1;
  int gpuBatchSize = 16384;
  int gpuSteps = 256;
  std::string resumeFile = "";
  std::string checkpointFile = "kangaroo.checkpoint";

  // Parse optional args
  for (int i = 4; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--gpu") {
      useGPU = true;
    } else if (arg == "--threads" && i + 1 < argc) {
      threads = std::stoi(argv[++i]);
    } else if (arg == "--dp" && i + 1 < argc) {
      dpBits = std::stoi(argv[++i]);
    } else if (arg == "--batch" && i + 1 < argc) {
      gpuBatchSize = std::stoi(argv[++i]);
    } else if (arg == "--steps" && i + 1 < argc) {
      gpuSteps = std::stoi(argv[++i]);
    } else if (arg == "--resume" && i + 1 < argc) {
      resumeFile = argv[++i];
    } else if (arg == "--checkpoint" && i + 1 < argc) {
      checkpointFile = argv[++i];
    } else if (arg == "--help") {
      printUsage();
      return 0;
    } else {
      // Legacy support: if it's a number, assume it's dpBits if we haven't set
      // it, or threads? The user's script uses: pub start end dp_bits --gpu So
      // if we see a raw number at pos 4, treat as dpBits for backward
      // compatibility
      if (i == 4 && isdigit(argv[i][0])) {
        dpBits = std::stoi(argv[i]);
      }
    }
  }

  mpz_class start, end;
  if (start.set_str(startHex, 0) != 0) {
    std::cerr << "Error parsing start range: " << startHex << std::endl;
    return 1;
  }
  if (end.set_str(endHex, 0) != 0) {
    std::cerr << "Error parsing end range: " << endHex << std::endl;
    return 1;
  }

  std::cout << "Silikangaroo v0.2.0 - Checkpoint & Optimization" << std::endl;
  std::cout << "Target: " << targetPubHex << std::endl;
  std::cout << "Range: [" << start.get_str(16) << ", " << end.get_str(16) << "]"
            << std::endl;
  if (useGPU) {
    std::cout << "Mode: GPU Accelerated (Metal)" << std::endl;
    std::cout << "GPU Config: Batch=" << gpuBatchSize << ", Steps=" << gpuSteps
              << std::endl;
  }

  try {
    Kangaroo kangaroo(start, end, targetPubHex, threads);
    globalKangaroo = &kangaroo;

    kangaroo.setUseGPU(useGPU);
    if (dpBits > 0)
      kangaroo.setDpBits(dpBits);
    if (useGPU)
      kangaroo.setGpuParams(gpuBatchSize, gpuSteps);
    kangaroo.setCheckpointFile(checkpointFile);

    if (!resumeFile.empty()) {
      std::cout << "Resuming from " << resumeFile << "..." << std::endl;
      kangaroo.loadCheckpoint(resumeFile);
    }

    // Monitor thread
    std::thread monitor([&]() {
      while (!kangaroo.isFound() && !kangaroo.isStopped()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        double duration = kangaroo.getDuration();
        uint64_t jumps = kangaroo.getTotalJumps();
        double rate = kangaroo.getOpsPerSecond();
        double remainingSeconds = kangaroo.getEstimatedSecondsRemaining();

        std::string timeStr;
        if (remainingSeconds < 0) {
          timeStr = "Calculating...";
        } else if (remainingSeconds > 31536000000.0) {
          timeStr = "> 1000 years";
        } else if (remainingSeconds > 31536000) {
          timeStr =
              std::to_string((int)(remainingSeconds / 31536000)) + " years";
        } else if (remainingSeconds > 86400) {
          timeStr = std::to_string((int)(remainingSeconds / 86400)) + " days";
        } else if (remainingSeconds > 3600) {
          timeStr = std::to_string((int)(remainingSeconds / 3600)) + " hours";
        } else if (remainingSeconds > 60) {
          timeStr = std::to_string((int)(remainingSeconds / 60)) + " minutes";
        } else {
          timeStr = std::to_string((int)remainingSeconds) + " seconds";
        }

        std::cout << "\rTime: " << (int)duration << "s | Rate: " << std::fixed
                  << std::setprecision(2) << rate / 1000000.0
                  << " M/jumps/s | Est: " << timeStr << "      " << std::flush;

        // Auto-save every 5 minutes
        static auto lastSave = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::minutes>(now - lastSave)
                .count() >= 5) {
          kangaroo.requestCheckpoint(checkpointFile);
          lastSave = now;
        }
      }
    });
    monitor.detach();

    kangaroo.run();

    if (kangaroo.isFound()) {
      std::cout << "\n\nSUCCESS! Private Key Found!" << std::endl;
      std::cout << "Private Key: " << kangaroo.getPrivateKey().get_str(16)
                << std::endl;
    } else {
      std::cout << "\n\nSearch finished without finding key (or stopped)."
                << std::endl;
      // Save checkpoint on exit
      kangaroo.saveCheckpoint(checkpointFile);
    }

  } catch (const std::exception& e) {
    std::cerr << "\nError: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
