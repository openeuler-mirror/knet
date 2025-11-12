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

#ifndef TBM_UTILS_H
#define TBM_UTILS_H

#include <stddef.h>

#include "dp_hashtbl_api.h"
#include "dp_fib4tbl_api.h"

#ifdef __cplusplus
extern "C" {
#endif

int TbmUtilInit(void);

int TbmUtilDeinit(void);

DP_HashTblHooks_t *GetHashtblHook(void);
void ClearHashtblHook(void);

#define TbmHashtblFuncReged ((GetHashtblHook()->createTable != NULL) ? true : false)
#define TbmHashtblCreate             GetHashtblHook()->createTable
#define TbmHashtblDestroy            GetHashtblHook()->destroyTable
#define TbmHashtblInsertEntry        GetHashtblHook()->insertEntry
#define TbmHashtblModifyEntry        GetHashtblHook()->modifyEntry
#define TbmHashtblLookupEntry        GetHashtblHook()->lookupEntry
#define TbmHashtblDelEntry           GetHashtblHook()->delEntry
#define TbmHashtblGetInfo            GetHashtblHook()->getInfo
#define TbmHashtblEntryGetFirst      GetHashtblHook()->hashtblEntryGetFirst
#define TbmHashtblEntryGetNext       GetHashtblHook()->hashtblEntryGetNext

DP_Fib4TblHooks_t *GetFib4tblHook(void);
void ClearFib4tblHook(void);

#define TbmFib4tblFuncReged ((GetFib4tblHook()->createTable != NULL) ? true : false)
#define TbmFib4Create                GetFib4tblHook()->createTable
#define TbmFib4Destroy               GetFib4tblHook()->destroyTable
#define TbmFib4InsertEntry           GetFib4tblHook()->insertEntry
#define TbmFib4ModifyEntry           GetFib4tblHook()->modifyEntry
#define TbmFib4DelEntry              GetFib4tblHook()->delEntry
#define TbmFib4GetInfo               GetFib4tblHook()->getInfo
#define TbmFib4ExactMatchEntry       GetFib4tblHook()->exactMatchEntry
#define TbmFib4LmpEntry              GetFib4tblHook()->lmpEntry
#define TbmFib4EntryGetFirst         GetFib4tblHook()->fib4EntryGetFirst
#define TbmFib4EntryGetNext          GetFib4tblHook()->fib4EntryGetNext

typedef void (*NodeFreeHook)(void*);
typedef uint32_t (*NodeWalkerHook)(void* para, int32_t id, void *out);

void *MemPoolCreate(size_t count, size_t itemSize, NodeFreeHook freeHook);
void MemPoolDestroy(void *pool);

int32_t MemNodeAlloc(void *pool);
void MemNodeFree(void *pool, int32_t id);

void* MemNodeGetPointer(void *pool, int32_t id);
uint32_t MemNodeSetPointer(void *pool, int32_t id, void* pointer);

uint32_t MemPoolWalker(void *pool, NodeWalkerHook nodeWalker, void *out);

#ifdef __cplusplus
}
#endif
#endif
