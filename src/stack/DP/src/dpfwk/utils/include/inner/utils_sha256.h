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
#ifndef UTILS_SHA256_H
#define UTILS_SHA256_H

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cpluscplus */


/** @defgroup LLF SHA2 Low level function */
/* SHA2-256 */
#define SHA256_BLOCKSIZE  64
#define SHA256_DIGESTSIZE 32

/* SHA2 256 ctx */
typedef struct {
    uint32_t h[SHA256_DIGESTSIZE / sizeof(uint32_t)]; /* 256 bits for SHA256 state */
    uint32_t block[SHA256_BLOCKSIZE / sizeof(uint32_t)]; /* 512 bits block cache */
    uint32_t lNum, hNum;                                           /* input bits counter, max 2^64 bits */
    uint32_t blocklen;                                     /* block length */
    uint32_t outlen;                                       /* digest output length */
    uint32_t errorCode; /* error Code */
} SHA256_Ctx_t;

/**
 * @defgroup SHA256Init
 * @ingroup LLF Low Level Functions
 * @par Prototype
 * @code
 * int32_t SHA256Init(SHA256_Ctx_t *ctx)
 * @endcode
 *
 * @par Purpose
 * This is used to initialize the SHA256 ctx for a digest operation.
 *
 * @par Description
 * SHA256Init function initializes the ctx for
 * a digest operation. This function must be called before
 * SHA256Update or SHA256Final operations. This function will not
 * allocate memory for any of the ctx variables. Instead the caller is
 * expected to pass a ctx pointer pointing to a valid memory location
 * (either locally or dynamically allocated).
 *
 * @param[in] ctx The sha256 ctx [N/A]
 *
 * @retval #CRYPT_SUCCESS ctx is initialized
 * @retval #CRYPT_NULL_INPUT ctx is NULL
 */
int32_t SHA256Init(SHA256_Ctx_t *ctx);

/**
 * @defgroup SHA256Update
 * @ingroup LLF Low Level Functions
 * @par Prototype
 * @code
 * int32_t SHA256Update(SHA256_Ctx_t *ctx, const uint8_t *data, usize_t nbytes)
 * @endcode
 *
 * @par Purpose
 * This is used to perform sha256 digest operation on chunks of data.
 *
 * @par Description
 * SHA256Update function performs digest operation on
 * chunks of data. This method of digesting is used when data is
 * present in multiple buffers or not available all at once.
 * SHA256Init must have been called before calling this
 * function.
 *
 * @param[in] ctx The sha256 ctx [N/A]
 * @param[in] data The input data [N/A]
 * @param[in] nbytes The input data length [N/A]
 *
 * @retval #CRYPT_SUCCESS If partial digest is calculated
 * @retval #CRYPT_NULL_INPUT input arguments is NULL
 * @retval #CRYPT_SHA2_ERR_OVERFLOW input message is overflow
 */
int32_t SHA256Update(SHA256_Ctx_t *ctx, const uint8_t *data, uint32_t nbytes);

/**
 * @defgroup SHA256Final
 * @ingroup LLF Low Level Functions
 * @par Prototype
 * @code
 * int32_t SHA256Final(SHA256_Ctx_t *ctx, uint8_t *digest, uint32_t *len)
 * @endcode
 *
 * @par Purpose
 * This is used to complete sha256 digest operation on remaining data, and is
 * called at the end of digest operation.
 *
 * @par Description
 * SHA256Final function completes digest operation on remaining data, and
 * is called at the end of digest operation.
 * SHA256Init must have been called before calling this function. This
 * function calculates the digest. The memory for digest must
 * already have been allocated.
 *
 * @param[in] ctx The sha256 ctx [N/A]
 * @param[out] digest The digest [N/A]
 *
 * @retval #CRYPT_SUCCESS If partial digest is calculated
 * @retval #CRYPT_NULL_INPUT input arguments is NULL
 * @retval #CRYPT_SHA2_ERR_OVERFLOW input message is overflow
 * @retval #CRYPT_SHA2_OUT_BUFF_LEN_NOT_ENOUGH output buffer is not enough
 */
int32_t SHA256Final(SHA256_Ctx_t *ctx, uint8_t *digest, uint32_t *outlen);

/**
 * @ingroup LLF Low Level Functions
 *
 * @brief SHA256 deinit function
 *
 * @param[in,out] ctx The SHA256 ctx
 */
void SHA256Deinit(SHA256_Ctx_t *ctx);

/* SHA256算法生成hash接口 */
void SHA256GenHash(const uint8_t *dataBlk, uint32_t dataBlkLen, uint8_t *hashBuf, uint32_t hashBufLen);
#ifdef __cplusplus
}
#endif
#endif
