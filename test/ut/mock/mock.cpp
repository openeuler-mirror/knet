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

#include "mock.h"

static void *g_NowMock = NULL;
static long g_pagesize = 0;

static bool IsAddressFar(FuncAddr addr, FuncAddr mockAddr)
{
    uintptr_t distance = (mockAddr >= addr) ? (mockAddr - addr) : (addr - mockAddr);
    return (sizeof(FuncAddr) > 4) && (((distance >> 31) - 1) > 0); /* 右移4字节, 右移31字节获取地址头 */
}

static void StoreFunc(SrcFuncInfo *funcInfo)
{
    size_t size = funcInfo->farJmp ? CODESIZE_MAX : CODESIZE_MIN;
    for (int i = 0; i < size; i++) {
        funcInfo->body[i] = funcInfo->addr[i];
    }
}

static void RestoreFunc(SrcFuncInfo *funcInfo)
{
    size_t size = funcInfo->farJmp ? CODESIZE_MAX : CODESIZE_MIN;
    for (int i = 0; i < size; i++) {
        funcInfo->addr[i] = funcInfo->body[i];
    }
    __builtin___clear_cache(funcInfo->addr, funcInfo->addr + size);
}

static void SwapAddr(bool farJmp, FuncAddr addr, FuncAddr mockAddr)
{
    if (farJmp) {
        SWAP_ADDR_FAR(addr, mockAddr);
    } else {
        SWAP_ADDR_NEAR(addr, mockAddr);
    }
}

static FuncAddr GetPage(FuncAddr addr)
{
    return (FuncAddr)((uintptr_t)addr & ~(g_pagesize - 1));
}

#define PAGE_NUM 2
void KTestMockCreate(void *func, void *mockFunc)
{
    FuncAddr addr = GetAddr(func);
    FuncAddr mockAddr = GetAddr(mockFunc);
    SrcFuncInfo *funcInfo = (SrcFuncInfo *)malloc(sizeof(SrcFuncInfo));
    if (funcInfo == NULL) {
        return;
    }
    funcInfo->addr = addr;
    funcInfo->farJmp = IsAddressFar(addr, mockAddr);
    funcInfo->next = NULL;
    StoreFunc(funcInfo);
    if (mprotect(GetPage(funcInfo->addr), g_pagesize * PAGE_NUM, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        printf("mprotect failed, errno:%d\r\n", errno);
        return;
    }
    SwapAddr(funcInfo->farJmp, addr, mockAddr);
    if (mprotect(GetPage(funcInfo->addr), g_pagesize * PAGE_NUM, PROT_READ | PROT_EXEC) != 0) {
        printf("mprotect failed, errno:%d\r\n", errno);
        return;
    }
    KTestMock *mock = (KTestMock *)g_NowMock;
    SrcFuncInfo *preFuncInfo = mock->funcInfo;
    if (mock->funcInfo == NULL) {
        mock->funcInfo = funcInfo;
        return;
    }
    while (preFuncInfo->next != NULL) {
        preFuncInfo = preFuncInfo->next;
    }
    preFuncInfo->next = funcInfo;
}

void KTestMockDeleteFunc(SrcFuncInfo *funcInfo)
{
    if (funcInfo == NULL) {
        printf("funcInfo is NULL\n");
        return;
    }
    if (mprotect(GetPage(funcInfo->addr), g_pagesize * PAGE_NUM, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        printf("mprotect failed, errno:%d\r\n", errno);
        return;
    }
    RestoreFunc(funcInfo);
    if (mprotect(GetPage(funcInfo->addr), g_pagesize * PAGE_NUM, PROT_READ | PROT_EXEC) != 0) {
        printf("mprotect failed, errno:%d\r\n", errno);
        return;
    }
    free(funcInfo);
    return;
}

void KTestMockDelete(void *func)
{
    KTestMock *mock = (KTestMock *)g_NowMock;
    FuncAddr addr = GetAddr(func);
    SrcFuncInfo *preFuncInfo = mock->funcInfo;
    SrcFuncInfo *funcInfo = NULL;
    if (preFuncInfo->addr == addr) {
        funcInfo = preFuncInfo;
        mock->funcInfo = preFuncInfo->next;
    } else {
        while (preFuncInfo->next) {
            if (preFuncInfo->next->addr == addr) {
                funcInfo = preFuncInfo->next;
                preFuncInfo->next = preFuncInfo->next->next;
                break;
            }
            preFuncInfo = preFuncInfo->next;
        }
    }
    KTestMockDeleteFunc(funcInfo);
}

KTestMock *CreateMock()
{
    g_pagesize = sysconf(_SC_PAGE_SIZE);
    if (g_pagesize <= 0) {
        g_pagesize = 4096; /* 默认page大小为4096 */
    }

#ifdef __cplusplus
    if (g_NowMock != NULL) {
        delete(g_NowMock);
    }
    g_NowMock = new KTestMock();
#else
    if (g_NowMock != NULL) {
        free(g_NowMock);
    }
    KTestMock *mock = (KTestMock *)malloc(sizeof(KTestMock));
    mock->Create = KTestMockCreate;
    mock->Delete = KTestMockDelete;
    mock->funcInfo = NULL;
    g_NowMock = mock;
#endif
    return (KTestMock *)g_NowMock;
}

void DeleteMock(KTestMock *ktestMock)
{
    if (ktestMock == NULL || ktestMock != g_NowMock) {
        return;
    }
    KTestMock *mock = (KTestMock *)g_NowMock;
    SrcFuncInfo *nowFuncInfo = mock->funcInfo;
    while (nowFuncInfo) {
        SrcFuncInfo *nextFuncInfo = nowFuncInfo->next;
        KTestMockDeleteFunc(nowFuncInfo);
        nowFuncInfo = nextFuncInfo;
    }
    if (mock != NULL) {
#ifdef __cplusplus
        delete(mock);
#else
        free(mock);
#endif
        mock = NULL;
        g_NowMock = mock;
    }
}