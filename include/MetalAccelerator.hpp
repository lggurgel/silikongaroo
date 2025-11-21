#pragma once

#include <secp256k1.h>

#include <string>
#include <vector>

#ifdef __OBJC__
#import <Metal/Metal.h>
#else
typedef void* id;
#endif

// Forward declarations
struct Jump;

class MetalAccelerator {
 public:
  MetalAccelerator();
  ~MetalAccelerator();

  void init(const std::vector<Jump>& jumpTable);

  struct FoundDP {
    uint32_t id;
    std::vector<unsigned char> x;
    std::vector<unsigned char> y;
    std::vector<unsigned char> dist;
  };

  // Run a batch of kangaroos
  // Returns true if any kangaroo found the key (though exact key retrieval
  // might need CPU check) 'points': input/output kangaroo positions (64 bytes
  // per point: 32 bytes X + 32 bytes Y) 'distances': input/output distances (32
  // bytes per distance)
  void runStep(std::vector<unsigned char>& points,
               std::vector<unsigned char>& distances, int numSteps, int dpBits,
               std::vector<FoundDP>& foundDPs);

  // Debug: Run a math test on GPU
  // op: 0=add, 1=mul, 2=inv
  // a, b: 32-byte big-endian integers
  // returns: 32-byte big-endian result
  std::vector<unsigned char> runMathTest(int op,
                                         const std::vector<unsigned char>& a,
                                         const std::vector<unsigned char>& b);

 private:
  id device;
  id commandQueue;
  id computePipelineState;
  id testMathPipelineState;  // Debug pipeline

  id jumpTableXBuffer;
  id jumpTableYBuffer;
  id jumpTableDistBuffer;

  uint32_t tableSize;
};
