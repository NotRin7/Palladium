// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Palladium Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PALLADIUM_PRIMITIVES_BLOCK_H
#define PALLADIUM_PRIMITIVES_BLOCK_H

#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>
#include <memory> // Für std::shared_ptr
#include <string> // Für std::string in ToString
#include <sstream> // Für std::stringstream in ToString
#include <vector> // Für std::vector

// --- AuxPoW Start ---
// Forward declaration
class CBlock;
class CBlockHeader; // Forward declaration hinzugefügt

// Hilfsfunktion zur Berechnung des Merkle Roots aus einem Branch (Deklaration)
uint256 ComputeMerkleRootFromBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex);

/** Header for merge-mining data.
 */
class CAuxPow
{
public:
    CTransactionRef coinbaseTx; // Bitcoin Coinbase Transaction
    uint256 hashBlock;          // Bitcoin Block Hash (Parent Block Hash) - Wird beim Serialisieren gesetzt/gelesen
    std::vector<uint256> vMerkleBranch; // Merkle branch connecting coinbaseTx to Merkle root
    int nIndex;                 // Index of coinbaseTx in parent block's Merkle tree

    // Merkle branch connecting aux block hash commitment to Merkle root.
    // This is used ONLY if the commitment is not in the coinbaseTx itself.
    // Often empty if commitment is in coinbaseTx's scriptSig.
    std::vector<uint256> vChainMerkleBranch;
    int nChainIndex;

    CBlockHeader parentBlock; // Bitcoin Block Header (Parent Block)

    CAuxPow()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(coinbaseTx);
        READWRITE(hashBlock); // Hash of Coinbase Tx needed for CheckMerkleBranch
        READWRITE(vMerkleBranch);
        READWRITE(nIndex);
        READWRITE(vChainMerkleBranch);
        READWRITE(nChainIndex);
        READWRITE(parentBlock);
        if (ser_action.ForRead()) {
             // Nach dem Lesen den korrekten Hash des Parent Blocks setzen
             // hashBlock in der serialisierten Form ist oft der Coinbase Hash, nicht der Block Hash
             // Wir verlassen uns auf GetParentBlockHash() für den korrekten PoW Hash
        }
    }

    void SetNull()
    {
        coinbaseTx.reset();
        hashBlock.SetNull(); // Dies sollte eher der Coinbase Tx Hash sein
        vMerkleBranch.clear();
        nIndex = 0;
        vChainMerkleBranch.clear();
        nChainIndex = 0;
        parentBlock.SetNull();
    }

    // Prüft den Merkle Branch für die Coinbase Tx zum Parent Block Merkle Root
    uint256 CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex) const
    {
        // Die Implementierung befindet sich jetzt in block.cpp
        return ComputeMerkleRootFromBranch(hash, vMerkleBranch, nIndex);
    }

    // Get the hash of the corresponding Bitcoin block. Auxiliary PoW uses this hash as the proof of work.
    uint256 GetParentBlockHash() const { return parentBlock.GetHash(); }

    // Calculate the Merkle root of the aux chain commitment branch.
    uint256 CheckChainMerkleBranch(uint256 hash, int nIndex) const;

    std::string ToString() const; // Deklaration hinzugefügt
};
// --- AuxPoW Ende ---


/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements. When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain. The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    // --- AuxPoW Start ---
    // Increment base version if other non-AuxPoW changes require it
    static const int32_t BASE_VERSION=7;
    // Define AuxPoW block version bit (Bit 8 = 256)
    static const int32_t AUXPOW_VERSION_BIT = (1 << 8);
    // --- AuxPoW Ende ---

    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CBlockHeader()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    void SetNull()
    {
        nVersion = CBlockHeader::BASE_VERSION; // Standardversion setzen (ohne AuxPoW Bit)
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    // --- AuxPoW Start ---
    // Check if the block version indicates AuxPoW.
    bool IsAuxpow() const
    {
        return nVersion & AUXPOW_VERSION_BIT;
    }
    // --- AuxPoW Ende ---

};


class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransactionRef> vtx;

    // --- AuxPoW Start ---
    // memory only - Optional pointer to AuxPoW data
    std::shared_ptr<CAuxPow> m_auxpow;
    // --- AuxPoW Ende ---

    // memory only
    mutable bool fChecked;

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *((CBlockHeader*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CBlockHeader, *this);
        // --- AuxPoW Start ---
        // Serialize AuxPoW data only if the version bit is set
        bool auxpowActive = IsAuxpow(); // Lese Version zuerst
        if (auxpowActive) {
            if (ser_action.ForRead()) {
                // Erstelle das Objekt nur, wenn gelesen wird und es noch nicht existiert
                if (!m_auxpow) {
                    m_auxpow = std::make_shared<CAuxPow>();
                }
            } else {
                 // Beim Schreiben: Stelle sicher, dass m_auxpow existiert, wenn das Bit gesetzt ist
                 // (Sollte durch die Erstellungslogik sichergestellt sein, aber zur Sicherheit)
                 if (!m_auxpow) {
                      // Dies sollte nicht passieren, wenn der Block korrekt erstellt wurde
                      // throw std::ios_base::failure("AuxPow flag set but AuxPow data missing for serialization");
                      // Alternativ: Erstelle ein leeres Objekt (führt aber zu ungültigen Daten)
                      m_auxpow = std::make_shared<CAuxPow>(); // Vorsicht hiermit
                 }
            }
            // Serialisiere das Objekt (nur wenn Bit gesetzt)
            READWRITE(*m_auxpow);

            if (ser_action.ForRead()) {
                 // Nach dem Lesen: Prüfe, ob das gelesene AuxPow-Objekt gültig ist
                 // (z.B. ob der Parent Block Hash nicht Null ist)
                 // Wenn nicht, setze m_auxpow zurück oder werfe Fehler
                 if (m_auxpow->GetParentBlockHash().IsNull()) {
                      // Potenziell ungültige AuxPow-Daten gelesen
                      // throw std::ios_base::failure("Invalid AuxPow data read");
                      m_auxpow.reset(); // Oder setze zurück
                 }
            }
        } else {
             // Wenn das AuxPow-Bit NICHT gesetzt ist
             if (ser_action.ForRead()) {
                 m_auxpow.reset(); // Stelle sicher, dass keine AuxPow-Daten gespeichert werden
             }
             // Beim Schreiben muss m_auxpow == nullptr sein (oder ignoriert werden)
        }
        // --- AuxPoW Ende ---
        READWRITE(vtx);
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        // --- AuxPoW Start ---
        m_auxpow.reset();
        // --- AuxPoW Ende ---
        fChecked = false;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        return block;
    }

    // --- AuxPoW Start ---
    // Return the Block Hash used for Proof of Work verification.
    // For normal blocks, it's the block's own hash.
    // For AuxPoW blocks, it's the hash of the parent Bitcoin block.
    uint256 GetPoWHash() const
    {
        if (IsAuxpow() && m_auxpow) {
            // Stelle sicher, dass m_auxpow initialisiert ist
            return m_auxpow->GetParentBlockHash();
        } else {
            return GetHash();
        }
    }
    // --- AuxPoW Ende ---

    std::string ToString() const;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    explicit CBlockLocator(const std::vector<uint256>& vHaveIn) : vHave(vHaveIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // PALLADIUM_PRIMITIVES_BLOCK_H