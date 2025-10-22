// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Palladium Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PALLADIUM_POW_H
#define PALLADIUM_POW_H

#include <consensus/params.h>
#include <stdint.h>
#include <vector> // Hinzugefügt für std::vector

class CBlockHeader;
class CBlockIndex;
class uint256;
// --- AuxPoW Start ---
class CBlock; // Forward declaration für CheckAuxPowProofOfWork
// --- AuxPoW Ende ---


unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);

// --- AuxPoW Start ---
/** Check whether the auxiliary proof-of-work satisfies the difficulty specified by nBits */
bool CheckAuxPowProofOfWork(const CBlock& block, const Consensus::Params& params);

// Deklaration für LwmaCalculateNextWorkRequired (bereits in deiner pow.cpp vorhanden)
unsigned int LwmaCalculateNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params&);
// Deklaration für ComputeMerkleRootFromBranch (in block.cpp hinzugefügt)
uint256 ComputeMerkleRootFromBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex);
// --- AuxPoW Ende ---

#endif // PALLADIUM_POW_H