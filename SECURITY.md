# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

## Reporting a Vulnerability

### DO NOT open public issues for security vulnerabilities

If you discover a security vulnerability, please report it privately by:

1. **Email**: Send details to [REPLACE WITH YOUR EMAIL]
2. **Include**:
   - Description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Suggested fix (if any)

### What to expect

- **Acknowledgment**: Within 48 hours
- **Initial Assessment**: Within 5 business days
- **Fix Timeline**: Depends on severity
  - Critical: Immediate (0-2 days)
  - High: 3-7 days
  - Medium: 1-2 weeks
  - Low: Next release cycle

### Disclosure Policy

- We follow **responsible disclosure** practices
- Security fixes will be released ASAP
- Credit will be given to reporters (unless anonymity requested)
- CVE IDs will be requested for major vulnerabilities

## Security Considerations

### This Software

**Known limitations:**
- Private keys are held in memory during computation (use encrypted memory if needed)
- No built-in rate limiting for distributed attacks
- GPU memory buffers are not zeroed after use

### Best Practices

When using Silikangaroo:

1. **Only on trusted hardware**: Don't run on shared/untrusted systems
2. **Private key handling**: Immediately secure any found keys
3. **Network usage**: Be cautious if implementing network features
4. **Dependencies**: Keep GMP, libomp, and system libraries updated
5. **Source verification**: Always build from official sources

### Out of Scope

The following are **not** security vulnerabilities:

- Performance issues (report as bugs)
- Incompatibility with non-Apple Silicon hardware (by design)
- Theoretical attacks requiring > 2^128 operations
- Issues in Bitcoin protocol itself (out of scope)

## Cryptographic Implementations

This project uses:

- **libsecp256k1**: Industry-standard, battle-tested Bitcoin curve library
- **GMP**: Well-audited arbitrary precision arithmetic
- **Metal Kernels**: Custom implementation (review appreciated!)

The Metal kernel implementations for modular arithmetic have been tested but should be considered **experimental**. Community review is welcome.

## License

All security-related contributions follow the same MIT License as the project.

