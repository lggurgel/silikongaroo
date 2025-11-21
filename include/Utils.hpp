#pragma once

#include <string>
#include <vector>
#include <gmp.h>
#include <iostream>
#include <iomanip>

namespace Utils {

    // Hex string to byte vector
    std::vector<unsigned char> hexToBytes(const std::string& hex);

    // Byte vector to hex string
    std::string bytesToHex(const std::vector<unsigned char>& bytes);

    // GMP mpz_t to 32-byte array (big endian)
    void mpzToBytes(const mpz_t num, unsigned char* bytes);

    // 32-byte array to GMP mpz_t (big endian)
    void bytesToMpz(mpz_t num, const unsigned char* bytes, size_t len = 32);

    // Print progress bar
    void printProgressBar(double percentage, double jumpsPerSec);

} // namespace Utils

