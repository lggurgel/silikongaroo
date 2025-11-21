#include <iostream>
#include <string>
#include <vector>
#include <gmp.h>
#include <secp256k1.h>
#include <random>
#include <ctime>
#include "../include/ECC.hpp"
#include "../include/Utils.hpp"

// Link against existing ECC.cpp and Utils.cpp
// We will compile this manually.

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: gen_key <private_key_hex>" << std::endl;
        return 1;
    }

    std::string privHex = argv[1];
    // Remove 0x
    if (privHex.substr(0, 2) == "0x") privHex = privHex.substr(2);

    mpz_t priv;
    mpz_init_set_str(priv, privHex.c_str(), 16);

    unsigned char privBytes[32];
    // Use our Util function (copy implementation or link)
    // For simplicity, let's just do it inline or assume we link Utils.o

    // We'll just rely on linking.
    Utils::mpzToBytes(priv, privBytes);

    ECC ecc;
    secp256k1_pubkey pub;
    if (ecc.getPubKeyFromPriv(pub, privBytes)) {
        std::vector<unsigned char> pubBytes = ecc.serializePublicKey(pub, true);
        std::string pubHex = Utils::bytesToHex(pubBytes);
        std::cout << "Private: " << privHex << std::endl;
        std::cout << "Public:  " << pubHex << std::endl;
    } else {
        std::cerr << "Invalid private key" << std::endl;
    }

    mpz_clear(priv);
    return 0;
}

