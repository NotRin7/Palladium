// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Palladium Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/system.h> // Für error()

// --- AuxPoW Start ---
#include <util/strencodings.h> // Für ParseHex
#include <script/script.h>     // Für CScript
#include <logging.h>          // Für LogPrintf
#include <set>                // Für Duplicate Check
#include <sync.h>             // Für cs_main Lock (Duplicate Check)
#include <algorithm>          // Für std::search, std::reverse
// --- AuxPoW Ende ---


// --- AuxPoW Start ---
// Globales Set zur Verfolgung verwendeter Parent Block Hashes (Needs cs_main lock for access)
// Dies ist eine einfache Implementierung. Eine robustere könnte LRU-Caching o.ä. verwenden.
std::set<uint256> setAuxPowScannedParentHashes GUARDED_BY(cs_main);
// --- AuxPoW Ende ---

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // --- AuxPoW Start ---
    // Nach dem AuxPoW Fork wird die Difficulty immer noch angepasst, aber CheckProofOfWork wird anders aufgerufen.
    // Die Logik hier bleibt also dieselbe, um das nBits für den *nächsten* Block zu bestimmen.
    // --- AuxPoW Ende ---

    // reset difficulty for new diff algorithm's average + Segwit/CSV activation (Block 29000 ist LWMA Start)
    if ((pindexLast->nHeight >= 28930) && (pindexLast->nHeight <= 28999)) {
        LogPrintf("Difficulty reset to limit for LWMA activation window at height %d\n", pindexLast->nHeight + 1);
        return nProofOfWorkLimit;
    }

    // Ab Block 29000 wird LWMA verwendet
    // Hole die aktuelle Höhe + 1 für die Entscheidung, welcher DifficultyAlgo genutzt wird
    int nHeight = pindexLast->nHeight + 1;
    if (nHeight >= 29000) { // Nutze die korrekte Höhe für die Entscheidung
        // LogPrintf("Using LWMA for difficulty calculation at height %d\n", nHeight);
        return LwmaCalculateNextWorkRequired(pindexLast, params);
    }


    // Alter Bitcoin Difficulty Adjustment Algo (vor Block 29000)
    // Only change once per difficulty adjustment interval
    // Verwende die korrekte Höhe für die Intervallprüfung
    if (nHeight % params.DifficultyAdjustmentInterval(nHeight) != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet/regtest:
            // If the new block's timestamp is more than 2* target spacing
            // then allow mining of a min-difficulty block.
            // WICHTIG: Verwende nPowTargetSpacing hier, da dieser Code nur < Block 29000 läuft.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                // Passe die Intervallprüfung an die Höhe des jeweiligen pindex an
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval(pindex->nHeight) != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be nPowTargetTimespan worth of blocks
    // Verwende die korrekte Höhe für die Intervallberechnung
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval(nHeight)-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    LogPrintf("Using CalculateNextWorkRequired (old algo) at height %d\n", nHeight);
    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

// LwmaCalculateNextWorkRequired bleibt unverändert (verwendet T = params.nPowTargetSpacingV2 korrekt)
unsigned int LwmaCalculateNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    const int64_t T = params.nPowTargetSpacingV2; // Korrekt: Verwendet V2 (120 Sek)
    const int64_t N = 240; // LWMA Fenstergröße (Anzahl Blöcke)
    // Original LWMA k calculation: T * N * (N + 1) / 2
    // Hier ist k = N * (N + 1) * T / 2; Das ist mathematisch dasselbe.
    const int64_t k = N * (N + 1) * T / 2;
    const int64_t height = pindexLast->nHeight;
    const arith_uint256 powLimit = UintToArith256(params.powLimit);

    // Genesis Block Case
    if (pindexLast == nullptr || height == 0) {
        //LogPrintf("LWMA: Genesis block or pindexLast is null, returning powLimit\n");
        return powLimit.GetCompact();
    }
    // Handle initial blocks where N previous blocks are not available
    if (height < N) {
        //LogPrintf("LWMA: Height %d < N (%d), returning powLimit\n", height, N);
        return powLimit.GetCompact();
    }

    arith_uint256 sumTarget, nextTarget;
    int64_t thisTimestamp, previousTimestamp;
    int64_t t = 0, j = 0;

    // Finde den Startblock für das LWMA Fenster
    const CBlockIndex* blockStart = pindexLast->GetAncestor(height - N);
    if (!blockStart) {
         // Sollte nicht passieren, wenn height >= N
         LogPrintf("ERROR: LWMA: Could not find ancestor at height %d\n", height - N);
         return powLimit.GetCompact();
    }
    previousTimestamp = blockStart->GetBlockTime();


    // Loop through N most recent blocks (von height - N + 1 bis height).
    const CBlockIndex* blockCurrent = blockStart;
    for (int64_t i = height - N + 1; i <= height; i++) {
        // Navigiere zum nächsten Block im Hauptzweig. GetAncestor kann teuer sein in Schleife.
        // Es ist effizienter, von pindexLast rückwärts zu gehen oder von blockStart vorwärts.
        blockCurrent = pindexLast->GetAncestor(i); // Nutze GetAncestor für Korrektheit bei Reorgs
        if (!blockCurrent) {
             LogPrintf("ERROR: LWMA: Could not find block at height %d using GetAncestor\n", i);
             return powLimit.GetCompact(); // Oder einen anderen Fehler-Fallback
        }

        thisTimestamp = blockCurrent->GetBlockTime();

        // Ensure timestamps are monotonic within the calculation window for sanity
        if(thisTimestamp < previousTimestamp) {
            //LogPrintf("LWMA Warning: Non-monotonic timestamp detected at height %d (%ld < %ld)\n", i, thisTimestamp, previousTimestamp);
            thisTimestamp = previousTimestamp; // Treat as 0 solvetime if non-monotonic, LWMA does prev+1
        }

        // Clamp solvetime: min 1, max 6*T
        int64_t solvetime = std::max((int64_t)1, thisTimestamp - previousTimestamp); // Min solvetime is 1
        solvetime = std::min(6 * T, solvetime);

        previousTimestamp = thisTimestamp;

        j++;
        t += solvetime * j; // Weighted solvetime sum.
        arith_uint256 target;
        target.SetCompact(blockCurrent->nBits);
        sumTarget += target; // Summe der Targets (NICHT geteilt!)
    }

    // Berechne das nächste Target mit korrekter LWMA-Formel
    if (N == 0 || T == 0 || k == 0) { // Division durch Null verhindern
         LogPrintf("ERROR: LWMA: N, T or k is zero (N=%ld, T=%ld, k=%ld)\n", N, T, k);
         return powLimit.GetCompact();
    }
    arith_uint256 avgTarget = sumTarget / N; // Durchschnittliches Target über N Blöcke
    nextTarget = avgTarget * t / (k * T); // KORREKTE LWMA Formel

    if (nextTarget > powLimit) {
        //LogPrintf("LWMA: Calculated target > powLimit, clamping to powLimit\n");
        nextTarget = powLimit;
    }

    return nextTarget.GetCompact();
}

// CalculateNextWorkRequired bleibt unverändert (nur für < Block 29000 relevant)
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    LogPrintf("CalculateNextWorkRequired: nActualTimespan = %d before bounds\n", nActualTimespan);
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;
    LogPrintf("CalculateNextWorkRequired: nActualTimespan = %d after bounds (Target: %d)\n", nActualTimespan, params.nPowTargetTimespan);


    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    LogPrintf("Difficulty Retarget - Old: %08x %s\n", pindexLast->nBits, arith_uint256().SetCompact(pindexLast->nBits).ToString());
    LogPrintf("Difficulty Retarget - New: %08x %s\n", bnNew.GetCompact(), bnNew.ToString());


    return bnNew.GetCompact();
}

// --- AuxPoW Start ---
// *** NEUE EINZIGARTIGE MAGIC BYTES *** (Hex für "plm" + 0x01)
const unsigned char pchAuxPowHeader[] = {0x70, 0x6c, 0x6d, 0x01};

/** Check whether the auxiliary proof-of-work satisfies the difficulty specified by nBits */
bool CheckAuxPowProofOfWork(const CBlock& block, const Consensus::Params& params)
{
    // Check if block has AuxPoW data and the version bit is set
    if (!block.IsAuxpow()) {
        // This case should ideally be caught by CheckBlock/ContextualCheckBlockHeader
        return error("%s: Block does not have AuxPoW version bit set", __func__);
    }
    if (!block.m_auxpow) {
        return error("%s: No AuxPoW data present in AuxPoW block", __func__);
    }
    LogPrintf("CheckAuxPowProofOfWork: AuxPoW data found for block %s\n", block.GetHash().ToString());

    // Check if the parent block hash satisfies the target difficulty
    if (!CheckProofOfWork(block.m_auxpow->GetParentBlockHash(), block.nBits, params)) {
        // Log more details for debugging
        arith_uint256 bnTarget;
        bnTarget.SetCompact(block.nBits);
        LogPrintf("CheckAuxPowProofOfWork failed: Parent Block Hash %s does not meet target %s (nBits %08x)\n",
                  block.m_auxpow->GetParentBlockHash().ToString(), bnTarget.ToString(), block.nBits);
        return error("%s: Bitcoin parent block PoW does not meet target", __func__);
    }
     LogPrintf("CheckAuxPowProofOfWork: Parent block hash %s meets target %08x\n", block.m_auxpow->GetParentBlockHash().ToString(), block.nBits);


    // Check if the Coinbase transaction is correctly linked to the parent block's Merkle root
    uint256 hashCoinbase = block.m_auxpow->coinbaseTx->GetHash();
    uint256 calculatedMerkleRoot = block.m_auxpow->CheckMerkleBranch(hashCoinbase, block.m_auxpow->vMerkleBranch, block.m_auxpow->nIndex);
    if (block.m_auxpow->parentBlock.hashMerkleRoot != calculatedMerkleRoot) {
         LogPrintf("CheckAuxPowProofOfWork failed: Merkle branch for Coinbase Tx verification failed.\n");
         LogPrintf("  Coinbase Hash: %s\n", hashCoinbase.ToString());
         LogPrintf("  Parent Merkle Root: %s\n", block.m_auxpow->parentBlock.hashMerkleRoot.ToString());
         LogPrintf("  Calculated Merkle Root: %s\n", calculatedMerkleRoot.ToString());
         LogPrintf("  Index: %d\n", block.m_auxpow->nIndex);
         LogPrintf("  Branch size: %d\n", block.m_auxpow->vMerkleBranch.size());
        return error("%s: Merkle branch for Coinbase Tx verification failed", __func__);
    }
     LogPrintf("CheckAuxPowProofOfWork: Coinbase Merkle branch verified.\n");


    // Find the AuxPoW commitment in the Coinbase transaction's scriptSig
    // It should be after the magic bytes pchAuxPowHeader
    const CScript& scriptSig = block.m_auxpow->coinbaseTx->vin[0].scriptSig;
    CScript::const_iterator pc = std::search(scriptSig.begin(), scriptSig.end(), pchAuxPowHeader, pchAuxPowHeader + sizeof(pchAuxPowHeader));

    if (pc == scriptSig.end()) {
        LogPrintf("CheckAuxPowProofOfWork failed: AuxPoW magic bytes not found in Coinbase scriptSig.\n");
        LogPrintf("  ScriptSig: %s\n", HexStr(scriptSig));
        return error("%s: AuxPoW magic bytes (%s) not found in Coinbase scriptSig", __func__, HexStr(pchAuxPowHeader, pchAuxPowHeader + sizeof(pchAuxPowHeader)));
    }
    LogPrintf("CheckAuxPowProofOfWork: Magic bytes found in scriptSig.\n");


    // Commitment structure: [Magic Bytes] [Palladium Block Hash (reversed)] [Merkle Tree Size] [Merkle Nonce]
    // Extract the Palladium block hash (committed hash) - it's typically stored reversed in the scriptSig
    std::vector<unsigned char> vchCommitment(pc + sizeof(pchAuxPowHeader), scriptSig.end());
    if (vchCommitment.size() < sizeof(uint256)) { // Ensure there's enough data for the hash
         LogPrintf("CheckAuxPowProofOfWork failed: Commitment data too short (%d bytes) in Coinbase scriptSig.\n", vchCommitment.size());
         return error("%s: Commitment data too short in Coinbase scriptSig", __func__);
    }
    uint256 hashAuxBlockCommit = uint256(std::vector<unsigned char>(vchCommitment.begin(), vchCommitment.begin() + sizeof(uint256)));
    // Reverse the hash bytes (standard AuxPoW practice)
    std::reverse(hashAuxBlockCommit.begin(), hashAuxBlockCommit.end());
     LogPrintf("CheckAuxPowProofOfWork: Found commitment hash: %s\n", hashAuxBlockCommit.ToString());


    // Calculate the expected Palladium block hash (without AuxPoW data)
    // We need the hash of the block *as if* it wasn't an AuxPoW block.
    // This typically means hashing the header *without* the AuxPoW version bit.
    CBlockHeader headerNoAux = block.GetBlockHeader();
    headerNoAux.nVersion &= ~CBlockHeader::AUXPOW_VERSION_BIT; // Remove AuxPoW flag for hashing
    uint256 hashAuxBlockExpected = headerNoAux.GetHash();
     LogPrintf("CheckAuxPowProofOfWork: Expected block hash: %s\n", hashAuxBlockExpected.ToString());


    // Verify the commitment
    if (hashAuxBlockCommit != hashAuxBlockExpected) {
        LogPrintf("CheckAuxPowProofOfWork failed: AuxPoW commitment mismatch.\n");
        LogPrintf("  Hash in scriptSig (reversed): %s\n", hashAuxBlockCommit.ToString());
        LogPrintf("  Expected block hash (version bit removed): %s\n", hashAuxBlockExpected.ToString());
        return error("%s: AuxPoW commitment mismatch: scriptSig %s vs expected %s", __func__,
                     hashAuxBlockCommit.ToString(), hashAuxBlockExpected.ToString());
    }
    LogPrintf("CheckAuxPowProofOfWork: Commitment hash verified.\n");


    // Optional: If using vChainMerkleBranch, verify it here.

    // Check against reusing the same parent block PoW for multiple aux blocks.
    {
        LOCK(cs_main); // Lock cs_main to access the global set
        if (setAuxPowScannedParentHashes.count(block.m_auxpow->GetParentBlockHash())) {
            LogPrintf("CheckAuxPowProofOfWork failed: Duplicate AuxPoW parent block hash %s\n", block.m_auxpow->GetParentBlockHash().ToString());
            return error("%s: duplicate proof-of-work parent block hash", __func__);
        }
        // Add hash to the set in validation.cpp ConnectBlock/ActivateBestChain
    }
     LogPrintf("CheckAuxPowProofOfWork: Parent block hash %s is not a duplicate (as far as checked here).\n", block.m_auxpow->GetParentBlockHash().ToString());


    LogPrintf("CheckAuxPowProofOfWork: Block %s PASSED\n", block.GetHash().ToString());
    return true;
}
// --- AuxPoW Ende ---

// Behalte die originale CheckProofOfWork für Blöcke vor dem Hard Fork und für die AuxPoW-Parent-Block-Prüfung
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return error("CheckProofOfWork(): nBits (%08x) below minimum work (%s) or invalid", nBits, UintToArith256(params.powLimit).ToString());

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return error("CheckProofOfWork(): hash %s doesn't match nBits target %s", hash.ToString(), bnTarget.ToString());

    return true;
}