/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "utils_sha256.h"

#include "securec.h"

#ifdef __cplusplus
extern "C" {
#endif
// clang-format off
#define BITS_PRE_BYTE   8 // bits per byte
#define SHIFTS_PER_BYTE 3 // shifts per byte
#define BITSIZE(t)      (sizeof(t) * BITS_PRE_BYTE)

#define GET_UINT32_BE(p, i)                                            \
(                                                                      \
    ((uint32_t)(p)[(i) + 0] << 24) | /* 24: 4th byte */                \
    ((uint32_t)(p)[(i) + 1] << 16) | /* 16: 3rd byte */                \
    ((uint32_t)(p)[(i) + 2] <<  8) | /*  8: 2nd byte */                \
    ((uint32_t)(p)[(i) + 3] <<  0)   /*  0: 1st byte */                \
)

#define PUT_UINT32_BE(v, p, i)                                         \
do {                                                                   \
    (p)[(i) + 0] = (uint8_t)((v) >> 24); /* 24: 4th byte */      \
    (p)[(i) + 1] = (uint8_t)((v) >> 16); /* 16: 3rd byte */      \
    (p)[(i) + 2] = (uint8_t)((v) >>  8); /*  8: 2nd byte */      \
    (p)[(i) + 3] = (uint8_t)((v) >>  0); /*  0: 1st byte */      \
} while (0)

// clang-format on
static void CompressMultiblocks(SHA256_Ctx_t *ctx, const uint8_t *in, uint32_t num);

int32_t SHA256Init(SHA256_Ctx_t *ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    (void)memset_s(ctx, sizeof(SHA256_Ctx_t), 0, sizeof(SHA256_Ctx_t));
    /**
     * @RFC 4634 6.1 SHA-224 and SHA-256 Initialization
     * SHA-256, the initial hash value, H(0):
     * H(0)0 = 6a09e667
     * H(0)1 = bb67ae85
     * H(0)2 = 3c6ef372
     * H(0)3 = a54ff53a
     * H(0)4 = 510e527f
     * H(0)5 = 9b05688c
     * H(0)6 = 1f83d9ab
     * H(0)7 = 5be0cd19
     */
    ctx->h[0] = 0x6a09e667UL;
    ctx->h[1] = 0xbb67ae85UL;
    ctx->h[2] = 0x3c6ef372UL;   // 第2位按照RFC 4634赋值
    ctx->h[3] = 0xa54ff53aUL;   // 第3位按照RFC 4634赋值
    ctx->h[4] = 0x510e527fUL;   // 第4位按照RFC 4634赋值
    ctx->h[5] = 0x9b05688cUL;   // 第5位按照RFC 4634赋值
    ctx->h[6] = 0x1f83d9abUL;   // 第6位按照RFC 4634赋值
    ctx->h[7] = 0x5be0cd19UL;   // 第7位按照RFC 4634赋值
    ctx->outlen = SHA256_DIGESTSIZE;
    return 0;
}

void SHA256Deinit(SHA256_Ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    (void)memset_s(ctx, sizeof(SHA256_Ctx_t), 0, sizeof(SHA256_Ctx_t));
}

static uint32_t CheckIsCorrupted(SHA256_Ctx_t *ctx, uint32_t nbytes);
static int32_t UpdateParamIsValid(SHA256_Ctx_t *ctx, const uint8_t *data, uint32_t nbytes)
{
    /* NULL check */
    if ((ctx == NULL) || (data == NULL && nbytes != 0)) {
        return -1;
    }

    if (ctx->errorCode != 0) {
        return -1;
    }

    if (CheckIsCorrupted(ctx, nbytes) != 0) {
        return -1;
    }

    return 0;
}

static uint32_t CheckIsCorrupted(SHA256_Ctx_t *ctx, uint32_t nbytes)
{
    uint32_t cnt0, cnt1;
    cnt0 = (ctx->lNum + (nbytes << SHIFTS_PER_BYTE)) & 0xffffffffUL;
    if (cnt0 < ctx->lNum) { /* overflow */
        if (++ctx->hNum == 0) {
            ctx->errorCode = 0;
            return -1;
        }
    }
    cnt1 = ctx->hNum + (uint32_t)(nbytes >> (BITSIZE(uint32_t) - SHIFTS_PER_BYTE));
    if (cnt1 < ctx->hNum) { /* overflow */
        ctx->errorCode = -1;
        return -1;
    }
    ctx->hNum = cnt1;
    ctx->lNum = cnt0;
    return 0;
}

int32_t SHA256Update(SHA256_Ctx_t *ctx, const uint8_t *data, uint32_t nbytes)
{
    const uint8_t *d;
    uint32_t left;
    uint32_t n;
    uint8_t *p;
    int32_t ret;
    ret = UpdateParamIsValid(ctx, data, nbytes);
    if (ret != 0) {
        return ret;
    }

    if (nbytes == 0) {
        return 0;
    }

    d = data;
    left = nbytes;
    n = ctx->blocklen;
    p = (uint8_t *)ctx->block;

    if (left < SHA256_BLOCKSIZE - n) {
        if (memcpy_s(p + n, SHA256_BLOCKSIZE - n, d, left) != EOK) {
            return -1;
        }
        ctx->blocklen += (uint32_t)left;
        return 0;
    }
    if ((n != 0) && (left >= SHA256_BLOCKSIZE - n)) {
        if (memcpy_s(p + n, SHA256_BLOCKSIZE - n, d, SHA256_BLOCKSIZE - n) != EOK) {
            return -1;
        }
        CompressMultiblocks(ctx, p, 1);
        n = SHA256_BLOCKSIZE - n;
        d += n;
        left -= n;
        ctx->blocklen = 0;
        (void)memset_s(p, SHA256_BLOCKSIZE, 0, SHA256_BLOCKSIZE);
    }

    n = (uint32_t)(left / SHA256_BLOCKSIZE);
    if (n > 0) {
        CompressMultiblocks(ctx, d, n);
        n *= SHA256_BLOCKSIZE;
        d += n;
        left -= n;
    }

    if (left != 0) {
        ctx->blocklen = (uint32_t)left;
        if (memcpy_s((uint8_t *)ctx->block, SHA256_BLOCKSIZE, d, left) != EOK) {
            return -1;
        }
    }

    return 0;
}

static int32_t FinalParamIsValid(const SHA256_Ctx_t *ctx, const uint8_t *out, const uint32_t *outLen)
{
    if ((ctx == NULL) || (out == NULL) || (outLen == NULL)) {
        return -1;
    }

    if (*outLen < ctx->outlen) {
        return -1;
    }

    if (ctx->errorCode != 0) {
        return -1;
    }

    return 0;
}
int32_t SHA256Final(SHA256_Ctx_t *ctx, uint8_t *digest, uint32_t *outlen)
{
    int32_t ret;
    uint8_t *p;
    uint32_t n, nn;

    ret = FinalParamIsValid(ctx, digest, outlen);
    if (ret != 0) {
        return ret;
    }

    p = (uint8_t *)ctx->block;
    n = ctx->blocklen;

    p[n++] = 0x80;
    if (n > (SHA256_BLOCKSIZE - 8)) { /* 8 bytes to save bits of input */
        (void)memset_s(p + n, SHA256_BLOCKSIZE - n, 0, SHA256_BLOCKSIZE - n);
        n = 0;
        CompressMultiblocks(ctx, p, 1);
    }
    (void)memset_s(p + n, SHA256_BLOCKSIZE - n, 0,
        SHA256_BLOCKSIZE - 8 - n); /* 8 bytes to save bits of input */

    p += SHA256_BLOCKSIZE - 8; /* 8 bytes to save bits of input */
    PUT_UINT32_BE(ctx->hNum, p, 0);
    p += sizeof(uint32_t);
    PUT_UINT32_BE(ctx->lNum, p, 0);
    p += sizeof(uint32_t);
    p -= SHA256_BLOCKSIZE;
    CompressMultiblocks(ctx, p, 1);
    ctx->blocklen = 0;
    (void)memset_s(p, SHA256_BLOCKSIZE, 0, SHA256_BLOCKSIZE);

    n = ctx->outlen / sizeof(uint32_t);
    for (nn = 0; nn < n; nn++) {
        PUT_UINT32_BE(ctx->h[nn], digest, sizeof(uint32_t) * nn);
    }
    *outlen = ctx->outlen;

    return 0;
}

void SHA256GenHash(const uint8_t *dataBlk, uint32_t dataBlkLen, uint8_t *hashBuf, uint32_t hashBufLen)
{
    SHA256_Ctx_t ctx;
 
    (void)SHA256Init(&ctx);
    (void)SHA256Update(&ctx, dataBlk, dataBlkLen);
    (void)SHA256Final(&ctx, hashBuf, &hashBufLen);
    SHA256Deinit(&ctx);
}

static const uint32_t K256[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL,
};

#define ROTR32(x, n) (((x) << (32 - (n))) | ((x) >> (n))) // Assumes that x is uint32_t and 0 < n < 32

#define S0(x) (ROTR32((x), 7) ^ ROTR32((x), 18) ^ ((x) >> 3))
#define S1(x) (ROTR32((x), 17) ^ ROTR32((x), 19) ^ ((x) >> 10))

#define R(W, t) \
    (S1((W)[(t) -  2]) + (W)[(t) -  7] +    \
     S0((W)[(t) - 15]) + (W)[(t) - 16])

#define ROUND(a, b, c, d, e, f, g, h, i, k)                                                                   \
    do {                                                                                                      \
        /* constants: 6, 11, 25 */                                                                            \
        (h) += (ROTR32((e), 6) ^ ROTR32((e), 11) ^ ROTR32((e), 25)) +                                         \
            ((g) ^ ((e) & ((f) ^ (g)))) + (k) + (i);                                                          \
        (d) += (h);                                                                                           \
        /* constants: 2, 13, 22 */                                                                            \
        (h) += (ROTR32((a), 2) ^ ROTR32((a), 13) ^ ROTR32((a), 22)) +                                         \
            (((a) & ((b) | (c))) | ((b) & (c)));                                                              \
    } while (0)

static inline void PrepareW(uint32_t w[64], const uint8_t block[SHA256_BLOCKSIZE])
{
    // RFC 6.2.1. Prepare the message schedule w:
    // For t = 0 to 15
    //    Wt = M(i)t
    for (unsigned i = 0; i < 16; i++) { // 16 rounds to prepare the message schedule
        w[i] = GET_UINT32_BE(block, 4 * (i)); /* 4 means bytes of uint32_t */
    }
}

static void CompressBlock(uint32_t state[8], const uint8_t block[SHA256_BLOCKSIZE])
{
    uint32_t w[64];
    unsigned i;
    uint32_t a, b, c, d, e, f, g, h;

    PrepareW(w, block);

    // For t = 16 to 63
    //    Wt = SSIG1(w(t-2)) + w(t-7) + SSIG0(t-15) + w(t-16)
    // @perf: speed up about 18% than expanded in x86_64

    // RFC 6.2.2. Initialize the working variables:
    // a, b, ..., g, h = H(i-1)[0..7]
    a = state[0]; // a = H(i-1)0
    b = state[1]; // b = H(i-1)1
    c = state[2]; // c = H(i-1)2
    d = state[3]; // d = H(i-1)3
    e = state[4]; // e = H(i-1)4
    f = state[5]; // f = H(i-1)5
    g = state[6]; // g = H(i-1)6
    h = state[7]; // h = H(i-1)7

    // RFC 6.2.3. Perform the main hash computation:
    for (i = 0; i < 16; i += 8) {  /* 0 ~ 16 rounds to do hash computation, 8 rounds pre loop */
        ROUND(a, b, c, d, e, f, g, h, w[i + 0], K256[i + 0]);
        ROUND(h, a, b, c, d, e, f, g, w[i + 1], K256[i + 1]);
        ROUND(g, h, a, b, c, d, e, f, w[i + 2], K256[i + 2]);
        ROUND(f, g, h, a, b, c, d, e, w[i + 3], K256[i + 3]);
        ROUND(e, f, g, h, a, b, c, d, w[i + 4], K256[i + 4]);
        ROUND(d, e, f, g, h, a, b, c, w[i + 5], K256[i + 5]);
        ROUND(c, d, e, f, g, h, a, b, w[i + 6], K256[i + 6]);
        ROUND(b, c, d, e, f, g, h, a, w[i + 7], K256[i + 7]);
    }

    for (i = 16; i < 64; i += 8) {  /* 16 ~ 64 rounds to do hash computation, 8 rounds pre loop */
        w[i + 0] = R(w, i + 0); /* 8 rounds pre loop, the NO.1 computation shift 0 bytes */
        ROUND(a, b, c, d, e, f, g, h, w[i + 0], K256[i + 0]);
        w[i + 1] = R(w, i + 1); /* 8 rounds pre loop, the NO.2 computation shift 1 bytes */
        ROUND(h, a, b, c, d, e, f, g, w[i + 1], K256[i + 1]);
        w[i + 2] = R(w, i + 2); /* 8 rounds pre loop, the NO.3 computation shift 2 bytes */
        ROUND(g, h, a, b, c, d, e, f, w[i + 2], K256[i + 2]);
        w[i + 3] = R(w, i + 3); /* 8 rounds pre loop, the NO.4 computation shift 3 bytes */
        ROUND(f, g, h, a, b, c, d, e, w[i + 3], K256[i + 3]);
        w[i + 4] = R(w, i + 4); /* 8 rounds pre loop, the NO.5 computation shift 4 bytes */
        ROUND(e, f, g, h, a, b, c, d, w[i + 4], K256[i + 4]);
        w[i + 5] = R(w, i + 5); /* 8 rounds pre loop, the NO.6 computation shift 5 bytes */
        ROUND(d, e, f, g, h, a, b, c, w[i + 5], K256[i + 5]);
        w[i + 6] = R(w, i + 6); /* 8 rounds pre loop, the NO.7 computation shift 6 bytes */
        ROUND(c, d, e, f, g, h, a, b, w[i + 6], K256[i + 6]);
        w[i + 7] = R(w, i + 7); /* 8 rounds pre loop, the NO.8 computation shift 7 bytes */
        ROUND(b, c, d, e, f, g, h, a, w[i + 7], K256[i + 7]);
    }

    // RFC 6.2.4. Compute the intermediate hash value H(i):
    // H(i) = [a, b, ..., g, h] + H(i-1)[0..7]
    state[0] += a; // H(i)0 = a + H(i-1)0
    state[1] += b; // H(i)1 = b + H(i-1)1
    state[2] += c; // H(i)2 = c + H(i-1)2
    state[3] += d; // H(i)3 = d + H(i-1)3
    state[4] += e; // H(i)4 = e + H(i-1)4
    state[5] += f; // H(i)5 = f + H(i-1)5
    state[6] += g; // H(i)6 = g + H(i-1)6
    state[7] += h; // H(i)7 = h + H(i-1)7
}
#undef ROTR32
#undef ROUND

static void CompressMultiblocks(SHA256_Ctx_t *ctx, const uint8_t *in, uint32_t num)
{
    uint32_t n = num;
    const uint8_t *p = in;
    while (n > 0) {
        CompressBlock(ctx->h, p);
        p += SHA256_BLOCKSIZE;
        n--;
    }
}

#ifdef __cplusplus
}
#endif

