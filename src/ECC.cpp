#include "ECC.hpp"
#include <stdexcept>
#include <cstring>

ECC::ECC() {
    ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
}

ECC::~ECC() {
    if (ctx) {
        secp256k1_context_destroy(ctx);
    }
}

bool ECC::parsePublicKey(secp256k1_pubkey& pubkey, const std::vector<unsigned char>& input) {
    return secp256k1_ec_pubkey_parse(ctx, &pubkey, input.data(), input.size()) == 1;
}

std::vector<unsigned char> ECC::serializePublicKey(const secp256k1_pubkey& pubkey, bool compressed) {
    size_t len = compressed ? 33 : 65;
    std::vector<unsigned char> output(len);
    unsigned int flags = compressed ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED;
    secp256k1_ec_pubkey_serialize(ctx, output.data(), &len, &pubkey, flags);
    output.resize(len); // Should match
    return output;
}

bool ECC::addPoints(secp256k1_pubkey& p, const secp256k1_pubkey& q) {
    const secp256k1_pubkey* pubkeys[2];
    pubkeys[0] = &p;
    pubkeys[1] = &q;
    // Result is stored in p (first argument acts as output in some APIs, but here combine writes to a separate output)
    // Wait, secp256k1_ec_pubkey_combine(ctx, out, ins, n)
    secp256k1_pubkey result;
    if (secp256k1_ec_pubkey_combine(ctx, &result, pubkeys, 2) == 1) {
        p = result;
        return true;
    }
    return false;
}

bool ECC::addScalar(secp256k1_pubkey& p, const unsigned char* scalar) {
    // tweak_add does in-place modification
    return secp256k1_ec_pubkey_tweak_add(ctx, &p, scalar) == 1;
}

bool ECC::getPubKeyFromPriv(secp256k1_pubkey& pubkey, const unsigned char* privKey) {
    return secp256k1_ec_pubkey_create(ctx, &pubkey, privKey) == 1;
}

