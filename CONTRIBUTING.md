# Contributing to Silikangaroo

Thank you for your interest in contributing to Silikangaroo! This document provides guidelines for contributing to the project.

## Code of Conduct

- Be respectful and inclusive
- Focus on constructive feedback
- Help make the community welcoming to everyone

## How to Contribute

### Reporting Bugs

Before creating bug reports, please check existing issues. When creating a bug report, include:

- **Description**: Clear description of the bug
- **Steps to Reproduce**: Detailed steps to reproduce the issue
- **Expected Behavior**: What you expected to happen
- **Actual Behavior**: What actually happened
- **Environment**: macOS version, chip (M1/M2/M3), dependencies versions
- **Logs**: Relevant error messages or logs

### Suggesting Enhancements

Enhancement suggestions are tracked as GitHub issues. When creating an enhancement suggestion, include:

- **Use Case**: Describe the problem you're trying to solve
- **Proposed Solution**: Your suggested approach
- **Alternatives**: Other solutions you've considered
- **Additional Context**: Any other relevant information

### Pull Requests

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Test thoroughly on Apple Silicon hardware
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to your branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

#### Pull Request Guidelines

- Follow the existing code style
- Update documentation as needed
- Add tests if applicable
- Ensure the project builds without errors
- Keep PRs focused on a single feature or fix
- Write clear commit messages

### Code Style

- Use consistent indentation (spaces, not tabs)
- Follow C++17 standards
- Comment complex algorithms
- Use descriptive variable names
- Keep functions focused and modular

### Testing

Before submitting a PR:

1. Build the project: `mkdir build && cd build && cmake .. && make`
2. Test with known puzzles (e.g., Puzzle 20)
3. Verify GPU acceleration works correctly
4. Check for memory leaks if adding new features

## Development Setup

### Prerequisites

- macOS with Apple Silicon (M1/M2/M3)
- Xcode Command Line Tools
- Homebrew
- CMake, GMP, libomp

### Building from Source

```bash
git clone https://github.com/yourusername/silikangaroo.git
cd silikangaroo
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Performance Optimization

When contributing performance improvements:

- Benchmark before and after changes
- Test on different Apple Silicon chips if possible
- Consider memory usage alongside speed
- Document any trade-offs

## Metal Shader Development

When modifying Metal kernels (`src/kernels.metal`):

- Test thoroughly as GPU bugs can be subtle
- Verify math correctness against CPU implementation
- Consider SIMD lane alignment (32 threads per group)
- Document any algorithm changes

## Documentation

- Update README.md for user-facing changes
- Add inline comments for complex code
- Update examples if adding new features
- Keep documentation clear and concise

## Questions?

Feel free to open an issue with the `question` label if you need help or clarification.

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

