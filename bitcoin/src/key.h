// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KEY_H
#define BITCOIN_KEY_H

#include <stdexcept>
#include <vector>

#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

#include "serialize.h"
#include "uint256.h"

// secp160k1
// const unsigned int PRIVATE_KEY_SIZE = 192;
// const unsigned int PUBLIC_KEY_SIZE  = 41;
// const unsigned int SIGNATURE_SIZE   = 48;
//
// secp192k1
// const unsigned int PRIVATE_KEY_SIZE = 222;
// const unsigned int PUBLIC_KEY_SIZE  = 49;
// const unsigned int SIGNATURE_SIZE   = 57;
//
// secp224k1
// const unsigned int PRIVATE_KEY_SIZE = 250;
// const unsigned int PUBLIC_KEY_SIZE  = 57;
// const unsigned int SIGNATURE_SIZE   = 66;
//
// secp256k1:
// const unsigned int PRIVATE_KEY_SIZE = 279;
// const unsigned int PUBLIC_KEY_SIZE  = 65;
// const unsigned int SIGNATURE_SIZE   = 72;
//
// see www.keylength.com
// script supports up to 75 for single byte push

// Generate a private key from just the secret parameter
int static inline EC_KEY_regenerate_key(EC_KEY *eckey, BIGNUM *priv_key)
{
    int ok = 0;
    BN_CTX *ctx = NULL;
    EC_POINT *pub_key = NULL;

    if (!eckey) return 0;

    const EC_GROUP *group = EC_KEY_get0_group(eckey);

    if ((ctx = BN_CTX_new()) == NULL)
        goto err;

    pub_key = EC_POINT_new(group);

    if (pub_key == NULL)
        goto err;

    if (!EC_POINT_mul(group, pub_key, priv_key, NULL, NULL, ctx))
        goto err;

    EC_KEY_set_private_key(eckey,priv_key);
    EC_KEY_set_public_key(eckey,pub_key);

    ok = 1;

err:

    if (pub_key)
        EC_POINT_free(pub_key);
    if (ctx != NULL)
        BN_CTX_free(ctx);

    return(ok);
}

// Perform ECDSA key recovery (see SEC1 4.1.6) for curves over (mod p)-fields
// recid selects which key is recovered
// if check is nonzero, additional checks are performed
int static inline ECDSA_SIG_recover_key_GFp(EC_KEY *eckey, ECDSA_SIG *ecsig, const unsigned char *msg, int msglen, int recid, int check)
{
    if (!eckey) return 0;

    int ret = 0;
    BN_CTX *ctx = NULL;

    BIGNUM *x = NULL;
    BIGNUM *e = NULL;
    BIGNUM *order = NULL;
    BIGNUM *sor = NULL;
    BIGNUM *eor = NULL;
    BIGNUM *field = NULL;
    EC_POINT *R = NULL;
    EC_POINT *O = NULL;
    EC_POINT *Q = NULL;
    BIGNUM *rr = NULL;
    BIGNUM *zero = NULL;
    int n = 0;
    int i = recid / 2;

    const EC_GROUP *group = EC_KEY_get0_group(eckey);
    if ((ctx = BN_CTX_new()) == NULL) { ret = -1; goto err; }
    BN_CTX_start(ctx);
    order = BN_CTX_get(ctx);
    if (!EC_GROUP_get_order(group, order, ctx)) { ret = -2; goto err; }
    x = BN_CTX_get(ctx);
    if (!BN_copy(x, order)) { ret=-1; goto err; }
    if (!BN_mul_word(x, i)) { ret=-1; goto err; }
    if (!BN_add(x, x, ecsig->r)) { ret=-1; goto err; }
    field = BN_CTX_get(ctx);
    if (!EC_GROUP_get_curve_GFp(group, field, NULL, NULL, ctx)) { ret=-2; goto err; }
    if (BN_cmp(x, field) >= 0) { ret=0; goto err; }
    if ((R = EC_POINT_new(group)) == NULL) { ret = -2; goto err; }
    if (!EC_POINT_set_compressed_coordinates_GFp(group, R, x, recid % 2, ctx)) { ret=0; goto err; }
    if (check)
    {
        if ((O = EC_POINT_new(group)) == NULL) { ret = -2; goto err; }
        if (!EC_POINT_mul(group, O, NULL, R, order, ctx)) { ret=-2; goto err; }
        if (!EC_POINT_is_at_infinity(group, O)) { ret = 0; goto err; }
    }
    if ((Q = EC_POINT_new(group)) == NULL) { ret = -2; goto err; }
    n = EC_GROUP_get_degree(group);
    e = BN_CTX_get(ctx);
    if (!BN_bin2bn(msg, msglen, e)) { ret=-1; goto err; }
    if (8*msglen > n) BN_rshift(e, e, 8-(n & 7));
    zero = BN_CTX_get(ctx);
    if (!BN_zero(zero)) { ret=-1; goto err; }
    if (!BN_mod_sub(e, zero, e, order, ctx)) { ret=-1; goto err; }
    rr = BN_CTX_get(ctx);
    if (!BN_mod_inverse(rr, ecsig->r, order, ctx)) { ret=-1; goto err; }
    sor = BN_CTX_get(ctx);
    if (!BN_mod_mul(sor, ecsig->s, rr, order, ctx)) { ret=-1; goto err; }
    eor = BN_CTX_get(ctx);
    if (!BN_mod_mul(eor, e, rr, order, ctx)) { ret=-1; goto err; }
    if (!EC_POINT_mul(group, Q, eor, R, sor, ctx)) { ret=-2; goto err; }
    if (!EC_KEY_set_public_key(eckey, Q)) { ret=-2; goto err; }

    ret = 1;

err:
    if (ctx) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
    }
    if (R != NULL) EC_POINT_free(R);
    if (O != NULL) EC_POINT_free(O);
    if (Q != NULL) EC_POINT_free(Q);
    return ret;
}

class key_error : public std::runtime_error
{
public:
    explicit key_error(const std::string& str) : std::runtime_error(str) {}
};


// secure_allocator is defined in serialize.h
// CPrivKey is a serialized private key, with all parameters included (279 bytes)
typedef std::vector<unsigned char, secure_allocator<unsigned char> > CPrivKey;
// CSecret is a serialization of just the secret parameter (32 bytes)
typedef std::vector<unsigned char, secure_allocator<unsigned char> > CSecret;

class CKey
{
protected:
    EC_KEY* pkey;
    bool fSet;

public:
    CKey()
    {
        pkey = EC_KEY_new_by_curve_name(NID_secp256k1);
        if (pkey == NULL)
            throw key_error("CKey::CKey() : EC_KEY_new_by_curve_name failed");
        fSet = false;
    }

    CKey(const CKey& b)
    {
        pkey = EC_KEY_dup(b.pkey);
        if (pkey == NULL)
            throw key_error("CKey::CKey(const CKey&) : EC_KEY_dup failed");
        fSet = b.fSet;
    }

    CKey& operator=(const CKey& b)
    {
        if (!EC_KEY_copy(pkey, b.pkey))
            throw key_error("CKey::operator=(const CKey&) : EC_KEY_copy failed");
        fSet = b.fSet;
        return (*this);
    }

    ~CKey()
    {
        EC_KEY_free(pkey);
    }

    bool IsNull() const
    {
        return !fSet;
    }

    void MakeNewKey()
    {
        if (!EC_KEY_generate_key(pkey))
            throw key_error("CKey::MakeNewKey() : EC_KEY_generate_key failed");
        fSet = true;
    }

    bool SetPrivKey(const CPrivKey& vchPrivKey)
    {
        const unsigned char* pbegin = &vchPrivKey[0];
        if (!d2i_ECPrivateKey(&pkey, &pbegin, vchPrivKey.size()))
            return false;
        fSet = true;
        return true;
    }

    bool SetSecret(const CSecret& vchSecret)
    {
        EC_KEY_free(pkey);
        pkey = EC_KEY_new_by_curve_name(NID_secp256k1);
        if (pkey == NULL)
            throw key_error("CKey::SetSecret() : EC_KEY_new_by_curve_name failed");
        if (vchSecret.size() != 32)
            throw key_error("CKey::SetSecret() : secret must be 32 bytes");
        BIGNUM *bn = BN_bin2bn(&vchSecret[0],32,BN_new());
        if (bn == NULL)
            throw key_error("CKey::SetSecret() : BN_bin2bn failed");
        if (!EC_KEY_regenerate_key(pkey,bn))
        {
            BN_clear_free(bn);
            throw key_error("CKey::SetSecret() : EC_KEY_regenerate_key failed");
        }
        BN_clear_free(bn);
        fSet = true;
        return true;
    }

    CSecret GetSecret() const
    {
        CSecret vchRet;
        vchRet.resize(32);
        const BIGNUM *bn = EC_KEY_get0_private_key(pkey);
        int nBytes = BN_num_bytes(bn);
        if (bn == NULL)
            throw key_error("CKey::GetSecret() : EC_KEY_get0_private_key failed");
        int n=BN_bn2bin(bn,&vchRet[32 - nBytes]);
        if (n != nBytes) 
            throw key_error("CKey::GetSecret(): BN_bn2bin failed");
        return vchRet;
    }

    CPrivKey GetPrivKey() const
    {
        unsigned int nSize = i2d_ECPrivateKey(pkey, NULL);
        if (!nSize)
            throw key_error("CKey::GetPrivKey() : i2d_ECPrivateKey failed");
        CPrivKey vchPrivKey(nSize, 0);
        unsigned char* pbegin = &vchPrivKey[0];
        if (i2d_ECPrivateKey(pkey, &pbegin) != nSize)
            throw key_error("CKey::GetPrivKey() : i2d_ECPrivateKey returned unexpected size");
        return vchPrivKey;
    }

    bool SetPubKey(const std::vector<unsigned char>& vchPubKey)
    {
        const unsigned char* pbegin = &vchPubKey[0];
        if (!o2i_ECPublicKey(&pkey, &pbegin, vchPubKey.size()))
            return false;
        fSet = true;
        return true;
    }

    std::vector<unsigned char> GetPubKey() const
    {
        unsigned int nSize = i2o_ECPublicKey(pkey, NULL);
        if (!nSize)
            throw key_error("CKey::GetPubKey() : i2o_ECPublicKey failed");
        std::vector<unsigned char> vchPubKey(nSize, 0);
        unsigned char* pbegin = &vchPubKey[0];
        if (i2o_ECPublicKey(pkey, &pbegin) != nSize)
            throw key_error("CKey::GetPubKey() : i2o_ECPublicKey returned unexpected size");
        return vchPubKey;
    }

    bool Sign(uint256 hash, std::vector<unsigned char>& vchSig)
    {
        vchSig.clear();
        ECDSA_SIG *sig = ECDSA_do_sign((unsigned char *) &hash, sizeof(hash), pkey);

        if (sig == NULL)
        {
            printf("ERROR, ECDSA_sign failed in key.h:Sign()\n");
            return false;
        }

        BN_CTX *ctx = BN_CTX_new();
        BN_CTX_start(ctx);
        const EC_GROUP *group = EC_KEY_get0_group(pkey);
        BIGNUM *order = BN_CTX_get(ctx);
        BIGNUM *halforder = BN_CTX_get(ctx);
        EC_GROUP_get_order(group, order, ctx);
        BN_rshift1(halforder, order);

        if (fHighS && (BN_cmp(sig->s, halforder) < 0))
        {
            // enforce high S values
            BN_sub(sig->s, order, sig->s);
        }

        if (fLowS && (BN_cmp(sig->s, halforder) > 0))
        {
            // enforce low S values
            BN_sub(sig->s, order, sig->s);
        }

        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        unsigned int nSize = ECDSA_size(pkey);
        vchSig.resize(nSize); // Make sure it is big enough
        unsigned char *pos = &vchSig[0];
        nSize = i2d_ECDSA_SIG(sig, &pos);
        //printf("DEBUG DER R: 0x%s\n", BN_bn2hex(sig->r));
        //printf("DEBUG DER R:   %s\n", BN_bn2dec(sig->r));
        //printf("DEBUG DER S: 0x%s\n", BN_bn2hex(sig->s));
        //printf("DEBUG DER S:   %s\n", BN_bn2dec(sig->s));
        ECDSA_SIG_free(sig);
        vchSig.resize(nSize); // Shrink to fit actual size
        return true;
    }

    // create a compact signature (65 bytes), which allows reconstructing the used public key
    // The format is one header byte, followed by two times 32 bytes for the serialized r and s values.
    // The header byte: 0x1B = first key with even y, 0x1C = first key with odd y,
    //                  0x1D = second key with even y, 0x1E = second key with odd y
    bool SignCompact(uint256 hash, std::vector<unsigned char>& vchSig)
    {
        bool fOk = false;
        ECDSA_SIG *sig = ECDSA_do_sign((unsigned char*)&hash, sizeof(hash), pkey);
        if (sig==NULL)
            return false;
        vchSig.clear();
        vchSig.resize(65,0);
        int nBitsR = BN_num_bits(sig->r);
        int nBitsS = BN_num_bits(sig->s);
        if (nBitsR <= 256 && nBitsS <= 256)
        {
            int nRecId = -1;
            for (int i=0; i<4; i++)
            {
                CKey keyRec;
                keyRec.fSet = true;
                if (ECDSA_SIG_recover_key_GFp(keyRec.pkey, sig, (unsigned char*)&hash, sizeof(hash), i, 1) == 1)
                    if (keyRec.GetPubKey() == this->GetPubKey())
                    {
                        nRecId = i;
                        break;
                    }
            }

            if (nRecId == -1)
                throw key_error("CKey::SignCompact() : unable to construct recoverable key");

            vchSig[0] = nRecId+27;
            BN_bn2bin(sig->r,&vchSig[33-(nBitsR+7)/8]);
            BN_bn2bin(sig->s,&vchSig[65-(nBitsS+7)/8]);
            fOk = true;
        }
        ECDSA_SIG_free(sig);
        return fOk;
    }

    // reconstruct public key from a compact signature
    // This is only slightly more CPU intensive than just verifying it.
    // If this function succeeds, the recovered public key is guaranteed to be valid
    // (the signature is a valid signature of the given data for that key)
    bool SetCompactSignature(uint256 hash, const std::vector<unsigned char>& vchSig)
    {
        if (vchSig.size() != 65)
            return false;
        if (vchSig[0]<27 || vchSig[0]>=31)
            return false;
        ECDSA_SIG *sig = ECDSA_SIG_new();
        BN_bin2bn(&vchSig[1],32,sig->r);
        BN_bin2bn(&vchSig[33],32,sig->s);

        EC_KEY_free(pkey);
        pkey = EC_KEY_new_by_curve_name(NID_secp256k1);
        if (ECDSA_SIG_recover_key_GFp(pkey, sig, (unsigned char*)&hash, sizeof(hash), vchSig[0] - 27, 0) == 1)
        {
            fSet = true;
            ECDSA_SIG_free(sig);
            return true;
        }
        return false;
    }

    bool Verify(uint256 hash, const std::vector<unsigned char>& vchSig)
    {
        // -1 = error, 0 = bad sig, 1 = good
        if (ECDSA_verify(0, (unsigned char*)&hash, sizeof(hash), &vchSig[0], vchSig.size(), pkey) != 1)
            return false;
        return true;
    }

    // Verify a compact signature
    bool VerifyCompact(uint256 hash, const std::vector<unsigned char>& vchSig)
    {
        CKey key;
        if (!key.SetCompactSignature(hash, vchSig))
            return false;
        if (GetPubKey() != key.GetPubKey())
            return false;
        return true;
    }

    bool IsValid()
    {
        if (!fSet)
            return false;

        CSecret secret = GetSecret();
        CKey key2;
        key2.SetSecret(secret);
        return GetPubKey() == key2.GetPubKey();
    }
};

#endif
