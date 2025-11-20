#include <chat_crypto.h>
#include <key.h>
#include <pubkey.h>
#include <crypto/aes.h>
#include <crypto/sha256.h>
#include <random.h>
#include <secp256k1.h>
#include <secp256k1_ecdh.h>

// Helper to access the global secp256k1 context from key.cpp if available, 
// or we might need to create a local context. 
// For simplicity in this snippet, we assume we can use the static context or create one.
// Note: In Bitcoin Core, 'secp256k1_context_sign' is usually available.

std::vector<unsigned char> ChatCrypto::GenerateSharedSecret(const CKey& myPrivKey, const CPubKey& theirPubKey)
{
    if (!myPrivKey.IsValid() || !theirPubKey.IsValid()) {
        throw std::runtime_error("Invalid keys for shared secret generation");
    }

    // We need to use libsecp256k1 for ECDH
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    
    std::vector<unsigned char> secret(32);
    
    // Prepare public key
    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, theirPubKey.begin(), theirPubKey.size())) {
        secp256k1_context_destroy(ctx);
        throw std::runtime_error("Failed to parse public key");
    }

    // Perform ECDH
    // Note: myPrivKey.begin() returns pointer to the 32-byte private key
    if (!secp256k1_ecdh(ctx, secret.data(), &pubkey, myPrivKey.begin(), NULL, NULL)) {
        secp256k1_context_destroy(ctx);
        throw std::runtime_error("ECDH failed");
    }

    secp256k1_context_destroy(ctx);
    return secret;
}

std::vector<unsigned char> ChatCrypto::Encrypt(const std::string& msg, const std::vector<unsigned char>& secret)
{
    // Use AES-256-CBC
    // Key = SHA256(secret) to ensure it's 32 bytes and well distributed
    unsigned char key[32];
    CSHA256().Write(secret.data(), secret.size()).Finalize(key);

    // IV (Initialization Vector)
    unsigned char iv[AES_BLOCKSIZE];
    GetStrongRandBytes(iv, AES_BLOCKSIZE);

    AES256CBCEncrypt enc(key, iv, true);

    // Pad message to block size (PKCS7 padding is standard, but here we might just pad with zeros or use standard padding)
    // For simplicity, let's just pad with 0s to multiple of block size (not robust for all cases but simple for now)
    std::vector<unsigned char> data(msg.begin(), msg.end());
    size_t padding = AES_BLOCKSIZE - (data.size() % AES_BLOCKSIZE);
    data.insert(data.end(), padding, (unsigned char)padding); // PKCS7 padding

    std::vector<unsigned char> ciphertext;
    ciphertext.resize(AES_BLOCKSIZE + data.size()); // IV + Encrypted Data

    // Copy IV to beginning
    memcpy(ciphertext.data(), iv, AES_BLOCKSIZE);

    // Encrypt
    int outLen = enc.Encrypt(data.data(), data.size(), ciphertext.data() + AES_BLOCKSIZE);
    if (outLen != data.size()) {
         // Should match if padding is correct
    }

    return ciphertext;
}

std::string ChatCrypto::Decrypt(const std::vector<unsigned char>& cipher, const std::vector<unsigned char>& secret)
{
    if (cipher.size() < AES_BLOCKSIZE) return "";

    unsigned char key[32];
    CSHA256().Write(secret.data(), secret.size()).Finalize(key);

    unsigned char iv[AES_BLOCKSIZE];
    memcpy(iv, cipher.data(), AES_BLOCKSIZE);

    AES256CBCDecrypt dec(key, iv, true);

    std::vector<unsigned char> plaintext(cipher.size() - AES_BLOCKSIZE);
    int len = dec.Decrypt(cipher.data() + AES_BLOCKSIZE, cipher.size() - AES_BLOCKSIZE, plaintext.data());
    
    if (len == 0) return "";

    // Remove PKCS7 padding
    unsigned char padVal = plaintext.back();
    if (padVal > 0 && padVal <= AES_BLOCKSIZE) {
        // Check validity
        bool valid = true;
        for (size_t i = plaintext.size() - padVal; i < plaintext.size(); ++i) {
            if (plaintext[i] != padVal) {
                valid = false;
                break;
            }
        }
        if (valid) {
            plaintext.resize(plaintext.size() - padVal);
        }
    }

    return std::string(plaintext.begin(), plaintext.end());
}
