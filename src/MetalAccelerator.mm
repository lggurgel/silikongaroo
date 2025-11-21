#include "MetalAccelerator.hpp"
#include "Kangaroo.hpp"
#include "Utils.hpp"
#include <cstring> // for memcpy
#include <iostream>
#include <mach-o/dyld.h>
#include <vector>

// Helper to copy 32-byte mpz to buffer
void copyBigIntToBuffer(const mpz_class &val, std::vector<unsigned char> &buf) {
  unsigned char temp[32];
  Utils::mpzToBytes(val.get_mpz_t(), temp);
  // Metal side expects specific endianness?
  // Our Utils::mpzToBytes produces Big Endian.
  // Metal (and ARM64) is Little Endian.
  // We need to reverse bytes for uint256 struct in Metal which is array of
  // uints. Wait, if uint256 is array of uint, val[0] is low. Utils output: Byte
  // 0 is MSB. We need to reverse it to be Little Endian (LSB at buf[0]).
  for (int i = 0; i < 32; i++)
    buf.push_back(temp[31 - i]);
}

void copyBytesToBuffer(const unsigned char *bytes,
                       std::vector<unsigned char> &buf) {
  // Input bytes are Big Endian (from serializatin).
  // Reverse to Little Endian for Metal
  for (int i = 0; i < 32; i++)
    buf.push_back(bytes[31 - i]);
}

MetalAccelerator::MetalAccelerator() {
  device = MTLCreateSystemDefaultDevice();
  if (!device) {
    std::cerr << "Metal is not supported on this device" << std::endl;
    return;
  }

  commandQueue = [device newCommandQueue];

  NSError *error = nil;
  NSString *libPath = @"default.metallib";

  // Try current directory first
  NSString *cwd = [[NSFileManager defaultManager] currentDirectoryPath];
  NSString *fullPath = [cwd stringByAppendingPathComponent:libPath];

  bool found = [[NSFileManager defaultManager] fileExistsAtPath:fullPath];

  if (!found) {
    // Try subdirectory 'build'
    NSString *buildPath = [[cwd stringByAppendingPathComponent:@"build"]
        stringByAppendingPathComponent:libPath];
    if ([[NSFileManager defaultManager] fileExistsAtPath:buildPath]) {
      fullPath = buildPath;
      found = true;
    }
  }

  if (!found) {
    // Try directory where executable is located
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
      NSString *execPath = [NSString stringWithUTF8String:path];
      NSString *execDir = [execPath stringByDeletingLastPathComponent];
      NSString *execLibPath = [execDir stringByAppendingPathComponent:libPath];
      if ([[NSFileManager defaultManager] fileExistsAtPath:execLibPath]) {
        fullPath = execLibPath;
        found = true;
      }
    }
  }

  NSURL *url = [NSURL fileURLWithPath:fullPath];
  id<MTLLibrary> library = [device newLibraryWithURL:url error:&error];

  if (!library) {
    std::cerr << "Failed to load Metal library from " << [fullPath UTF8String]
              << ": " << [[error localizedDescription] UTF8String] << std::endl;
    computePipelineState = nil; // Ensure it's nil
    return;
  }

  id<MTLFunction> kernelFunction =
      [library newFunctionWithName:@"kangaroo_step"];
  if (!kernelFunction) {
    std::cerr << "Failed to find kernel function 'kangaroo_step'" << std::endl;
    return;
  }

  computePipelineState =
      [device newComputePipelineStateWithFunction:kernelFunction error:&error];
  if (!computePipelineState) {
    std::cerr << "Failed to create pipeline state: " <<
        [[error localizedDescription] UTF8String] << std::endl;
    return;
  }

  // Debug pipeline
  id<MTLFunction> testFunction = [library newFunctionWithName:@"test_math"];
  if (testFunction) {
    testMathPipelineState =
        [device newComputePipelineStateWithFunction:testFunction error:&error];
  }

  std::cout << "Metal initialized successfully on "
            << [[device name] UTF8String] << std::endl;
}

MetalAccelerator::~MetalAccelerator() {
  // ARC handles release
}

void MetalAccelerator::init(const std::vector<Jump> &jumpTable) {
  // Upload jump table to GPU Buffers
  // Structure of Arrays: X[], Y[], Dist[]

  std::vector<unsigned char> tableX;
  std::vector<unsigned char> tableY;
  std::vector<unsigned char> tableDist;

  ECC ecc; // Temp ecc for context

  for (const auto &jump : jumpTable) {
    // Dist
    copyBigIntToBuffer(jump.dist, tableDist);

    // Point
    // We need X and Y coordinates.
    // Serialize uncompressed (65 bytes: 04 X Y)
    std::vector<unsigned char> pub = ecc.serializePublicKey(jump.point, false);
    // X is at [1..32], Y is at [33..64]

    copyBytesToBuffer(pub.data() + 1, tableX);
    copyBytesToBuffer(pub.data() + 33, tableY);
  }

  // Create buffers
  jumpTableXBuffer = [device newBufferWithBytes:tableX.data()
                                         length:tableX.size()
                                        options:MTLResourceStorageModeShared];
  jumpTableYBuffer = [device newBufferWithBytes:tableY.data()
                                         length:tableY.size()
                                        options:MTLResourceStorageModeShared];
  jumpTableDistBuffer =
      [device newBufferWithBytes:tableDist.data()
                          length:tableDist.size()
                         options:MTLResourceStorageModeShared];

  tableSize = (uint32_t)jumpTable.size();
}

void MetalAccelerator::runStep(std::vector<unsigned char> &points,
                               std::vector<unsigned char> &distances,
                               int numSteps, int dpBits,
                               std::vector<FoundDP> &foundDPs) {
  if (!computePipelineState) {
    std::cerr
        << "Error: Metal pipeline state is not initialized. Skipping GPU step."
        << std::endl;
    return;
  }

  NSUInteger count = points.size() / 64; // X + Y (32+32)
  if (count == 0)
    return;

  std::vector<unsigned char> bufX;
  std::vector<unsigned char> bufY;
  std::vector<unsigned char> bufDist;
  bufX.reserve(count * 32);
  bufY.reserve(count * 32);
  bufDist.reserve(count * 32);

  for (size_t i = 0; i < count; i++) {
    // Copy X (Reverse to Little Endian)
    for (int j = 0; j < 32; j++)
      bufX.push_back(points[i * 64 + 31 - j]);
    // Copy Y (Reverse to Little Endian)
    for (int j = 0; j < 32; j++)
      bufY.push_back(points[i * 64 + 63 - j]);
    // Copy Dist (Reverse to Little Endian)
    for (int j = 0; j < 32; j++)
      bufDist.push_back(distances[i * 32 + 31 - j]);
  }

  id<MTLBuffer> bufferX =
      [device newBufferWithBytes:bufX.data()
                          length:bufX.size()
                         options:MTLResourceStorageModeShared];
  id<MTLBuffer> bufferY =
      [device newBufferWithBytes:bufY.data()
                          length:bufY.size()
                         options:MTLResourceStorageModeShared];
  id<MTLBuffer> bufferDist =
      [device newBufferWithBytes:bufDist.data()
                          length:bufDist.size()
                         options:MTLResourceStorageModeShared];

  // Found DPs buffers
  uint32_t maxFound = 4096;
  id<MTLBuffer> foundX =
      [device newBufferWithLength:maxFound * 32
                          options:MTLResourceStorageModeShared];
  id<MTLBuffer> foundY =
      [device newBufferWithLength:maxFound * 32
                          options:MTLResourceStorageModeShared];
  id<MTLBuffer> foundDist =
      [device newBufferWithLength:maxFound * 32
                          options:MTLResourceStorageModeShared];
  id<MTLBuffer> foundIds =
      [device newBufferWithLength:maxFound * sizeof(uint32_t)
                          options:MTLResourceStorageModeShared];
  id<MTLBuffer> foundCount =
      [device newBufferWithLength:sizeof(uint32_t)
                          options:MTLResourceStorageModeShared];

  // Reset foundCount
  std::memset([foundCount contents], 0, sizeof(uint32_t));

  id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
  id<MTLComputeCommandEncoder> computeEncoder =
      [commandBuffer computeCommandEncoder];

  [computeEncoder setComputePipelineState:computePipelineState];

  [computeEncoder setBuffer:bufferX offset:0 atIndex:0];
  [computeEncoder setBuffer:bufferY offset:0 atIndex:1];
  [computeEncoder setBuffer:bufferDist offset:0 atIndex:2];

  uint32_t steps = (uint32_t)numSteps;
  [computeEncoder setBytes:&steps length:sizeof(uint32_t) atIndex:3];

  [computeEncoder setBuffer:jumpTableXBuffer offset:0 atIndex:4];
  [computeEncoder setBuffer:jumpTableYBuffer offset:0 atIndex:5];
  [computeEncoder setBuffer:jumpTableDistBuffer offset:0 atIndex:6];
  [computeEncoder setBytes:&tableSize length:sizeof(uint32_t) atIndex:7];

  [computeEncoder setBuffer:foundX offset:0 atIndex:8];
  [computeEncoder setBuffer:foundY offset:0 atIndex:9];
  [computeEncoder setBuffer:foundDist offset:0 atIndex:10];
  [computeEncoder setBuffer:foundIds offset:0 atIndex:11];
  [computeEncoder setBuffer:foundCount offset:0 atIndex:12];

  uint32_t dpBitsVal = (uint32_t)dpBits;
  [computeEncoder setBytes:&dpBitsVal length:sizeof(uint32_t) atIndex:13];
  [computeEncoder setBytes:&maxFound length:sizeof(uint32_t) atIndex:14];

  MTLSize gridSize = MTLSizeMake(count, 1, 1);
  id<MTLComputePipelineState> pso =
      (id<MTLComputePipelineState>)computePipelineState;
  NSUInteger threadGroupSize = pso.maxTotalThreadsPerThreadgroup;
  if (threadGroupSize > count)
    threadGroupSize = count;

  // Ensure threadGroupSize is a multiple of 32 (SIMD width) for batched inverse
  if (threadGroupSize > 32) {
    threadGroupSize = (threadGroupSize / 32) * 32;
  } else if (threadGroupSize < 32) {
    threadGroupSize = 32;
  }

  MTLSize threadgroupSize = MTLSizeMake(threadGroupSize, 1, 1);

  [computeEncoder dispatchThreads:gridSize
            threadsPerThreadgroup:threadgroupSize];

  [computeEncoder endEncoding];
  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];

  // Copy back results (interleave X/Y)
  unsigned char *ptrX = (unsigned char *)[bufferX contents];
  unsigned char *ptrY = (unsigned char *)[bufferY contents];

  for (size_t i = 0; i < count; i++) {
    // Reverse back to Big Endian
    for (int j = 0; j < 32; j++)
      points[i * 64 + j] = ptrX[i * 32 + 31 - j];
    for (int j = 0; j < 32; j++)
      points[i * 64 + 32 + j] = ptrY[i * 32 + 31 - j];
  }

  unsigned char *ptrDist = (unsigned char *)[bufferDist contents];
  for (size_t i = 0; i < count; i++) {
    for (int j = 0; j < 32; j++)
      distances[i * 32 + j] = ptrDist[i * 32 + 31 - j];
  }

  // Process Found DPs
  uint32_t numFound = *(uint32_t *)[foundCount contents];
  // Debug print
  // std::cout << "GPU Found Count: " << numFound << std::endl;

  if (numFound > maxFound)
    numFound = maxFound;

  unsigned char *fX = (unsigned char *)[foundX contents];
  unsigned char *fY = (unsigned char *)[foundY contents];
  unsigned char *fD = (unsigned char *)[foundDist contents];
  uint32_t *fIds = (uint32_t *)[foundIds contents];

  for (uint32_t i = 0; i < numFound; i++) {
    FoundDP dp;
    dp.id = fIds[i];
    dp.x.resize(32);
    dp.y.resize(32);
    dp.dist.resize(32);

    // Metal stores Little Endian. We need Big Endian for Utils/CPU?
    // Wait, copyBigIntToBuffer reversed it.
    // So Metal has Little Endian.
    // Utils::bytesToHex expects Big Endian?
    // Let's check Utils.cpp later. Assuming we need to reverse back.
    // The `runMathTest` reverses back.
    // So we should reverse back here too.

    for (int j = 0; j < 32; j++)
      dp.x[j] = fX[i * 32 + (31 - j)];
    for (int j = 0; j < 32; j++)
      dp.y[j] = fY[i * 32 + (31 - j)];
    for (int j = 0; j < 32; j++)
      dp.dist[j] = fD[i * 32 + (31 - j)];

    foundDPs.push_back(dp);
  }
}

std::vector<unsigned char>
MetalAccelerator::runMathTest(int op, const std::vector<unsigned char> &a,
                              const std::vector<unsigned char> &b) {
  if (!testMathPipelineState) {
    std::cerr << "Error: Test pipeline state is not initialized." << std::endl;
    return {};
  }

  // a, b are Big Endian 32 bytes. Convert to Little Endian for GPU.
  std::vector<unsigned char> bufA;
  std::vector<unsigned char> bufB;
  copyBytesToBuffer(a.data(), bufA);
  copyBytesToBuffer(b.data(), bufB);

  // Create buffers
  id<MTLBuffer> bufferA =
      [device newBufferWithBytes:bufA.data()
                          length:32
                         options:MTLResourceStorageModeShared];
  id<MTLBuffer> bufferB =
      [device newBufferWithBytes:bufB.data()
                          length:32
                         options:MTLResourceStorageModeShared];
  id<MTLBuffer> bufferOut =
      [device newBufferWithLength:32 options:MTLResourceStorageModeShared];

  id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
  id<MTLComputeCommandEncoder> computeEncoder =
      [commandBuffer computeCommandEncoder];

  [computeEncoder setComputePipelineState:testMathPipelineState];
  [computeEncoder setBuffer:bufferA offset:0 atIndex:0];
  [computeEncoder setBuffer:bufferB offset:0 atIndex:1];
  [computeEncoder setBuffer:bufferOut offset:0 atIndex:2];
  uint32_t opVal = (uint32_t)op;
  [computeEncoder setBytes:&opVal length:sizeof(uint32_t) atIndex:3];

  MTLSize gridSize = MTLSizeMake(1, 1, 1);
  MTLSize threadgroupSize = MTLSizeMake(1, 1, 1);
  [computeEncoder dispatchThreads:gridSize
            threadsPerThreadgroup:threadgroupSize];

  [computeEncoder endEncoding];
  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];

  // Copy back result (Little Endian) and convert to Big Endian
  unsigned char *ptrOut = (unsigned char *)[bufferOut contents];
  std::vector<unsigned char> result;
  result.resize(32);
  for (int i = 0; i < 32; i++)
    result[i] = ptrOut[31 - i];

  return result;
}
