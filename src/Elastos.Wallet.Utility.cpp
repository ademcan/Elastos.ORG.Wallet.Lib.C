#include "Elastos.Wallet.Utility.h"
#include <string>
#include "BRBIP32Sequence.h"
#include "WalletTool.h"
#include "ElaController.h"
#include "BigIntFormat.h"
#include "CMemBlock.h"
#include "Mnemonic.h"
#include "BRCrypto.h"
#include "Utils.h"
#include "BRBIP39Mnemonic.h"

static char* getResultStrEx(const char* src, int len)
{
    char* key = (char*)malloc(sizeof(char) * (len + 1));
    if (!key) {
        return nullptr;
    }
    memcpy(key, src, len);
    key[len] = '\0';

    return key;
}

static char* getResultStr(const CMBlock& mblock)
{
    CMemBlock<char> cmblock;
    cmblock = Hex2Str(mblock);

    return getResultStrEx(cmblock, cmblock.GetSize());
}

static char* getPublickeyFromPrivateKey(const BRKey& key)
{
    CMBlock privKey;
    privKey.SetMemFixed((uint8_t *) &key.secret, sizeof(key.secret));
    CMBlock result;
    result.Resize(33);
    getPubKeyFromPrivKey(result, (UInt256 *) (uint8_t *) privKey);

    return getResultStr(result);
}

static char* getAddressEx(const char* publicKey, int signType)
{
    if (!publicKey) {
        return nullptr;
    }

    CMemBlock<char> cPublickey;
    cPublickey.SetMemFixed(publicKey, strlen(publicKey) + 1);
    CMBlock pubKey = Str2Hex(cPublickey);

    CMBlock code = Utils::getCode(pubKey, signType);

    std::string redeedScript = Utils::encodeHex(code, code.GetSize());

    UInt168 hash = Utils::codeToProgramHash(redeedScript);

    std::string address = Utils::UInt168ToAddress(hash);

    return getResultStrEx(address.c_str(), address.length());
}

static MasterPublicKey* getMasterPublicKey(BRKey& key, const UInt256& chainCode)
{
    CMBlock privKey(sizeof(UInt256));
    privKey.SetMemFixed((uint8_t*)&key.secret, sizeof(key.secret));
    CMBlock publicKey;
    publicKey.Resize(33);
    getPubKeyFromPrivKey(publicKey, (UInt256 *) (uint8_t *) privKey);


    MasterPublicKey* masterKey = new MasterPublicKey();
    if (!masterKey) {
        return nullptr;
    }

    masterKey->fingerPrint = BRKeyHash160(&key).u32[0];
    memcpy(masterKey->chainCode, (uint8_t*)&chainCode, sizeof(chainCode));
    memcpy(masterKey->publicKey, (void *)publicKey, publicKey.GetSize());

    return masterKey;
}

static BRMasterPubKey* toBRMasterPubKey(const MasterPublicKey* pubKey)
{
    BRMasterPubKey* brPublicKey = new BRMasterPubKey();
    if (!brPublicKey) {
        return nullptr;
    }

    brPublicKey->fingerPrint = pubKey->fingerPrint;
    memcpy((uint8_t*)&brPublicKey->chainCode, pubKey->chainCode, sizeof(brPublicKey->chainCode));
    memcpy(brPublicKey->pubKey, pubKey->publicKey, sizeof(brPublicKey->pubKey));

    return brPublicKey;
}

char* getSinglePrivateKey(const void* seed, int seedLen)
{
    if (!seed || seedLen <= 0) {
        return nullptr;
    }

    BRKey masterKey;
    BRBIP32APIAuthKey(&masterKey, seed, seedLen);

    CMBlock privateKey(sizeof(UInt256));
    memcpy(privateKey, &masterKey.secret, sizeof(UInt256));

    return getResultStr(privateKey);
}

char* getSinglePublicKey(const void* seed, int seedLen)
{
    if (!seed || seedLen <= 0) {
        return nullptr;
    }

    BRKey masterKey;
    BRBIP32APIAuthKey(&masterKey, seed, seedLen);

    return getPublickeyFromPrivateKey(masterKey);
}

MasterPublicKey* getMasterPublicKey(const void* seed, int seedLen, int coinType)
{
    if (!seed || seedLen <= 0) {
        return nullptr;
    }

    UInt256 chainCode;
    BRKey key;
    BRBIP32PrivKeyPath(&key, &chainCode, seed, seedLen, 3, 44 | BIP32_HARD,
                       coinType | BIP32_HARD, 0 | BIP32_HARD);

    MasterPublicKey* masterKey = getMasterPublicKey(key, chainCode);

    var_clean(&chainCode);

    return masterKey;
}

char* getAddress(const char* publicKey)
{
    return getAddressEx(publicKey, 0xAC);
}

char* generateMnemonic(const char* language, const char* words)
{
    if (!language || !words) {
        return nullptr;
    }

    CMemBlock<uint8_t> seed128 = WalletTool::GenerateSeed128();
    Mnemonic mnemonic(language, words);
    CMemBlock<char> phrase = WalletTool::GeneratePhraseFromSeed(seed128, mnemonic.words());

    return getResultStrEx(phrase, phrase.GetSize());
}

int getSeedFromMnemonic(void** seed, const char* mmemonic, const char* language, const char* words, const char* mnemonicPassword)
{
    if (!seed || !mmemonic || !language || !words) {
        return 0;
    }

    CMemBlock<char> phraseData;
    phraseData.SetMemFixed(mmemonic, strlen(mmemonic) + 1);

    Mnemonic* pMnemonic = new Mnemonic(language, words);
    if (!pMnemonic || !WalletTool::PhraseIsValid(phraseData, pMnemonic->words())) {
        return 0;
    }

    UInt512 useed;
    BRBIP39DeriveKey(&useed, mmemonic, mnemonicPassword);

    delete pMnemonic;

    int len = sizeof(useed);

    void* result = malloc(len);
    if (!result) {
        return 0;
    }
    memcpy(result, &useed, len);
    *seed = result;

    return len;
}

int sign(const char* privateKey, const void* data, int len, void** signedData)
{
    if (!privateKey || !data || len <= 0 || !signedData) {
        return 0;
    }

    CMemBlock<char> cPrivatekey;
    cPrivatekey.SetMemFixed(privateKey, strlen(privateKey) + 1);
    CMBlock privKey = Str2Hex(cPrivatekey);

    UInt256 md = UINT256_ZERO;
    BRSHA256(&md, data, len);

    CMBlock mbSignedData;
    mbSignedData.Resize(65);
    bool ret = ECDSA65Sign_sha256(privKey, privKey.GetSize(), &md, mbSignedData, mbSignedData.GetSize());
    if (ret) {
        int signedlen = mbSignedData.GetSize();
        void* buf = malloc(signedlen);
        if (!buf) return 0;
        memcpy(buf, mbSignedData, signedlen);
        *signedData = buf;
        return signedlen;
    }

    return 0;
}

bool verify(const char* publicKey, const void* data,
            int len, const void* signedData, int signedLen)
{
    if (!publicKey || !data || len <= 0 || !signedData || signedLen <= 0) {
        return false;
    }

    CMemBlock<char> cPublickey;
    cPublickey.SetMemFixed(publicKey, strlen(publicKey) + 1);
    CMBlock pubKey = Str2Hex(cPublickey);

    UInt256 md = UINT256_ZERO;
    BRSHA256(&md, data, len);
    CMBlock mbSignedData;
    mbSignedData.Resize((size_t) signedLen);
    memcpy((void *)mbSignedData, signedData, signedLen);

    return ECDSA65Verify_sha256(pubKey, pubKey.GetSize(), &md, mbSignedData, mbSignedData.GetSize());
}

char* generateRawTransaction(const char* transaction)
{
    if (!transaction) {
        return nullptr;
    }
    std::string rawTransaction = ElaController::genRawTransaction(transaction);

    return getResultStrEx(rawTransaction.c_str(), rawTransaction.length());
}

char* generateSubPrivateKey(const void* seed, int seedLen, int coinType, int chain, int index)
{
    if (!seed || seedLen <= 0) {
        return nullptr;
    }

    UInt256 chainCode;
    BRKey key;
    BRBIP32PrivKeyPath(&key, &chainCode, seed, seedLen, 5, 44 | BIP32_HARD,
                        coinType | BIP32_HARD, 0 | BIP32_HARD, chain, index);
    var_clean(&chainCode);

    std::string keyStr = Utils::UInt256ToString(key.secret);

    return getResultStrEx(keyStr.c_str(), keyStr.length());
}

char* generateSubPublicKey(const MasterPublicKey* masterPublicKey, int chain, int index)
{
    if (!masterPublicKey) {
        return nullptr;
    }

    BRMasterPubKey* brPublicKey = toBRMasterPubKey(masterPublicKey);
    if (!brPublicKey) {
        return nullptr;
    }

    size_t len = BRBIP32PubKey(NULL, 0, *brPublicKey, chain, index);
    CMBlock subPubKey(len);
    BRBIP32PubKey(subPubKey, subPubKey.GetSize(), *brPublicKey, chain, index);

    delete brPublicKey;

    return getResultStr(subPubKey);
}

void freeBuf(void* buf)
{
    if (!buf) return;

    free(buf);
}

MasterPublicKey* getIdChainMasterPublicKey(const void* seed, int seedLen)
{
    if (!seed || seedLen <= 0) {
        return nullptr;
    }

    BRKey idMasterKey;
    UInt256 idChainCode;
    BRBIP32PrivKeyPath(&idMasterKey, &idChainCode, &seed, sizeof(seed), 1, 0 | BIP32_HARD);

    MasterPublicKey* masterKey = getMasterPublicKey(idMasterKey, idChainCode);

    var_clean(&idChainCode);

    return masterKey;
}

char* generateIdChainSubPrivateKey(const void* seed, int seedLen, int purpose, int index)
{
    if (!seed || seedLen <= 0) {
        return nullptr;
    }

    BRKey key;
    UInt256 chainCode;
    BRBIP32PrivKeyPath(&key, &chainCode, seed, seedLen, 3, 0 | BIP32_HARD, purpose, index);

    var_clean(&chainCode);

    std::string keyStr = Utils::UInt256ToString(key.secret);

    return getResultStrEx(keyStr.c_str(), keyStr.length());
}

char* generateIdChainSubPublicKey(const MasterPublicKey* masterPublicKey, int purpose, int index)
{
    if (!masterPublicKey) {
        return nullptr;
    }

    BRMasterPubKey* brPublicKey = toBRMasterPubKey(masterPublicKey);
    if (!brPublicKey) {
        return nullptr;
    }

    uint8_t pubKey[BRBIP32PubKey(NULL, 0, *brPublicKey, purpose, index)];
    size_t len = BRBIP32PubKey(pubKey, sizeof(pubKey), *brPublicKey, purpose, index);

    delete brPublicKey;

    BRKey rawKey;
    BRKeySetPubKey(&rawKey, pubKey, len);
    CMBlock cbPubKey;
    cbPubKey.SetMemFixed(rawKey.pubKey, len);

    return getResultStr(cbPubKey);
}

char* getDid(const char* publicKey)
{
    return getAddressEx(publicKey, 0xAD);
}
