#ifndef PALLADIUM_CHAT_CRYPTO_H
#define PALLADIUM_CHAT_CRYPTO_H

#include <key.h>
#include <pubkey.h>
#include <vector>
#include <string>

class ChatCrypto {
public:
    static std::vector<unsigned char> GenerateSharedSecret(const CKey& myPrivKey, const CPubKey& theirPubKey);
    static std::vector<unsigned char> Encrypt(const std::string& msg, const std::vector<unsigned char>& secret);
    static std::string Decrypt(const std::vector<unsigned char>& cipher, const std::vector<unsigned char>& secret);
};

#endif // PALLADIUM_CHAT_CRYPTO_H
