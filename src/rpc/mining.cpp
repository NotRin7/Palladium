// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Palladium Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <key_io.h>
#include <miner.h>
#include <net.h>
#include <node/context.h>
#include <policy/fees.h>
#include <pow.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <shutdown.h>
#include <txmempool.h>
#include <univalue.h>
#include <util/fees.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/system.h>
#include <validation.h> // Für ::ChainActive(), cs_main, mapBlockIndex
#include <validationinterface.h>
#include <versionbitsinfo.h>
#include <warnings.h>

#include <memory>
#include <stdint.h>

#include <sync.h>        // Für cs_main Lock
// --- AuxPoW Start ---
// #include <pow.h> // Ist bereits oben inkludiert
#include <primitives/block.h> // Für CBlockHeader::AUXPOW_VERSION_BIT und CBlock
// --- AuxPoW Ende ---


// --- AuxPoW Start ---
// Magic Bytes for AuxPoW block hash commitment (aus pow.cpp)
// Deklariere als extern, da in pow.cpp definiert
extern const unsigned char pchAuxPowHeader[];
// *** NEUE EINZIGARTIGE CHAIN ID *** (Hex für "plm" + 0x01 als Integer)
const int32_t PALLADIUM_AUXPOW_CHAIN_ID = 0x706C6D01;
// --- AuxPoW Ende ---

namespace { // Begin anon namespace
/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or from the last difficulty change if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
UniValue GetNetworkHashPS(int lookup, int height) {
    CBlockIndex *pb = ::ChainActive().Tip();

    if (height >= 0 && height < ::ChainActive().Height())
        pb = ::ChainActive()[height];

    if (pb == nullptr || !pb->nHeight)
        return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    // Pass current height to DifficultyAdjustmentInterval
    if (lookup <= 0)
        lookup = pb->nHeight % Params().GetConsensus().DifficultyAdjustmentInterval(pb->nHeight + 1) + 1;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex *pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    arith_uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return workDiff.getdouble() / timeDiff;
}
} // End anon namespace

static UniValue getnetworkhashps(const JSONRPCRequest& request)
{
            RPCHelpMan{"getnetworkhashps",
                "\nReturns the estimated network hashes per second based on the last n blocks.\n"
                "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change.\n"
                "Pass in [height] to estimate the network speed at the time when a certain block was found.\n",
                {
                    {"nblocks", RPCArg::Type::NUM, /* default */ "48", "The number of blocks, or -1 for blocks since last difficulty change."}, // Default geändert von 120 auf 48 (24h * 60min / 2min * 2 ?) -> Check logic
                    {"height", RPCArg::Type::NUM, /* default */ "-1", "To estimate at the time of the given height."},
                },
                RPCResult{
                    RPCResult::Type::NUM, "", "Hashes per second estimated"},
                RPCExamples{
                    HelpExampleCli("getnetworkhashps", "")
            + HelpExampleRpc("getnetworkhashps", "")
                },
            }.Check(request);

    LOCK(cs_main);
    // Passe Default-Wert hier an, falls nötig (z.B. auf 720 für 24 Stunden bei 2 Min Blöcken)
    return GetNetworkHashPS(!request.params[0].isNull() ? request.params[0].get_int() : 720, !request.params[1].isNull() ? request.params[1].get_int() : -1);
}

// Internal function called by generatetoaddress and generatetodescriptor
// WARNING: THIS FUNCTION MINES VIA CPU - DO NOT USE ON MAINNET UNLESS REGTEST/TESTNET
//          IT IS EXTREMELY INEFFICIENT AND CANNOT GENERATE AUXPOW BLOCKS!
static UniValue generateBlocks(const CTxMemPool& mempool, const CScript& coinbase_script, int nGenerate, uint64_t nMaxTries)
{
    int nHeightEnd = 0;
    int nHeight = 0;

    {   // Don't keep cs_main locked
        LOCK(cs_main);
        nHeight = ::ChainActive().Height();
        nHeightEnd = nHeight+nGenerate;
    }
    unsigned int nExtraNonce = 0;
    UniValue blockHashes(UniValue::VARR);
    const CChainParams& chainparams = Params();
    const Consensus::Params& consensusParams = chainparams.GetConsensus();

    while (nHeight < nHeightEnd && !ShutdownRequested())
    {
        // --- AuxPoW Check ---
        bool fAuxPowActive = false;
        {
             LOCK(cs_main);
             fAuxPowActive = (::ChainActive().Height() + 1 >= consensusParams.nAuxpowStartHeight);
        }
        if (fAuxPowActive && chainparams.NetworkIDString() != CBaseChainParams::REGTEST) {
             // Verhindere CPU-Mining mit generate* RPCs, wenn AuxPoW aktiv ist (außer Regtest)
             throw JSONRPCError(RPC_MISC_ERROR, "Cannot generate blocks via RPC when AuxPoW is active on this network. Use an external miner.");
        }
        // --- AuxPoW Check Ende ---

        std::unique_ptr<CBlockTemplate> pblocktemplate;
        try {
            pblocktemplate = BlockAssembler(mempool, chainparams).CreateNewBlock(coinbase_script);
        } catch (const std::exception& e) {
             LogPrintf("GenerateBlocks: Error creating block template: %s\n", e.what());
             throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not create block template");
        }
        if (!pblocktemplate.get())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new block");
        CBlock *pblock = &pblocktemplate->block;
        {
            LOCK(cs_main);
            IncrementExtraNonce(pblock, ::ChainActive().Tip(), nExtraNonce);
        }

        // Standard PoW Loop (nur relevant wenn AuxPoW nicht aktiv ist)
        while (nMaxTries > 0 && pblock->nNonce < std::numeric_limits<uint32_t>::max() && !ShutdownRequested()) {
            if (CheckProofOfWork(pblock->GetHash(), pblock->nBits, consensusParams)) {
                 break; // Found a solution
            }
            ++pblock->nNonce;
            --nMaxTries;
        }

        if (nMaxTries == 0 || ShutdownRequested()) {
            break;
        }
        if (pblock->nNonce == std::numeric_limits<uint32_t>::max()) {
            // Update time and try again if nonce wrapped around
             UpdateTime(pblock, consensusParams, ::ChainActive().Tip());
            continue;
        }

        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        bool new_block = false; // Wird von ProcessNewBlock gesetzt
        if (!ProcessNewBlock(chainparams, shared_pblock, true, &new_block)) {
             LogPrintf("GenerateBlocks: ProcessNewBlock failed for block %s\n", pblock->GetHash().ToString());
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");
        }
        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());
    }
    return blockHashes;
}


static UniValue generatetodescriptor(const JSONRPCRequest& request)
{
    RPCHelpMan{
        "generatetodescriptor",
        "\nMine blocks immediately to a specified descriptor (before the RPC call returns)\n"
        "Note: This command can only be used on regtest networks. Use an external miner for mainnet/testnet.\n", // Warnung hinzugefügt
        {
            {"num_blocks", RPCArg::Type::NUM, RPCArg::Optional::NO, "How many blocks are generated immediately."},
            {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO, "The descriptor to send the newly generated palladium to."},
            {"maxtries", RPCArg::Type::NUM, /* default */ "1000000", "How many iterations to try."},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "hashes of blocks generated",
            {
                {RPCResult::Type::STR_HEX, "", "blockhash"},
            }
        },
        RPCExamples{
            "\nGenerate 11 blocks to mydesc\n" + HelpExampleCli("generatetodescriptor", "11 \"mydesc\"")},
    }
        .Check(request);

    // --- AuxPoW Check ---
    if (Params().NetworkIDString() != CBaseChainParams::REGTEST) {
        throw JSONRPCError(RPC_MISC_ERROR, "generatetodescriptor can only be used on regtest.");
    }
    // --- AuxPoW Check Ende ---

    const int num_blocks{request.params[0].get_int()};
    const uint64_t max_tries{request.params[2].isNull() ? 1000000 : request.params[2].get_int64()}; // Verwende get_int64

    FlatSigningProvider key_provider;
    std::string error;
    const auto desc = Parse(request.params[1].get_str(), key_provider, error, /* require_checksum = */ false);
    if (!desc) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
    }
    if (desc->IsRange()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Ranged descriptor not accepted. Maybe pass through deriveaddresses first?");
    }

    // This Wallet context part might need adaptation depending on how wallet access is handled
    // Assuming GetScriptPubKeyManFromDescriptor is available or adapt accordingly.
    /*
    ScriptPubKeyMan* spk_man = GetScriptPubKeyManFromDescriptor(*desc, key_provider, false); // internal = false
    if (!spk_man) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Descriptor %s does not have keys", desc->ToString()));
    }
    const CScript script = spk_man->GetScript(); // Simplified: GetScript might need more context
    */
    // Alternative: Directly expand the descriptor to get scripts
    FlatSigningProvider provider;
    std::vector<CScript> coinbase_scripts;
    if (!desc->Expand(0, key_provider, coinbase_scripts, provider)) {
       throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Cannot derive script without private keys for descriptor"));
    }
    CHECK_NONFATAL(coinbase_scripts.size() == 1); // Ensure only one script is derived
    const CScript& script = coinbase_scripts[0];

    const CTxMemPool& mempool = EnsureMemPool(); // Use EnsureMemPool to get reference

    return generateBlocks(mempool, script, num_blocks, max_tries);
}

static UniValue generatetoaddress(const JSONRPCRequest& request)
{
            RPCHelpMan{"generatetoaddress",
                "\nMine blocks immediately to a specified address (before the RPC call returns)\n"
                 "Note: This command can only be used on regtest networks. Use an external miner for mainnet/testnet.\n", // Warnung hinzugefügt
                {
                    {"nblocks", RPCArg::Type::NUM, RPCArg::Optional::NO, "How many blocks are generated immediately."},
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to send the newly generated palladium to."},
                    {"maxtries", RPCArg::Type::NUM, /* default */ "1000000", "How many iterations to try."},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "hashes of blocks generated",
                    {
                        {RPCResult::Type::STR_HEX, "", "blockhash"},
                    }},
                RPCExamples{
            "\nGenerate 11 blocks to myaddress\n"
            + HelpExampleCli("generatetoaddress", "11 \"myaddress\"")
            + "If you are running the palladium core wallet, you can get a new address to send the newly generated palladium to with:\n"
            + HelpExampleCli("getnewaddress", "")
                },
            }.Check(request);

    // --- AuxPoW Check ---
    if (Params().NetworkIDString() != CBaseChainParams::REGTEST) {
        throw JSONRPCError(RPC_MISC_ERROR, "generatetoaddress can only be used on regtest.");
    }
    // --- AuxPoW Check Ende ---


    int nGenerate = request.params[0].get_int();
    uint64_t nMaxTries = 1000000;
    if (!request.params[2].isNull()) {
        nMaxTries = request.params[2].get_int64(); // Verwende get_int64
    }

    CTxDestination destination = DecodeDestination(request.params[1].get_str());
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");
    }

    const CTxMemPool& mempool = EnsureMemPool(); // Use EnsureMemPool

    CScript coinbase_script = GetScriptForDestination(destination);

    return generateBlocks(mempool, coinbase_script, nGenerate, nMaxTries);
}

static UniValue getmininginfo(const JSONRPCRequest& request)
{
            RPCHelpMan{"getmininginfo",
                "\nReturns a json object containing mining-related information.",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::NUM, "blocks", "The current block"},
                        {RPCResult::Type::NUM, "currentblockweight", /* optional */ true, "The block weight of the last assembled block (only present if a block was ever assembled)"},
                        {RPCResult::Type::NUM, "currentblocktx", /* optional */ true, "The number of block transactions of the last assembled block (only present if a block was ever assembled)"},
                        {RPCResult::Type::NUM, "difficulty", "The current difficulty"},
                        {RPCResult::Type::NUM, "networkhashps", "The network hashes per second"},
                        {RPCResult::Type::NUM, "pooledtx", "The size of the mempool"},
                        {RPCResult::Type::STR, "chain", "current network name (main, test, regtest)"},
                        {RPCResult::Type::STR, "warnings", "any network and blockchain warnings"},
                    }},
                RPCExamples{
                    HelpExampleCli("getmininginfo", "")
            + HelpExampleRpc("getmininginfo", "")
                },
            }.Check(request);


    LOCK(cs_main);
    const CTxMemPool& mempool = EnsureMemPool(); // Use EnsureMemPool
    const CChainParams& chainparams = Params();

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks",           (int)::ChainActive().Height());
    // Referenziere die statischen Member korrekt (abhängig von deiner BlockAssembler Implementierung)
    if (BlockAssembler::m_last_block_weight) obj.pushKV("currentblockweight", *BlockAssembler::m_last_block_weight);
    if (BlockAssembler::m_last_block_num_txs) obj.pushKV("currentblocktx", *BlockAssembler::m_last_block_num_txs);
    obj.pushKV("difficulty",       (double)GetDifficulty(::ChainActive().Tip()));
    // Pass a copy of the request to getnetworkhashps if needed, or adjust getnetworkhashps to not need it.
    // Making a dummy request for getnetworkhashps call.
    JSONRPCRequest netHashRequest = request; // Copy request for sub-call context if needed
    netHashRequest.params = UniValue(UniValue::VARR); // Clear params for default behavior
    netHashRequest.params.append(720); // Default blocks (e.g., 24h)
    netHashRequest.params.append(-1);  // Default height
    obj.pushKV("networkhashps",    getnetworkhashps(netHashRequest)); // Use modified request
    obj.pushKV("pooledtx",         (uint64_t)mempool.size());
    obj.pushKV("chain",            chainparams.NetworkIDString());
    obj.pushKV("warnings",         GetWarnings(false).original); // Assuming GetWarnings exists and returns struct/object with .original
    return obj;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const BlockValidationState& state) // Verwende BlockValidationState
{
    if (state.IsValid())
        return NullUniValue;

    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, state.ToString());
    if (state.IsInvalid())
    {
        std::string strRejectReason = state.GetRejectReason();
        if (strRejectReason.empty())
            return "rejected"; // Default rejection string
        // Map specific results to BIP22 strings
        if (state.GetResult() == BlockValidationResult::BLOCK_HEADER_DUPLICATE || state.GetResult() == BlockValidationResult::BLOCK_INVALID_HEADER) {
            // Check reason string for specifics
            if (strRejectReason == "duplicate") return "duplicate";
            if (strRejectReason == "duplicate-invalid") return "duplicate-invalid";
        }
        if (state.GetResult() == BlockValidationResult::BLOCK_INVALID_PREV) return "inconclusive-not-best-prevblk"; // Example
        // Add more mappings if needed based on BlockValidationResult enums and reason strings

        // Fallback to reason string or general 'rejected'
        return strRejectReason; // Return reason string if available
    }
    // Should be impossible?
    return "valid?"; // Should not happen
}

/* RBFStatus Logic adapted from Bitcoin Core - adjust based on your mempool implementation */
// Assuming RBFTransactionState enum and IsRBFOptIn are defined elsewhere (e.g., policy/rbf.h/cpp or util/rbf.h/cpp)
// RBFTransactionState IsRBFOptIn(const CTransaction& tx, const CTxMemPool& pool) EXCLUSIVE_LOCKS_REQUIRED(pool.cs);

static UniValue prioritisetransaction(const JSONRPCRequest& request)
{
            RPCHelpMan{"prioritisetransaction",
                "\nAccepts the transaction into mined blocks at a higher (or lower) priority\n",
                {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id."},
                    {"dummy", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "API-Compatibility for previous API. Must be zero or null.\n"
             "                  DEPRECATED. For forward compatibility use named arguments and omit this parameter."},
                    {"fee_delta", RPCArg::Type::NUM, RPCArg::Optional::NO, "The fee value (in satoshis) to add (or subtract, if negative).\n"
             "                  Note, that this value is not a fee rate. It is a value to modify absolute fee of the TX.\n"
             "                  The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
             "                  considers the transaction as it would have paid a higher (or lower) fee."},
                },
                RPCResult{
                    RPCResult::Type::BOOL, "", "Returns true"},
                RPCExamples{
                    HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000")
            + HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000")
                },
            }.Check(request);

    // LOCK(cs_main); // Lock cs_main first if mempool requires it
    LOCK(EnsureMemPool().cs); // Lock mempool directly

    uint256 hash(ParseHashV(request.params[0], "txid"));
    CAmount nAmount = request.params[2].get_int64();

    if (!(request.params[1].isNull() || request.params[1].get_real() == 0)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Priority is no longer supported, dummy argument to prioritisetransaction must be 0.");
    }

    EnsureMemPool().PrioritiseTransaction(hash, nAmount); // Use EnsureMemPool()
    return true;
}


// Helper functions for getblocktemplate proposal mode
// Adaptations from Bitcoin Core - ensure types/methods match your codebase
namespace { // Begin anon namespace
struct GBTValidity {
    std::shared_ptr<CBlock> pblock;
    CBlockIndex* pindexPrev = nullptr; // Initialize to nullptr
    BlockValidationState state; // Use BlockValidationState
};

// Simple handler for proposals (does basic checks)
GBTValidity CheckBlockTemplateProposal(const UniValue& data, const CChainParams& chainparams) {
    GBTValidity ret;
    ret.pblock = std::make_shared<CBlock>();
    CBlock& block = *ret.pblock;

    if (!DecodeHexBlk(block, data.get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    uint256 hash = block.GetHash();
    LOCK(cs_main);
    // Use LookupBlockIndex directly
    const CBlockIndex* pindex = LookupBlockIndex(hash);
    if (pindex) {
        if (pindex->IsValid(BLOCK_VALID_SCRIPTS)) {
             ret.state.Invalid(BlockValidationResult::BLOCK_HEADER_DUPLICATE, "duplicate", "duplicate");
             return ret;
        }
        if (pindex->nStatus & BLOCK_FAILED_MASK) {
             ret.state.Invalid(BlockValidationResult::BLOCK_HEADER_DUPLICATE, "duplicate-invalid", "duplicate-invalid");
             return ret;
        }
    }

    ret.pindexPrev = ::ChainActive().Tip();
    // Only do basic validation package checks (context-free)
    // Pass non-const pindexPrev
    CBlockIndex* pindexPrevMut = ::ChainActive().Tip(); // Get mutable pointer if needed
    if (!TestBlockValidity(ret.state, chainparams, block, pindexPrevMut, /*fCheckPOW=*/false, /*fCheckMerkleRoot=*/true)) {
         LogPrintf("CheckBlockTemplateProposal: TestBlockValidity failed: %s\n", ret.state.ToString());
         return ret;
    }

    return ret;
}

// Helper functions copied from rpc/blockchain.cpp for GBT response generation
// Ensure these functions exist or adapt them.

// Simplified TransactionsToJson - adapt based on actual fields needed by GBT
UniValue TransactionsToJson(const std::vector<CAmount>& vTxFees, const std::vector<int64_t>& vTxSigOpsCost, const CBlock& block) {
    UniValue transactions(UniValue::VARR);
    std::map<uint256, int64_t> setTxIndex;
    int i = 0;
    for (const auto& tx : block.vtx) {
         uint256 txHash = tx->GetHash();
         setTxIndex[txHash] = i++;

         if (tx->IsCoinBase()) continue;

         UniValue entry(UniValue::VOBJ);
         entry.pushKV("data", EncodeHexTx(*tx));
         entry.pushKV("txid", txHash.GetHex());
         entry.pushKV("hash", tx->GetWitnessHash().GetHex()); // Assuming GetWitnessHash exists

         UniValue deps(UniValue::VARR);
         for (const CTxIn &in : tx->vin) {
             if (setTxIndex.count(in.prevout.hash))
                 deps.push_back(setTxIndex[in.prevout.hash]);
         }
         entry.pushKV("depends", deps);

         int index_in_template = i - 1;
         if (index_in_template >= 0 && index_in_template < vTxFees.size()) {
             entry.pushKV("fee", vTxFees[index_in_template]);
         }
         if (index_in_template >= 0 && index_in_template < vTxSigOpsCost.size()) {
             entry.pushKV("sigops", vTxSigOpsCost[index_in_template]); // SigOpsCost might need scaling
         }
         entry.pushKV("weight", GetTransactionWeight(*tx)); // Assuming GetTransactionWeight exists

         transactions.push_back(entry);
    }
    return transactions;
}

// Simplified CoinbaseAuxToJson - adapt based on actual fields
UniValue CoinbaseAuxToJson(const std::vector<unsigned char>& vchCoinbaseCommitment) {
    UniValue aux(UniValue::VOBJ);
    // Example: Assuming commitment is just flags
    // if (!vchCoinbaseCommitment.empty()) aux.pushKV("flags", HexStr(vchCoinbaseCommitment));
    // Adapt based on what vchCoinbaseCommitment actually contains
    return aux;
}

// Assuming BIP22Capabilities, BIP9RulesArray, GetMutableList exist
UniValue BIP22Capabilities() {
     UniValue capabilities(UniValue::VARR);
     capabilities.push_back("proposal");
     // Add other capabilities like "coinbasetxn", "coinbasevalue", "longpoll", etc.
     return capabilities;
}

// Simplified BIP9RulesArray - adapt based on actual BIP9 logic
UniValue BIP9RulesArray(const CBlockIndex* pindexPrev, const Consensus::Params& consensusParams) {
     UniValue rules(UniValue::VARR);
     // Add currently active BIP9 rules based on pindexPrev height/state
     rules.push_back("csv"); // Example
     if (IsWitnessEnabled(pindexPrev, consensusParams)) {
          rules.push_back("segwit"); // Example
     }
     return rules;
}

// Simplified GetMutableList - adapt based on actual mutable fields
UniValue GetMutableList() {
     UniValue mutables(UniValue::VARR);
     mutables.push_back("time");
     mutables.push_back("transactions");
     mutables.push_back("prevblock");
     return mutables;
}

} // End anon namespace

static UniValue getblocktemplate(const JSONRPCRequest& request)
{
            RPCHelpMan{"getblocktemplate",
                "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
                "It returns data needed to construct a block to work on.\n"
                "For full specification, see BIPs 22, 23, 9, and 145:\n"
                "    https://github.com/palladium/bips/blob/master/bip-0022.mediawiki\n"
                "    https://github.com/palladium/bips/blob/master/bip-0023.mediawiki\n"
                "    https://github.com/palladium/bips/blob/master/bip-0009.mediawiki#getblocktemplate_changes\n"
                "    https://github.com/palladium/bips/blob/master/bip-0145.mediawiki\n",
                {
                    {"template_request", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "Format of the template",
                        {
                            {"mode", RPCArg::Type::STR, /* treat as named arg */ RPCArg::Optional::OMITTED_NAMED_ARG, "This must be set to \"template\", \"proposal\" (see BIP 23), or omitted"},
                            {"capabilities", RPCArg::Type::ARR, /* treat as named arg */ RPCArg::Optional::OMITTED_NAMED_ARG, "A list of strings",
                                {
                                    {"support", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'"},
                                },
                            },
                            {"rules", RPCArg::Type::ARR, RPCArg::Optional::NO, "A list of strings", // Made mandatory by later check
                                {
                                    {"support", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "client side supported softfork deployment"},
                                },
                             },
                        },
                        "\"template_request\""},
                },
                 RPCResult{
                     RPCResult::Type::OBJ, "", "",
                     {
                         {RPCResult::Type::NUM, "version", "The preferred block version"},
                         {RPCResult::Type::ARR, "rules", "specific block rules that are to be enforced", { {RPCResult::Type::STR, "", "rulename"}, }},
                         {RPCResult::Type::OBJ_DYN, "vbavailable", "set of pending, supported versionbit (BIP 9) softfork deployments", { {RPCResult::Type::NUM, "rulename", "identifies the bit number as indicating acceptance and readiness for the named softfork rule"}, }},
                         {RPCResult::Type::NUM, "vbrequired", "bit mask of versionbits the server requires set in submissions"},
                         {RPCResult::Type::STR, "previousblockhash", "The hash of current highest block"},
                         {RPCResult::Type::ARR, "transactions", "contents of non-coinbase transactions that should be included in the next block", { {RPCResult::Type::OBJ, "", "", { /* ... transaction fields ... */ } } }},
                         {RPCResult::Type::OBJ, "coinbaseaux", "data that should be included in the coinbase's scriptSig content", { {RPCResult::Type::ELISION, "", ""}, }},
                         {RPCResult::Type::NUM, "coinbasevalue", "maximum allowable input to coinbase transaction, including the generation award and transaction fees (in satoshis)"},
                         {RPCResult::Type::STR, "longpollid", "an ID to be used with the longpoll protocol"},
                         {RPCResult::Type::STR, "target", "The hash target"},
                         {RPCResult::Type::NUM_TIME, "mintime", "The minimum timestamp appropriate for the next block time, expressed in " + UNIX_EPOCH_TIME},
                         {RPCResult::Type::ARR, "mutable", "list of ways the block template may be changed", { {RPCResult::Type::STR, "value", "A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'"}, }},
                         {RPCResult::Type::STR_HEX, "noncerange", "A range of valid nonces"},
                         {RPCResult::Type::NUM, "sigoplimit", "limit of sigops in blocks"},
                         {RPCResult::Type::NUM, "sizelimit", /* optional */ true, "limit of block size (deprecated)"},
                         {RPCResult::Type::NUM, "weightlimit", /* optional */ true, "limit of block weight"},
                         {RPCResult::Type::NUM_TIME, "curtime", "current timestamp in " + UNIX_EPOCH_TIME},
                         {RPCResult::Type::STR, "bits", "compressed target of next block"},
                         {RPCResult::Type::NUM, "height", "The height of the next block"},
                         // --- AuxPoW Start ---
                         {RPCResult::Type::OBJ, "aux", /* optional */ true, "AuxPoW specific data (present if AuxPoW is active)", {
                            {RPCResult::Type::STR_HEX, "flags", "Magic bytes identifying the AuxPoW commitment format"},
                            {RPCResult::Type::NUM, "chainid", "Unique identifier for this chain"},
                          }},
                         {RPCResult::Type::BOOL, "submitold", /* optional */ true, "If false, submitblock expects AuxPoW data"},
                         // --- AuxPoW Ende ---
                         {RPCResult::Type::STR_HEX, "default_witness_commitment", /* optional */ true, "a valid witness commitment for the unmodified block template"},
                     }},
                RPCExamples{
                    HelpExampleCli("getblocktemplate", "'{\"rules\": [\"segwit\"]}'")
            + HelpExampleRpc("getblocktemplate", "{\"rules\": [\"segwit\"]}")
                },
            }.Check(request);

    // NodeContext& node = EnsureNodeContext(request.context); // NodeContext nicht verfügbar?
    // ChainstateManager& chainman = EnsureChainman(node);

    LOCK(cs_main);

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    std::set<std::string> setClientRules;
    int64_t nMaxVersionPreVB = -1; // Not relevant for newer versions?
    if (!request.params[0].isNull())
    {
        const UniValue& oparam = request.params[0].get_obj();
        const UniValue& modeval = find_value(oparam, "mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull()) { /* Do nothing */ }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = find_value(oparam, "longpollid");

        // Handle proposal mode
        if (strMode == "proposal")
        {
            const UniValue& dataval = find_value(oparam, "data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            GBTValidity validity = CheckBlockTemplateProposal(dataval, Params());
            return BIP22ValidationResult(validity.state); // Return validation result
        }

        // Parse rules and capabilities for template mode
        const UniValue& aClientRules = find_value(oparam, "rules");
        if (aClientRules.isArray()) {
            for (unsigned int i = 0; i < aClientRules.size(); ++i) {
                const UniValue& v = aClientRules[i];
                setClientRules.insert(v.get_str());
            }
        } else {
             // Fallback for older clients/no rules specified? BIP requires rules.
             // throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing rules array");
        }

        // Parse capabilities (optional)
        const UniValue& capabilities = find_value(oparam, "capabilities");
        if (capabilities.isArray()) {
             // Process capabilities if needed, e.g., longpoll support
        }
        // maxversion (legacy, pre-BIP9)
        const UniValue& uvMaxVersion = find_value(oparam, "maxversion");
        if (uvMaxVersion.isNum()) {
             nMaxVersionPreVB = uvMaxVersion.get_int64();
        }

    } // End parsing request.params[0]

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode"); // Should not happen

    // Check P2P status
    if (!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    if (g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0 && !Params().MineBlocksOnDemand())
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Palladium is not connected!");
    if (::ChainstateActive().IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Palladium is downloading blocks...");

    // Check for segwit rule
    if (setClientRules.count("segwit") == 0) {
         throw JSONRPCError(RPC_INVALID_PARAMETER, "getblocktemplate must be called with the \"segwit\" rule set.");
    }

    // Handle Long Poll
    static unsigned int nTransactionsUpdatedLast; // Static variable for tracking mempool updates
    if (!lpval.isNull())
    {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        std::chrono::steady_clock::time_point checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr())
        {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();
            if (lpstr.length() != 64 + std::to_string(nTransactionsUpdatedLast).length()) { // Basic format check
                 throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid longpollid format");
            }

            hashWatchedChain = ParseHashV(lpstr.substr(0, 64), "longpollid");
            // Use std::stoul for safer conversion
            try {
                nTransactionsUpdatedLastLP = std::stoul(lpstr.substr(64));
            } catch (const std::exception& e) {
                 throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid longpollid format (transaction count)");
            }
        }
        else
        {
            // Use current state if longpollid is not a string (testing?)
            hashWatchedChain = ::ChainActive().Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            checktxtime = std::chrono::steady_clock::now() + std::chrono::minutes(1);

            WAIT_LOCK(g_best_block_mutex, lock); // Assuming g_best_block_mutex/cv exist
            while (g_best_block == hashWatchedChain && IsRPCRunning()) // Assuming g_best_block exists
            {
                if (g_best_block_cv.wait_until(lock, checktxtime) == std::cv_status::timeout)
                {
                    // Timeout: Check transactions for update
                    if (EnsureMemPool().GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += std::chrono::seconds(10); // Wait another 10 seconds
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // Recheck initial conditions after wait
        if (::ChainstateActive().IsInitialBlockDownload())
             throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Palladium is downloading blocks...");
        if (g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0 && !Params().MineBlocksOnDemand())
             throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Palladium is not connected!");

    } // End Long Poll Handling

    const CChainParams& chainparams = Params();
    const CTxMemPool& mempool = EnsureMemPool(); // Use EnsureMemPool

    // Update block template if necessary
    static CBlockIndex* pindexPrev = nullptr; // Static pindexPrev for caching template
    static int64_t nStart = 0;
    static std::unique_ptr<CBlockTemplate> pblocktemplate; // Static template

    // Check if tip changed or mempool updated significantly
    if (pindexPrev != ::ChainActive().Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5))
    {
        // Clear previous template data
        pindexPrev = nullptr;

        // Store current state
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = ::ChainActive().Tip();
        nStart = GetTime();

        // Create new block template
        CScript scriptDummy = CScript() << OP_TRUE;
        try {
            pblocktemplate = BlockAssembler(mempool, chainparams).CreateNewBlock(scriptDummy);
        } catch (const std::runtime_error& e) {
             throw JSONRPCError(RPC_INTERNAL_ERROR, e.what());
        } catch (...) {
             throw JSONRPCError(RPC_INTERNAL_ERROR, "Block template creation failed");
        }
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory"); // Or internal error

        // Update pindexPrev cache only on success
        pindexPrev = pindexPrevNew;
    }
    CHECK_NONFATAL(pindexPrev); // Should be set if we got here
    CBlock* pblock = &pblocktemplate->block; // Pointer for convenience
    const Consensus::Params& consensusParams = chainparams.GetConsensus(); // Get consensus params
    int nHeight = pindexPrev->nHeight + 1; // Correct height

    // Update nTime for the template (miners can adjust this)
    UpdateTime(pblock, consensusParams, pindexPrev);
    pblock->nNonce = 0; // Miner sets nonce

    // Populate reply
    UniValue result(UniValue::VOBJ);
    result.pushKV("capabilities", BIP22Capabilities());
    result.pushKV("rules", BIP9RulesArray(pindexPrev, consensusParams));

    // Version bits info - Adapt based on your VersionBitsDeploymentInfo and caching logic
    VersionBitsCache versionbitscache; // Local cache for this call
    UniValue vbavailable(UniValue::VOBJ);
    int32_t nVBMask = 0; // Required mask
    for (int j = 0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
        Consensus::DeploymentPos pos = Consensus::DeploymentPos(j);
        ThresholdState state = VersionBitsState(pindexPrev, consensusParams, pos, versionbitscache);
        switch (state) {
            case ThresholdState::DEFINED:
            case ThresholdState::FAILED:
                break;
            case ThresholdState::LOCKED_IN:
                 // Set bit in required mask
                 nVBMask |= VersionBitsMask(consensusParams, pos);
                 // Fallthrough
            case ThresholdState::STARTED:
                 vbavailable.pushKV(VersionBitsDeploymentInfo[pos].name, consensusParams.vDeployments[pos].bit);
                 break;
            case ThresholdState::ACTIVE:
                 // Already included in rules array
                 break;
        }
    }
    result.pushKV("vbavailable", vbavailable);
    result.pushKV("vbrequired", nVBMask);


    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("transactions", TransactionsToJson(pblocktemplate->vTxFees, pblocktemplate->vTxSigOpsCost, *pblock));
    result.pushKV("coinbaseaux", CoinbaseAuxToJson(pblocktemplate->vchCoinbaseCommitment));
    result.pushKV("coinbasevalue", (int64_t)pblock->vtx[0]->vout[0].nValue);
    result.pushKV("longpollid", pindexPrev->GetBlockHash().GetHex() + ToString(nTransactionsUpdatedLast)); // Use ToString
    result.pushKV("target", ArithToUint256(arith_uint256().SetCompact(pblock->nBits)).GetHex());
    result.pushKV("mintime", (int64_t)pindexPrev->GetMedianTimePast()+1);
    result.pushKV("mutable", GetMutableList());
    result.pushKV("noncerange", "00000000ffffffff");

    // Limits based on Segwit status
    int64_t nSigOpLimit = MAX_BLOCK_SIGOPS_COST;
    int64_t nSizeLimit = MAX_BLOCK_SERIALIZED_SIZE; // Might be MAX_BLOCK_BASE_SIZE in newer code
    bool fUsingWitness = IsWitnessEnabled(pindexPrev, consensusParams);
    if (fUsingWitness) {
        result.pushKV("sigoplimit", nSigOpLimit);
        result.pushKV("weightlimit", (int64_t)MAX_BLOCK_WEIGHT);
    } else {
        // Legacy limits calculation might differ based on version
        // Using MAX_BLOCK_SIGOPS_COST / WITNESS_SCALE_FACTOR as an approximation
        result.pushKV("sigoplimit", nSigOpLimit / WITNESS_SCALE_FACTOR);
        result.pushKV("sizelimit", nSizeLimit); // Pre-segwit uses size limit
    }

    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("bits", strprintf("%08x", pblock->nBits));
    result.pushKV("height", nHeight);

    // Add default witness commitment if segwit is active
    if (fUsingWitness && !pblocktemplate->vchCoinbaseCommitment.empty()) {
        result.pushKV("default_witness_commitment", HexStr(pblocktemplate->vchCoinbaseCommitment));
    }


    // --- AuxPoW Start ---
    bool fAuxPowActive = (nHeight >= consensusParams.nAuxpowStartHeight);
    if (fAuxPowActive) {
        // Füge AuxPoW-spezifische Felder zum GBT hinzu
        UniValue aux(UniValue::VOBJ);
        // Flags sind die Magic Bytes
        aux.pushKV("flags", HexStr(pchAuxPowHeader, pchAuxPowHeader + sizeof(pchAuxPowHeader)));
        // Chain ID zur Unterscheidung
        aux.pushKV("chainid", PALLADIUM_AUXPOW_CHAIN_ID); // Verwende die Konstante
        result.pushKV("aux", aux);

        // Signalisiere, dass AuxPoW benötigt wird
        result.pushKV("submitold", false);

         LogPrintf("getblocktemplate: AuxPoW active for height %d, adding aux fields.\n", nHeight);
    } else {
         LogPrintf("getblocktemplate: AuxPoW NOT active for height %d.\n", nHeight);
    }
    // Stelle sicher, dass die Version im Template das AuxPoW-Bit korrekt hat
    result.pushKV("version", (int64_t)pblock->nVersion); // Setze die Version hier (überschreibt ggf. BIP9-Bits, was ok sein sollte)
    // --- AuxPoW Ende ---

    return result;
}

class submitblock_StateCatcher final : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    BlockValidationState state; // Verwende BlockValidationState

    explicit submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), found(false), state() {}

protected:
    void BlockChecked(const CBlock& block, const BlockValidationState& stateIn) override { // Verwende BlockValidationState
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    }
};

static UniValue submitblock(const JSONRPCRequest& request)
{
    // We allow 2 arguments for compliance with BIP22. Argument 2 is ignored.
            RPCHelpMan{"submitblock",
                "\nAttempts to submit new block to network.\n"
                "See https://en.palladium.it/wiki/BIP_0022 for full specification.\n", // Link angepasst
                {
                    {"hexdata", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded block data to submit"},
                    {"dummy", RPCArg::Type::STR, /* default */ "ignored", "dummy value, for compatibility with BIP22. This value is ignored."},
                },
                RPCResult{RPCResult::Type::NONE, "", "Returns JSON Null when valid, a string according to BIP22 otherwise"},
                RPCExamples{
                    HelpExampleCli("submitblock", "\"mydata\"")
            + HelpExampleRpc("submitblock", "\"mydata\"")
                },
            }.Check(request);

    std::shared_ptr<CBlock> blockptr = std::make_shared<CBlock>();
    CBlock& block = *blockptr;
    if (!DecodeHexBlk(block, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    // --- AuxPoW Start ---
    bool fBlockHasAuxPow = block.IsAuxpow();
    bool fShouldHaveAuxPow = false;
    int nHeight = 0;
    const Consensus::Params& consensusParams = Params().GetConsensus();
    {
        LOCK(cs_main);
        const CBlockIndex* pindexPrev = LookupBlockIndex(block.hashPrevBlock); // Verwende LookupBlockIndex
        if (pindexPrev) {
             nHeight = pindexPrev->nHeight + 1;
             fShouldHaveAuxPow = (nHeight >= consensusParams.nAuxpowStartHeight);
        } else if (block.GetHash() == consensusParams.hashGenesisBlock) {
             nHeight = 0;
             fShouldHaveAuxPow = (nHeight >= consensusParams.nAuxpowStartHeight); // Check height 0 case
        } else {
             LogPrintf("submitblock: Preceding block %s not found\n", block.hashPrevBlock.ToString());
             // BIP22 expects specific strings on failure
             // return "inconclusive - not-best-prevblk"; // This might be more appropriate than throwing
             throw JSONRPCError(RPC_VERIFY_ERROR, "Block rejected: previous block not known");
        }
    }

     LogPrintf("submitblock: Received block %s (height %d). ShouldHaveAuxPow=%d, HasAuxPow=%d\n",
               block.GetHash().ToString(), nHeight, fShouldHaveAuxPow, fBlockHasAuxPow);

    // Prüfe Konsistenz der AuxPoW-Version mit der Höhe
    if (fShouldHaveAuxPow && !fBlockHasAuxPow) {
         return "rejected: bad-auxpow-version-missing";
    }
    if (!fShouldHaveAuxPow && fBlockHasAuxPow) {
         return "rejected: bad-auxpow-unexpected";
    }
    // Wenn AuxPoW erwartet wird, prüfe ob die Daten vorhanden sind (Deserialisierung sollte das getan haben)
    if (fShouldHaveAuxPow && !block.m_auxpow) {
         LogPrintf("submitblock: AuxPoW expected but m_auxpow is null for block %s\n", block.GetHash().ToString());
         return "rejected: bad-auxpow-data-missing";
    }
    // --- AuxPoW Ende ---

    // Check block basics before submitting (e.g., coinbase existence)
    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase()) {
        // throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block does not start with a coinbase");
        return "rejected: bad-cb-missing"; // BIP22 konformer
    }


    uint256 hash = block.GetHash();
    // Check if block already exists
    {
        LOCK(cs_main);
        const CBlockIndex* pindex = LookupBlockIndex(hash);
        if (pindex) {
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS)) {
                return "duplicate";
            }
            if (pindex->nStatus & BLOCK_FAILED_MASK) {
                return "duplicate-invalid";
            }
            // Block already known but not fully validated/failed
            // ProcessNewBlock will handle this case
        }
    }

    // Check prev block again to update structures if needed
    {
        LOCK(cs_main);
        const CBlockIndex* pindex = LookupBlockIndex(block.hashPrevBlock);
        if (pindex) {
             // UpdateUncommittedBlockStructures might be needed depending on codebase version
             // If ProcessNewBlock handles this, it might not be needed here.
             // UpdateUncommittedBlockStructures(block, pindex, consensusParams);
        } else {
             // Should have been caught earlier, but double-check
             return "inconclusive - not-best-prevblk";
        }
    }


    bool new_block = false;
    // Register ValidationInterface to catch the result state
    auto sc = std::make_shared<submitblock_StateCatcher>(block.GetHash());
    RegisterSharedValidationInterface(sc);
    // Call ProcessNewBlock
    bool accepted = ProcessNewBlock(Params(), blockptr, /* fForceProcessing */ true, /* fNewBlock */ &new_block);
    UnregisterSharedValidationInterface(sc);

    // Check the result state captured by the interface
    if (sc->found) {
         // Block was processed, return the BIP22 result
         return BIP22ValidationResult(sc->state);
    }

    // If BlockChecked was somehow not called (shouldn't happen if ProcessNewBlock was attempted)
    // Fallback based on ProcessNewBlock return value
    if (!accepted) {
        // Generic rejection if state catcher failed but acceptance failed
        return "rejected";
    }
    if (!new_block) {
        // Block was accepted but not new (already known)
        return "duplicate";
    }

    // Should not be reached if state catcher worked
    return NullUniValue; // Success
}

static UniValue submitheader(const JSONRPCRequest& request)
{
            RPCHelpMan{"submitheader",
                "\nDecode header and submit it as a candidate chain tip if valid.\n"
                "Throws when the header is invalid.\n"
                 "WARNING: Incompatible with AuxPoW after the fork height.\n", // Warnung hinzugefügt
                {
                    {"hexdata", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded block header data"},
                },
                RPCResult{RPCResult::Type::NONE, "", "None"},
                RPCExamples{
                    HelpExampleCli("submitheader", "\"aabbcc\"") +
                    HelpExampleRpc("submitheader", "\"aabbcc\"")
                },
            }.Check(request);

    CBlockHeader h;
    if (!DecodeHexBlockHeader(h, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block header decode failed");
    }

    // --- AuxPoW Start ---
    // Check if AuxPoW is active for the potential height of this header
    const Consensus::Params& consensusParams = Params().GetConsensus();
    int nHeight = 0; // Height this header *would* have
    {
         LOCK(cs_main);
         const CBlockIndex* pindexPrev = LookupBlockIndex(h.hashPrevBlock);
         if (pindexPrev) {
             nHeight = pindexPrev->nHeight + 1;
         } else if (h.GetHash() == consensusParams.hashGenesisBlock) {
             nHeight = 0;
         } else {
              throw JSONRPCError(RPC_VERIFY_ERROR, "Submitheader rejected: previous block not known");
         }
    }
    if (nHeight >= consensusParams.nAuxpowStartHeight) {
         throw JSONRPCError(RPC_INVALID_REQUEST, "Submitheader is incompatible with active AuxPoW");
    }
    // --- AuxPoW Ende ---

    // NodeContext& node = EnsureNodeContext(request.context); // Nicht verfügbar?
    // ChainstateManager& chainman = EnsureChainman(node);
    BlockValidationState state; // Verwende BlockValidationState
    // Direkter Aufruf von ProcessNewBlockHeaders
    if (!ProcessNewBlockHeaders({h}, state, Params())) {
        if (state.IsInvalid()) {
             // Throw specific error based on state
             throw JSONRPCError(RPC_VERIFY_ERROR, state.ToString());
        }
        // Header was valid but didn't connect or wasn't better
        // Silently return NullUniValue? Or indicate non-acceptance?
        // Let's return Null for now as per original logic if not invalid/error
    }

    return NullUniValue;
}

static UniValue estimatesmartfee(const JSONRPCRequest& request)
{
            RPCHelpMan{"estimatesmartfee",
                "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
                "confirmation within conf_target blocks if possible and return the number of blocks\n"
                "for which the estimate is valid. Uses virtual transaction size as defined\n"
                "in BIP 141 (witness data is discounted).\n",
                {
                    {"conf_target", RPCArg::Type::NUM, RPCArg::Optional::NO, "Confirmation target in blocks (1 - 1008)"},
                    {"estimate_mode", RPCArg::Type::STR, /* default */ "CONSERVATIVE", "The fee estimate mode.\n"
             "                  Whether to return a more conservative estimate which also satisfies\n"
             "                  a longer history. A conservative estimate potentially returns a\n"
             "                  higher feerate and is more likely to be sufficient for the desired\n"
             "                  target, but is not as responsive to short term drops in the\n"
             "                  prevailing fee market.  Must be one of:\n"
             "      \"UNSET\"\n"
             "      \"ECONOMICAL\"\n"
             "      \"CONSERVATIVE\""},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::NUM, "feerate", /* optional */ true, "estimate fee rate in " + CURRENCY_UNIT + "/kvB (only present if no errors were encountered)"}, // kvB statt kB
                        {RPCResult::Type::ARR, "errors", /* optional */ true, "Errors encountered during processing", // Optional gemacht
                            {
                                {RPCResult::Type::STR, "", "error"},
                            }},
                        {RPCResult::Type::NUM, "blocks", "block number where estimate was found\n"
             "The request target will be clamped between 2 and the highest target\n"
             "fee estimation is able to return based on how long it has been running.\n"
             "An error is returned if not enough transactions and blocks\n"
             "have been observed to make an estimate for any number of blocks."},
                    }},
                RPCExamples{
                    HelpExampleCli("estimatesmartfee", "6")
                },
            }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VSTR});
    RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
    unsigned int max_target = ::feeEstimator.HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE); // Assuming ::feeEstimator exists
    unsigned int conf_target = ParseConfirmTarget(request.params[0], max_target); // Assuming ParseConfirmTarget exists
    bool conservative = true;
    if (!request.params[1].isNull()) {
        FeeEstimateMode fee_mode;
        if (!FeeModeFromString(request.params[1].get_str(), fee_mode)) { // Assuming FeeModeFromString exists
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }
        if (fee_mode == FeeEstimateMode::ECONOMICAL) conservative = false;
    }

    UniValue result(UniValue::VOBJ);
    UniValue errors(UniValue::VARR);
    FeeCalculation feeCalc;
    CFeeRate feeRate = ::feeEstimator.estimateSmartFee(conf_target, &feeCalc, conservative);
    if (feeRate != CFeeRate(0)) {
        result.pushKV("feerate", ValueFromAmount(feeRate.GetFeePerK())); // PerK or PerKVB?
    } else {
        errors.push_back("Insufficient data or no feerate found");
        result.pushKV("errors", errors);
    }
    result.pushKV("blocks", feeCalc.returnedTarget);
    return result;
}

static UniValue estimaterawfee(const JSONRPCRequest& request)
{
            RPCHelpMan{"estimaterawfee",
                "\nWARNING: This interface is unstable and may disappear or change!\n"
                "\nWARNING: This is an advanced API call that is tightly coupled to the specific\n"
                "        implementation of fee estimation. The parameters it can be called with\n"
                "        and the results it returns will change if the internal implementation changes.\n"
                "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
                "confirmation within conf_target blocks if possible. Uses virtual transaction size as\n"
                "defined in BIP 141 (witness data is discounted).\n",
                {
                    {"conf_target", RPCArg::Type::NUM, RPCArg::Optional::NO, "Confirmation target in blocks (1 - 1008)"},
                    {"threshold", RPCArg::Type::NUM, /* default */ "0.95", "The proportion of transactions in a given feerate range that must have been\n"
             "                  confirmed within conf_target in order to consider those feerates as high enough and proceed to check\n"
             "                  lower buckets."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "Results are returned for any horizon which tracks blocks up to the confirmation target",
                    {
                        {RPCResult::Type::OBJ, "short", /* optional */ true, "estimate for short time horizon", { /* ... short fields ... */ }},
                        {RPCResult::Type::OBJ, "medium", /* optional */ true, "estimate for medium time horizon", { /* ... medium fields ... */ }},
                        {RPCResult::Type::OBJ, "long", /* optional */ true, "estimate for long time horizon", { /* ... long fields ... */ }},
                    }},
                RPCExamples{
                    HelpExampleCli("estimaterawfee", "6 0.9")
                },
            }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VNUM}, true);
    RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
    unsigned int max_target = ::feeEstimator.HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
    unsigned int conf_target = ParseConfirmTarget(request.params[0], max_target);
    double threshold = 0.95;
    if (!request.params[1].isNull()) {
        threshold = request.params[1].get_real();
    }
    if (threshold < 0 || threshold > 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid threshold");
    }

    UniValue result(UniValue::VOBJ);

    // Helper lambda to format bucket info
    auto BucketToJson = [](const EstimationResult::EstimationBucket& bucket) {
        UniValue bucket_obj(UniValue::VOBJ);
        if (bucket.start >= 0) bucket_obj.pushKV("startrange", round(bucket.start)); // Use fee rate values
        if (bucket.end >= 0) bucket_obj.pushKV("endrange", round(bucket.end));
        bucket_obj.pushKV("withintarget", round(bucket.withinTarget * 100.0) / 100.0);
        bucket_obj.pushKV("totalconfirmed", round(bucket.totalConfirmed * 100.0) / 100.0);
        bucket_obj.pushKV("inmempool", round(bucket.inMempool * 100.0) / 100.0);
        bucket_obj.pushKV("leftmempool", round(bucket.leftMempool * 100.0) / 100.0);
        return bucket_obj;
    };

    for (const FeeEstimateHorizon horizon : {FeeEstimateHorizon::SHORT_HALFLIFE, FeeEstimateHorizon::MED_HALFLIFE, FeeEstimateHorizon::LONG_HALFLIFE}) {
        CFeeRate feeRate;
        EstimationResult buckets;

        // Only output results for horizons which track the target
        if (conf_target > ::feeEstimator.HighestTargetTracked(horizon)) continue;

        feeRate = ::feeEstimator.estimateRawFee(conf_target, threshold, horizon, &buckets);
        UniValue horizon_result(UniValue::VOBJ);
        UniValue errors(UniValue::VARR);

        // CFeeRate(0) is used to indicate error as a return value from estimateRawFee
        if (feeRate != CFeeRate(0)) {
            horizon_result.pushKV("feerate", ValueFromAmount(feeRate.GetFeePerK())); // PerK or PerKVB?
            horizon_result.pushKV("decay", buckets.decay);
            horizon_result.pushKV("scale", (int)buckets.scale);
             if (buckets.pass.start >= 0) horizon_result.pushKV("pass", BucketToJson(buckets.pass));
             if (buckets.fail.start >= 0) horizon_result.pushKV("fail", BucketToJson(buckets.fail));
        } else {
            // Output only information that is still meaningful in the event of error
            horizon_result.pushKV("decay", buckets.decay);
            horizon_result.pushKV("scale", (int)buckets.scale);
            // Only output fail bucket if relevant data exists
            if (buckets.fail.start >= 0) horizon_result.pushKV("fail", BucketToJson(buckets.fail));
            errors.push_back("Insufficient data or no feerate found which meets threshold");
            horizon_result.pushKV("errors",errors);
        }
        result.pushKV(StringForFeeEstimateHorizon(horizon), horizon_result); // Assuming StringForFeeEstimateHorizon exists
    }
    return result;
}

void RegisterMiningRPCCommands(CRPCTable &t)
{
// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "mining",             "getnetworkhashps",       &getnetworkhashps,       {"nblocks","height"} },
    { "mining",             "getmininginfo",          &getmininginfo,          {} },
    { "mining",             "prioritisetransaction",  &prioritisetransaction,  {"txid","dummy","fee_delta"} },
    { "mining",             "getblocktemplate",       &getblocktemplate,       {"template_request"} },
    { "mining",             "submitblock",            &submitblock,            {"hexdata","dummy"} },
//{ "mining",             "submitheader",           &submitheader,           {"hexdata"} }, // Keep commented out


    { "generating",         "generatetoaddress",      &generatetoaddress,      {"nblocks","address","maxtries"} },
    { "generating",         "generatetodescriptor",   &generatetodescriptor,   {"num_blocks","descriptor","maxtries"} },

    { "util",               "estimatesmartfee",       &estimatesmartfee,       {"conf_target", "estimate_mode"} },

    { "hidden",             "estimaterawfee",         &estimaterawfee,         {"conf_target", "threshold"} },
};
// clang-format on

    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) // Verwende ARRAYLEN statt range-based for
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}