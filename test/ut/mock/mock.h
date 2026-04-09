/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef KTEST_MOCK_H
#define KTEST_MOCK_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/mman.h>

#if defined(__aarch64__) || defined(_M_ARM64)
    #define CODESIZE 16U
    #define CODESIZE_MIN 16U
    #define CODESIZE_MAX CODESIZE

    #define SWAP_ADDR_FAR(ADDR, MOCK_ADDR) \
    do { \
        ((uint32_t*)ADDR)[0] = 0x58000040 | 9; \
        ((uint32_t*)ADDR)[1] = 0xd61f0120 | (9 << 5); \
        ((uint32_t*)ADDR)[2] = (uint32_t)(uintptr_t)MOCK_ADDR; \
        ((uint32_t*)ADDR)[3] = (uint32_t)((uintptr_t)MOCK_ADDR >> 32); \
        __builtin___clear_cache(ADDR, ADDR + CODESIZE); \
    } while (0)
    #define SWAP_ADDR_NEAR(ADDR, MOCK_ADDR) SWAP_ADDR_FAR(ADDR, MOCK_ADDR)
#elif defined(__arm__) || defined(_M_ARM)
    #define CODESIZE 8U
    #define CODESIZE_MIN 8U
    #define CODESIZE_MAX CODESIZE

    #define SWAP_ADDR_FAR(ADDR, MOCK_ADDR) \
    do { \
        ((uint32_t*)ADDR)[0] = 0xe51ff004; \
        ((uint32_t*)ADDR)[1] = (uintptr_t)MOCK_ADDR; \
        __builtin___clear_cache(ADDR, ADDR + CODESIZE); \
    } while (0)
    #define SWAP_ADDR_NEAR(ADDR, MOCK_ADDR) SWAP_ADDR_FAR(ADDR, MOCK_ADDR)
#else
    #define CODESIZE 13U
    #define CODESIZE_MIN 5U
    #define CODESIZE_MAX CODESIZE

    #define SWAP_ADDR_FAR(ADDR, MOCK_ADDR) \
    do { \
        *ADDR = 0x49; \
        *(ADDR + 1) = 0xbb; \
        *(long long *)(ADDR + 2) = (uintptr_t)MOCK_ADDR; \
        *(ADDR + 10) = 0x41; \
        *(ADDR + 11) = 0xff; \
        *(ADDR + 12) = 0xe3; \
    } while (0)

    #define SWAP_ADDR_NEAR(ADDR, MOCK_ADDR) \
    do { \
        *ADDR = 0xE9; \
        *(int *)(ADDR + 1) = (int)(MOCK_ADDR - ADDR - CODESIZE_MIN); \
    } while (0)
#endif

typedef void (*MOCK_CREATE)(void *, void *);
typedef void (*MOCK_DELETE)(void *);
typedef uint8_t *FuncAddr;

typedef struct stFuncInfo {
    struct stFuncInfo *next;
    FuncAddr addr;
    uint8_t body[CODESIZE_MAX];
    bool farJmp;
} SrcFuncInfo;

void KTestMockCreate(void *func, void *mockFunc);
void KTestMockDelete(void *func);

#ifdef __cplusplus
template<typename T>
FuncAddr GetAddr(T func)
{
    return reinterpret_cast<FuncAddr>(func);
}
    class KTestMock {
    public:
        KTestMock(){};
        ~KTestMock(){};
        template<typename T, typename S>
        void Create(T func, S mockFunc)
        {
            KTestMockCreate(GetAddr(func), GetAddr(mockFunc));
        }
        template<typename T>
        void Delete(T func)
        {
            KTestMockDelete(GetAddr(func));
        }
        SrcFuncInfo *funcInfo = NULL;
    };
#else
static FuncAddr GetAddr(void *func)
{
    return (FuncAddr)func;
}
typedef struct {
    MOCK_CREATE Create;
    MOCK_DELETE Delete;
    SrcFuncInfo *funcInfo;
} KTestMock;
#endif

KTestMock *CreateMock();
void DeleteMock(KTestMock *ktestMock);

#define TEST_DefFuncRetPositive(n) \
static inline int32_t TEST_FuncRet##n() \
{                          \
    return n;                           \
}\

#define TEST_DefFuncRetNegative(n) \
static inline int32_t TEST_FuncRetNegative##n() \
{                          \
    return -n;                           \
}                                  \

TEST_DefFuncRetPositive(0)
TEST_DefFuncRetPositive(1)
TEST_DefFuncRetPositive(2)
TEST_DefFuncRetNegative(1)
TEST_DefFuncRetNegative(2)

#define TEST_GetFuncRetPositive(n) TEST_FuncRet##n
#define TEST_GetFuncRetNegative(n) TEST_FuncRetNegative##n

#endif