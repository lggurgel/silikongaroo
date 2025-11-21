#pragma once

#include <secp256k1.h>
#include <vector>
#include <string>
#include <gmp.h>

class ECC {
public:
    ECC();
    ~ECC();

    // Parse public key (compressed or uncompressed)
    bool parsePublicKey(secp256k1_pubkey& pubkey, const std::vector<unsigned char>& input);

    // Serialize public key
    std::vector<unsigned char> serializePublicKey(const secp256k1_pubkey& pubkey, bool compressed = true);

    // P = P + Q
    bool addPoints(secp256k1_pubkey& p, const secp256k1_pubkey& q);

    // P = P + scalar * G
    bool addScalar(secp256k1_pubkey& p, const unsigned char* scalar);

    // Create a point from a scalar (P = scalar * G)
    bool getPubKeyFromPriv(secp256k1_pubkey& pubkey, const unsigned char* privKey);

    secp256k1_context* getContext() { return ctx; }

private:
    secp256k1_context* ctx;
};

