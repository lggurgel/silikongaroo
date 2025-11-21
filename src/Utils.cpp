#include "Utils.hpp"
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace Utils {

    std::vector<unsigned char> hexToBytes(const std::string& hex) {
        std::vector<unsigned char> bytes;
        for (unsigned int i = 0; i < hex.length(); i += 2) {
            std::string byteString = hex.substr(i, 2);
            unsigned char byte = (unsigned char)strtol(byteString.c_str(), NULL, 16);
            bytes.push_back(byte);
        }
        return bytes;
    }

    std::string bytesToHex(const std::vector<unsigned char>& bytes) {
        std::stringstream ss;
        for (unsigned char byte : bytes) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
        }
        return ss.str();
    }

    void mpzToBytes(const mpz_t num, unsigned char* bytes) {
        size_t count;
        mpz_export(bytes, &count, 1, 1, 1, 0, num);
        // Pad with zeros if necessary (to ensure 32 bytes)
        // mpz_export might write fewer bytes if the number is small
        // But we need to handle the offset.
        // A better way:
        // clear buffer
        std::memset(bytes, 0, 32);
        if (mpz_sgn(num) == 0) return;

        size_t size = (mpz_sizeinbase(num, 2) + 7) / 8;
        if (size > 32) size = 32; // Should not happen for valid keys

        // Export to a temporary buffer if alignment is tricky, but mpz_export handles it.
        // However, we want big endian, 32 bytes.
        // If export gives fewer bytes, they are the significant ones? No, mpz_export with endian=1 gives most significant first.
        // So if we have fewer than 32 bytes, we need to put them at the END of the buffer for big endian representation?
        // No, "123" (val) -> 0x00...007B.
        // If mpz_export returns "7B", we need to place it at bytes[31].

        // Actually, let's do it simply:
        std::vector<unsigned char> temp(32);
        size_t exported_size;
        mpz_export(temp.data(), &exported_size, 1, 1, 1, 0, num);

        // Copy to end of bytes buffer
        size_t offset = 32 - exported_size;
        std::memcpy(bytes + offset, temp.data(), exported_size);
    }

    void bytesToMpz(mpz_t num, const unsigned char* bytes, size_t len) {
        mpz_import(num, len, 1, 1, 1, 0, bytes);
    }

    void printProgressBar(double percentage, double jumpsPerSec) {
        int width = 50;
        int pos = width * percentage;
        std::cout << "\r[";
        for (int i = 0; i < width; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << int(percentage * 100.0) << "% "
                  << std::fixed << std::setprecision(2) << jumpsPerSec/1000000.0 << " M/s " << std::flush;
    }

} // namespace Utils

