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
#include "netdev.h"

#include "worker.h"
#include "shm.h"
#include "utils_base.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_spinlock.h"

#include "dev.h"

typedef struct {
    LIST_HEAD(, NetdevQue) rxQues;
    LIST_HEAD(, NetdevQue) txQues;

    LIST_HEAD(, NetdevQue) rxBacklogQues;
    LIST_HEAD(, NetdevQue) txBacklogQues;
    Spinlock_t lock;

    int rxQueCnt;
} DevTask_t;

typedef struct {
    uint16_t cnt;
    Spinlock_t lock;
    DevTask_t* task[0];
} DevTaskMgr_t;

static DevTaskMgr_t* g_devTaskMgr;

static void DevClearQue(NetdevQue_t* que)
{
    Pbuf_t* pbuf = RING_Pop(&que->cached);
    while (pbuf != NULL) {
        PBUF_Free(pbuf);
        pbuf = RING_Pop(&que->cached);
    }
    NETDEV_PutDev(que->dev);
}

static uint8_t QueMapSet(NetdevQue_t* head, int ifIndex, uint32_t* queMap, uint32_t mapCnt)
{
    NetdevQue_t* que;
    uint8_t queCnt = 0;
    for (que = head; que != NULL; que = que->node.next) {
        if (que->dev->ifindex == ifIndex) {
            uint32_t index = que->queid >> 5; // 5代表除以32
            uint32_t offset = que->queid & ((1 << 5) - 1); // 左移5位为32
            if (index < mapCnt) {
                queMap[index] |= (uint32_t)1 << offset;
            }
            queCnt++;
        }
    }
    return queCnt;
}

uint8_t NETDEV_TaskQueMapGet(int wid, int ifIndex, uint32_t* queMap, uint32_t mapCnt)
{
    DevTask_t* task = g_devTaskMgr->task[wid];
    uint8_t queCnt = QueMapSet(task->rxQues.first, ifIndex, queMap, mapCnt);

    if (LIST_IS_EMPTY(&task->rxBacklogQues)) {
        return queCnt;
    }
    SPINLOCK_Lock(&g_devTaskMgr->lock);
    queCnt += QueMapSet(task->rxBacklogQues.first, ifIndex, queMap, mapCnt);
    SPINLOCK_Unlock(&g_devTaskMgr->lock);
    return queCnt;
}

void DevTask(int wid)
{
    DevTask_t*    task;
    NetdevQue_t* rxQue;
    NetdevQue_t* txQue;
    DevTaskMgr_t* mgr = g_devTaskMgr;

    task = mgr->task[wid];

    if (!LIST_IS_EMPTY(&task->rxBacklogQues)) {
        SPINLOCK_Lock(&mgr->lock);
        // LIST_CONCAT内会清空rxBacklogQues，无需手动清空
        LIST_CONCAT(&task->rxQues, &task->rxBacklogQues, node);
        LIST_CONCAT(&task->txQues, &task->txBacklogQues, node);

        SPINLOCK_Unlock(&mgr->lock);
    }

    rxQue = LIST_FIRST(&task->rxQues);

    while (rxQue != NULL) {
        if ((rxQue->dev->ifflags & DP_IFF_RUNNING) == 0) {
            NetdevQue_t* rxRemove = rxQue;

            rxQue = LIST_NEXT(rxRemove, node);
            LIST_REMOVE(&task->rxQues, rxRemove, node);
            DevClearQue(rxRemove);
            continue;
        }

        DoRcvPkts(rxQue);

        rxQue = LIST_NEXT(rxQue, node);
    }

    txQue = LIST_FIRST(&task->txQues);

    while (txQue != NULL) {
        if ((txQue->dev->ifflags & DP_IFF_RUNNING) == 0) {
            NetdevQue_t* txRemove = txQue;

            txQue = LIST_NEXT(txRemove, node);
            LIST_REMOVE(&task->txQues, txRemove, node);
            DevClearQue(txRemove);
            continue;
        }

        if (RING_IsEmpty(&txQue->cached) == 0) {
            XmitCached(txQue);
        }

        txQue = LIST_NEXT(txQue, node);
    }
}

static void DevAddRxQue(NetdevQue_t* rxQue, NetdevQue_t* txQue)
{
    DevTaskMgr_t* mgr = g_devTaskMgr;
    DevTask_t*    task;
    uint16_t      idx = 0;

    int minRxqueCnt = mgr->task[0]->rxQueCnt;
    for (uint16_t i = 1; i < mgr->cnt; i++) {
        task = mgr->task[i];
        if (minRxqueCnt > task->rxQueCnt) {
            idx = i;
            minRxqueCnt = task->rxQueCnt;
        }
    }

    task = mgr->task[idx];

    SPINLOCK_Lock(&mgr->lock);

    rxQue->wid = idx;
    txQue->wid = idx;

    LIST_INSERT_TAIL(&task->rxBacklogQues, rxQue, node);
    if (txQue != NULL) {
        LIST_INSERT_TAIL(&task->txBacklogQues, txQue, node);
    }
    task->rxQueCnt++;

    (void)NETDEV_RefDev(rxQue->dev);
    (void)NETDEV_RefDev(txQue->dev);
    SPINLOCK_Unlock(&mgr->lock);
}

// dev start 和 dev stop需要在nslock范围内操作
void DevStart(Netdev_t* dev)
{
    dev->ifflags |= (DP_IFF_RUNNING | DP_IFF_UP);

    for (int i = 0; i < dev->rxQueCnt; i++) {
        DevAddRxQue(&dev->rxQues[i], &dev->txQues[i]);
    }
}

void DevStop(Netdev_t* dev)
{
    dev->ifflags &= ~(DP_IFF_RUNNING | DP_IFF_UP);
}

int InitDevTasks(int slave)
{
    DevTask_t*           task;
    size_t               taskCnt;
    size_t               taskSize = sizeof(*task);
    size_t               allocSize;
    static WORKER_Work_t devTask = {
        .type         = WORKER_WORK_TYPE_FIX,
        .task.fixWork = DevTask,
    };

    SHM_REG("devtaskmgr", g_devTaskMgr);

    if (slave != 0) {
        return 0;
    }

    if (g_devTaskMgr != NULL) {
        return -1;
    }

    taskCnt   = (size_t)CFG_GET_VAL(DP_CFG_WORKER_MAX); // 当前仅支持配置1个worker
    allocSize = sizeof(DevTaskMgr_t) + taskCnt * sizeof(DevTask_t*);
    allocSize += taskCnt * taskSize;

    /* 在该函数中全字段赋值，无需初始化 */
    g_devTaskMgr = SHM_MALLOC(allocSize, MOD_NETDEV, DP_MEM_FIX);
    if (g_devTaskMgr == NULL) {
        DP_LOG_ERR("Malloc memory failed for devTask manager.");
        return -1;
    }

    g_devTaskMgr->cnt  = (uint16_t)taskCnt;
    task = (DevTask_t*)((uint8_t*)(g_devTaskMgr) + taskCnt * sizeof(DevTask_t*) + sizeof(DevTaskMgr_t));

    for (int i = 0; i < g_devTaskMgr->cnt; i++) {
        g_devTaskMgr->task[i] = task;

        LIST_INIT_HEAD(&task->rxQues);
        LIST_INIT_HEAD(&task->txQues);
        LIST_INIT_HEAD(&task->rxBacklogQues);
        LIST_INIT_HEAD(&task->txBacklogQues);

        (void)SPINLOCK_Init(&task->lock);
        task->rxQueCnt = 0;
        task = (DevTask_t*)((uint8_t*)task + taskSize);
    }

    (void)SPINLOCK_Init(&g_devTaskMgr->lock);

    WORKER_SET_BIT_MAP_ALL(&devTask.map);
    WORKER_AddWork(&devTask);

    return 0;
}

void DeinitDevTasks(int slave)
{
    if (slave != 0) {
        return;
    }

    if (g_devTaskMgr == NULL) {
        return;
    }

    for (int i = 0; i < g_devTaskMgr->cnt; i++) {
        SPINLOCK_Deinit(&g_devTaskMgr->task[i]->lock);
    }
    SPINLOCK_Deinit(&g_devTaskMgr->lock);

    SHM_FREE(g_devTaskMgr, DP_MEM_FIX);
    g_devTaskMgr = NULL;
}

void CleanDevTasksQue()
{
    if (g_devTaskMgr == NULL) {
        return;
    }

    for (int i = 0; i < g_devTaskMgr->cnt; i++) {
        LIST_INIT_HEAD(&g_devTaskMgr->task[i]->rxQues);
        LIST_INIT_HEAD(&g_devTaskMgr->task[i]->txQues);
        LIST_INIT_HEAD(&g_devTaskMgr->task[i]->rxBacklogQues);
        LIST_INIT_HEAD(&g_devTaskMgr->task[i]->txBacklogQues);
    }
}
