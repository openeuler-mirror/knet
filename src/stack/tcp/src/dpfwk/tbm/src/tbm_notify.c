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
#include "dp_errno.h"

#include "shm.h"
#include "tbm.h"
#include "utils_log.h"

typedef struct {
    LIST_HEAD(, TBM_Notify) mcLists;

    Spinlock_t lock;
} TbmMc_t;

void* AllocNotifyList(void)
{
    size_t   objSize;
    TbmMc_t* tm;

    objSize = sizeof(*tm);

    /* 在该函数中全字段赋值，无需初始化 */
    tm = SHM_MALLOC(objSize, MOD_TBM, DP_MEM_FIX);
    if (tm == NULL) {
        DP_LOG_ERR("Malloc memory failed for tbm.");
        return NULL;
    }

    LIST_INIT_HEAD(&tm->mcLists);
    if (SPINLOCK_Init(&tm->lock) != 0) {
        SHM_FREE(tm, DP_MEM_FIX);
        return NULL;
    }

    return tm;
}

void FreeNotifyList(void* ptr)
{
    SHM_FREE(ptr, DP_MEM_FIX);
}

int TBM_AddNotify(NS_Net_t* net, TBM_Notify_t* tn)
{
    TbmMc_t*      tm = NS_GET_NL_TBL(net);
    TBM_Notify_t* tmp;
    int           ret = 0;

    if (tn == NULL || tn->groups == 0 || tn->cb == NULL) {
        return -EINVAL;
    }

    SPINLOCK_Lock(&tm->lock);
    SPINLOCK_Lock(&tn->lock);

    LIST_FOREACH(&tm->mcLists, tmp, node)
    {
        if (tmp->pid == tn->pid) { // 不可绑定相同pid
            ret = -EEXIST;
            break;
        }
    }

    if (ret == 0) {
        LIST_INSERT_TAIL(&tm->mcLists, tn, node);
    }

    SPINLOCK_Unlock(&tn->lock);
    SPINLOCK_Unlock(&tm->lock);

    return ret;
}

void TBM_DelNotify(NS_Net_t* net, TBM_Notify_t* tn)
{
    TbmMc_t* tm = NS_GET_NL_TBL(net);

    if (tn == NULL || tn->groups == 0) {
        return;
    }

    SPINLOCK_Lock(&tm->lock);

    SPINLOCK_Lock(&tn->lock);
    LIST_REMOVE(&tm->mcLists, tn, node);
    SPINLOCK_Unlock(&tn->lock);

    SPINLOCK_Unlock(&tm->lock);
}

void TBM_Notify(NS_Net_t* net, int type, int op, uint8_t family, void* item)
{
    TbmMc_t*      tm = NS_GET_NL_TBL(net);
    TBM_Notify_t* tn = NULL;

    LIST_FOREACH(&tm->mcLists, tn, node)
    {
        SPINLOCK_Lock(&tn->lock);
        if ((tn->groups & (uint32_t)type) != 0) {
            tn->cb(tn, type, op, family, item);
        }
        SPINLOCK_Unlock(&tn->lock);
    }
}
