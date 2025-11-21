# Changelog

All notable changes to Silikangaroo will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned
- Save/resume functionality for long-running searches
- Multiple GPU support for M1 Ultra and Mac Studio
- Web dashboard for monitoring
- Distributed solving across multiple machines

## [0.1.0] - 2025-11-21

### Added
- Initial release of Silikangaroo
- Pollard's Kangaroo algorithm implementation
- Metal GPU acceleration with custom kernels
- 256-bit modular arithmetic in Metal
- Batched modular inversion (32-way SIMD)
- CPU fallback mode with OpenMP
- Distinguished points collision detection
- Real-time performance statistics
- Helper tool `gen_key` for key generation
- Support for Apple Silicon (M1/M2/M3/M4)
- Comprehensive build system with CMake
- Integration with libsecp256k1
- Documentation and examples

### Features
- GPU achieves ~25-50 million jumps/sec on M1 Max
- Automatic distinguished point bit calculation
- Threadgroup memory optimization for jump tables
- Jacobian coordinate point operations
- Compressed public key support

### Tested On
- Puzzle 20 ✓
- Puzzle 30 ✓
- Puzzle 60 (partial)

## [0.0.1] - Development

### Added
- Initial proof of concept
- Basic CPU-only kangaroo implementation
- Early Metal experimentation

---

## Version History Summary

- **0.1.0**: First public release with full GPU support
- **0.0.1**: Internal development version

[Unreleased]: https://github.com/yourusername/silikangaroo/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/yourusername/silikangaroo/releases/tag/v0.1.0
[0.0.1]: https://github.com/yourusername/silikangaroo/releases/tag/v0.0.1

