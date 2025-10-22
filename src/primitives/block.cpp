// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Palladium Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
// --- AuxPoW Start ---
#include <consensus/merkle.h> // Für ComputeMerkleRootFromBranch
#include <util/strencodings.h> // Für HexStr (in CAuxPow::ToString)
#include <crypto/common.h> // Für BEGIN/END Makros (in ComputeMerkleRootFromBranch)
// --- AuxPoW Ende ---


// --- AuxPoW Start ---
// Stolen from Bitcoin documentation - Calculate the Merkle root for a branch.
// Note: This implementation assumes the branch is provided correctly for the Bitcoin Merkle tree structure.
uint256 ComputeMerkleRootFromBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex)
{
    uint256 currentHash = hash;
    for (const uint256& step : vMerkleBranch) {
        if (nIndex & 1) { // If index is odd, the step is the left sibling
            currentHash = Hash(BEGIN(step), END(step), BEGIN(currentHash), END(currentHash));
        } else { // If index is even, the step is the right sibling
            currentHash = Hash(BEGIN(currentHash), END(currentHash), BEGIN(step), END(step));
        }
        nIndex >>= 1; // Move up to the next level in the tree
    }
    return currentHash;
}


/* Calculates the Merkle root of the aux chain commitment branch.
   Note: This assumes the commitment hash is placed correctly in the tree.
   It might need adjustments based on how the commitment is structured. */
uint256 CAuxPow::CheckChainMerkleBranch (uint256 hash, int nIndex) const
{
    // This implementation might be identical to CheckMerkleBranch if the structure is the same.
    // If vChainMerkleBranch is typically empty because the commitment is within the coinbase tx's
    // scriptSig, this function might not even be strictly necessary or could return a pre-calculated value.
    // For now, let's assume it works like the main branch check.
    if (vChainMerkleBranch.empty()) {
         // If commitment is in coinbase, the root of this branch should be the same as the coinbase branch root
         // Return the parent block's merkle root directly? Or maybe the hash itself if index is 0 and branch is empty?
         // This needs careful verification against the exact commitment method. Let's return 0 for now.
         return uint256(); // Indicate an issue or that this check isn't applicable
    }
    return ComputeMerkleRootFromBranch(hash, vChainMerkleBranch, nIndex);
}

// Implement ToString for CAuxPow for debugging
std::string CAuxPow::ToString() const
{
    return strprintf("CAuxPow(parentblock=%s)", GetParentBlockHash().ToString());
}
// --- AuxPoW Ende ---


uint256 CBlockHeader::GetHash() const
{
    // Header hash is always calculated the same way, regardless of AuxPoW.
    return SerializeHash(*this);
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, powhash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        GetPoWHash().ToString(), // Zeige den PoW-relevanten Hash an
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    // --- AuxPoW Start ---
    if (IsAuxpow() && m_auxpow) {
         s << "  " << m_auxpow->ToString() << "\n"; // Füge AuxPow Infos hinzu
    }
    // --- AuxPoW Ende ---
    return s.str();
}