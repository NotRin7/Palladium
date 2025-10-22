// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Palladium Core developers // Jahr angepasst
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PALLADIUM_CONSENSUS_PARAMS_H // Guard angepasst
#define PALLADIUM_CONSENSUS_PARAMS_H

#include <uint256.h>
#include <limits>
#include <map>
#include <string>

namespace Consensus {

// --- Maximale Blockhöhe für Fork-Planung ---
static constexpr int MAX_BLOCK_HEIGHT = std::numeric_limits<int>::max();


enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits signalling. */
    int64_t nStartTime;
    /** Timeout MedianTime for version bits signalling. */
    int64_t nTimeout;

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     * This is useful for testing, regression testing and informational deployments. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Block height and hash at which BIP16 becomes active */
    // int BIP16Height; // Oft 0 in neueren Chains, P2SH immer aktiv? Prüfen!
    uint256 BIP16Exception; // Genesis hash if BIP16 active from start
    /** Block height at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash; // Not needed if height-based
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height. */
    int MinBIP9WarningHeight;
    /** BIP9 deployment parameters */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetSpacingV2; // Für LWMA / neue Blockzeit
    int64_t nPowTargetTimespan;
    // DifficultyAdjustmentInterval angepasst, um korrekten Wert nach LWMA-Aktivierung zu liefern
    int DifficultyAdjustmentInterval(int nHeight) const {
        // Prüfe, ob LWMA aktiv ist (Block >= 29000). Wenn ja, ist das klassische Intervall irrelevant.
        // Falls wir das Fenster für BIP9 brauchen, muss es anders berechnet werden.
        // Nehmen wir an, nMinerConfirmationWindow ist jetzt der relevante Wert für BIP9.
        // Die alte Berechnung ist nur für den alten Algo (< 29000) relevant.
        if (nHeight < 29000) { // Beispiel: Blockhöhe anpassen, falls LWMA anders aktiviert wurde
             return nPowTargetTimespan / nPowTargetSpacing;
        } else {
             // Für BIP9 wird nMinerConfirmationWindow verwendet.
             // Gib hier einen sinnvollen Wert zurück oder passe die Verwendung an.
             // Bitcoin Core verwendet hier oft einen festen Wert (z.B. 2016)
             // oder leitet ihn aus Timespan/Spacing ab.
             // Da LWMA das Timespan ignoriert, ist es sauberer, nMinerConfirmationWindow direkt zu verwenden.
             // return nMinerConfirmationWindow; // Oder die alte Berechnung, wenn sie noch irgendwo gebraucht wird
             return nPowTargetTimespan / nPowTargetSpacingV2; // Behalte Timespan/Spacing Logik bei
        }
    }
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;

    // --- AuxPoW Start ---
    /** Block height at which Auxiliary Proof of Work (AuxPoW) becomes active */
    int nAuxpowStartHeight;
    // --- AuxPoW Ende ---

    // Constructor to initialize members - WICHTIG: Alle neuen Member hier initialisieren!
    Params() : hashGenesisBlock(), nSubsidyHalvingInterval(0), /*BIP16Height(0),*/ BIP16Exception(),
               BIP34Height(0), BIP34Hash(), BIP65Height(0), BIP66Height(0), CSVHeight(0), SegwitHeight(0),
               MinBIP9WarningHeight(0), nRuleChangeActivationThreshold(0), nMinerConfirmationWindow(0),
               vDeployments{}, powLimit(), fPowAllowMinDifficultyBlocks(false), fPowNoRetargeting(false),
               nPowTargetSpacing(0), nPowTargetSpacingV2(0), nPowTargetTimespan(0),
               nMinimumChainWork(), defaultAssumeValid()
               // --- AuxPoW Start ---
               , nAuxpowStartHeight(MAX_BLOCK_HEIGHT) // Standardmäßig deaktiviert
               // --- AuxPoW Ende ---
    {}
};
} // namespace Consensus

#endif // PALLADIUM_CONSENSUS_PARAMS_H