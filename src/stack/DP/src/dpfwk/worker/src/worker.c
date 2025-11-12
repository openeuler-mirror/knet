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

#include <stdbool.h>
#include <securec.h>

#include "utils_base.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_debug.h"

#include "worker.h"

typedef struct WORKER_Work Work_t;

typedef struct TimerWorkList {
    uint32_t              expiredTick;
    uint32_t              internalTick;
    WORKER_TimerCb_t      cb;
    struct TimerWorkList* next;
} TimerWorkList_t;

typedef struct FixWorkList {
    WORKER_FixWorkCb_t  cb;
    struct FixWorkList* next;
} FixWorkList_t;

typedef struct DP_Worker {
    uint16_t         wid;
    uint16_t         flags;
    uint16_t         fixWorkCnt;
    uint16_t         timerWorkCnt;
    TimerWorkList_t* timerWorks;
    FixWorkList_t*   fixWorks;

    uint32_t nextTime;
    uint32_t tick;
    uint32_t time;
    volatile bool isStopped;

    DP_Sem_t sem;
} Worker_t;

typedef struct {
    int        workerCnt;
    Worker_t** workers;
} WorkerMgr_t;

static WORKER_Work_t* g_works = NULL;
static WorkerMgr_t    g_workerMgr = { 0 };

void WORKER_ClearWork(void)
{
    WORKER_Work_t* work;
    while (g_works != NULL) {
        work = g_works->next;
        g_works->next = NULL;
        g_works = work;
    }
}

void WORKER_AddWork(WORKER_Work_t* work)
{
    if (g_works == NULL) {
        g_works = work;
    } else {
        work->next = g_works;
        g_works    = work;
    }
}

static inline uint32_t GetTime(Worker_t* worker)
{
    return worker->time;
}

static inline void UpdateTime(Worker_t* worker)
{
    worker->time = UTILS_TimeNow();
}

static TimerWorkList_t* AdjustTimers(TimerWorkList_t* first)
{
    TimerWorkList_t* next = first->next;
    TimerWorkList_t *cur = NULL;
    TimerWorkList_t *prev = NULL;

    if (next == NULL) {
        return first;
    }

    if (TIME_CMP(next->expiredTick, first->expiredTick) >= 0) {
        return first;
    }

    cur   = first;
    first = next;

    while (next != NULL && TIME_CMP(cur->expiredTick, next->expiredTick) > 0) {
        prev = next;
        next = next->next;
    }

    ASSERT(prev != NULL);

    prev->next = cur;
    cur->next  = next;

    return first;
}

static void AllocWorkerList(Worker_t *worker, TimerWorkList_t *twl, FixWorkList_t *fwl)
{
    Work_t *work = g_works;
    int wid = worker->wid;
    while (work != NULL) {
        if (WORKER_IS_IN_BIT_MAP(&work->map, wid) == 0) {
            work = work->next;
            continue;
        }

        switch (work->type) {
            case WORKER_WORK_TYPE_TIMER:
                twl->expiredTick   = worker->tick + work->task.timerWork.internalTick;
                twl->internalTick  = work->task.timerWork.internalTick;
                twl->cb            = work->task.timerWork.timerCb;
                if (work->task.timerWork.initCb != NULL) {
                    work->task.timerWork.initCb(wid, worker->tick);
                }
                twl->next          = worker->timerWorks;
                worker->timerWorks = twl;
                worker->timerWorks = AdjustTimers(worker->timerWorks);
                twl++;

                break;
            default:
                fwl->cb          = work->task.fixWork;
                fwl->next        = worker->fixWorks;
                worker->fixWorks = fwl;
                fwl++;
                break;
        }

        work = work->next;
    }
}

static Worker_t* AllocWorker(uint16_t wid)
{
    Worker_t*        worker = NULL;
    uint16_t         fixWorkCnt   = 0;
    uint16_t         timerWorkCnt = 0;
    Work_t*          work = NULL;
    size_t           allocSize = 0;
    TimerWorkList_t* twl = NULL;
    FixWorkList_t*   fwl = NULL;

    // 统计fixWork和timerWork数量
    work = g_works;
    while (work != NULL) {
        if (WORKER_IS_IN_BIT_MAP(&work->map, wid) == 0) {
            work = work->next;
            continue;
        }

        switch (work->type) {
            case WORKER_WORK_TYPE_TIMER:
                timerWorkCnt++;
                break;
            default:
                fixWorkCnt++;
                break;
        }

        work = work->next;
    }

    allocSize = sizeof(Worker_t) + SEM_Size;
    allocSize += timerWorkCnt * sizeof(TimerWorkList_t);
    allocSize += fixWorkCnt * sizeof(FixWorkList_t);
    worker = MEM_MALLOC_ALIGN(allocSize, CACHE_LINE, MOD_WORKER, DP_MEM_FIX);
    if (worker == NULL) {
        return NULL;
    }
    (void)memset_s(worker, allocSize, 0, allocSize);

    worker->sem = (DP_Sem_t)(worker + 1);
    twl         = (TimerWorkList_t*)((uint8_t*)(worker->sem) + SEM_Size);
    fwl         = (FixWorkList_t*)(twl + timerWorkCnt);

    worker->wid          = wid;
    worker->timerWorkCnt = timerWorkCnt;
    worker->fixWorkCnt   = fixWorkCnt;
    worker->nextTime     = UTILS_TimeNow() + WORKER_MS_PER_TICK;
    worker->tick         = RAND_GEN();
    worker->flags        = WORKER_FLAGS_EVENT;

    if (SEM_INIT(worker->sem) != 0) {
        MEM_FREE(worker, DP_MEM_FIX);
        return NULL;
    }

    AllocWorkerList(worker, twl, fwl);

    UpdateTime(worker);

    return worker;
}

static void FreeWorker(Worker_t* worker)
{
    if (worker == NULL) {
        return;
    }

    SEM_DEINIT(worker->sem);
    MEM_FREE(worker, DP_MEM_FIX);
}

static void FreeWorkers(Worker_t** workers, int cnt)
{
    if (workers == NULL) {
        return;
    }

    for (int i = 0; i < cnt; i++) {
        FreeWorker(workers[i]);
    }
    MEM_FREE(workers, DP_MEM_FIX);
}

int WORKER_Init(void)
{
    int        wid;
    Worker_t** workers;
    size_t     workersSize;

    if (g_workerMgr.workers != NULL) {
        return -1;
    }

    g_workerMgr.workerCnt = CFG_GET_VAL(DP_CFG_WORKER_MAX);
    workersSize = sizeof(Worker_t*) * g_workerMgr.workerCnt;

    workers = MEM_MALLOC(workersSize, MOD_WORKER, DP_MEM_FIX);
    if (workers == NULL) {
        DP_LOG_ERR("Malloc memory failed for worker.");
        return -1;
    }
    (void)memset_s(workers, workersSize, 0, workersSize);

    for (wid = 0; wid < g_workerMgr.workerCnt; wid++) {
        workers[wid] = AllocWorker((uint16_t)wid); // wid不会为负，强转无风险
        if (workers[wid] == NULL) {
            break;
        }
    }

    if (wid == g_workerMgr.workerCnt) {
        g_workerMgr.workers = workers;
        return 0;
    }

    FreeWorkers(workers, wid);

    return -1;
}

void WORKER_Deinit(void)
{
    WORKER_ClearWork();
    FreeWorkers(g_workerMgr.workers, g_workerMgr.workerCnt);
    g_workerMgr.workers = NULL;
    g_workerMgr.workerCnt = 0;
}

uint32_t WORKER_GetTime(void)
{
    return GetTime(g_workerMgr.workers[0]);
}

uint32_t WORKER_GetTick(int wid)
{
    Worker_t* worker;

    if (wid < 0 || wid >= g_workerMgr.workerCnt) {
        return 0;
    }

    worker = g_workerMgr.workers[wid];

    ASSERT(worker != NULL);
    return worker->tick;
}

static void WalkTimers(Worker_t* worker, uint32_t now)
{
    TimerWorkList_t* twl = worker->timerWorks;

    if (twl == NULL) {
        return;
    }

    while (TIME_CMP(now, worker->nextTime) >= 0) {
        worker->nextTime += WORKER_MS_PER_TICK;
        worker->tick++;

        while (TIME_CMP(worker->tick, twl->expiredTick) >= 0) {
            twl->cb(worker->wid, worker->tick);
            twl->expiredTick = worker->tick + twl->internalTick;

            worker->timerWorks = AdjustTimers(worker->timerWorks);
            twl = worker->timerWorks;
        }
    }

    worker->timerWorks = twl;
}

static void RunWorkerOnce(Worker_t* worker)
{
    uint32_t now;
    FixWorkList_t* fwl = worker->fixWorks;

    UpdateTime(worker);

    // 定时器处理
    now = GetTime(worker);
    if (TIME_CMP(now, worker->nextTime) >= 0) {
        WalkTimers(worker, now);
    }

    // 固定任务
    while (fwl != NULL) {
        fwl->cb(worker->wid);
        fwl = fwl->next;
    }
}

void DP_RunWorker(int wid)
{
    Worker_t* worker;

    if (wid < 0 || wid >= g_workerMgr.workerCnt) {
        return;
    }

    worker = g_workerMgr.workers[wid];
    ASSERT(worker != NULL);
    worker->isStopped = false;

    while (!worker->isStopped) {
        if ((worker->flags & WORKER_FLAGS_EVENT) != 0) {
            SEM_WAIT(worker->sem, WORKER_MS_PER_TICK);
        }

        RunWorkerOnce(worker);
    }
}

void DP_StopWorker(int wid)
{
    Worker_t* worker;

    if (wid < 0 || wid >= g_workerMgr.workerCnt) {
        return;
    }

    worker = g_workerMgr.workers[wid];
    ASSERT(worker != NULL);
    worker->isStopped = true;
}

void DP_WakeupWorker(int wid)
{
    Worker_t* worker;

    if (wid < 0 || wid >= g_workerMgr.workerCnt) {
        return;
    }

    worker = g_workerMgr.workers[wid];

    ASSERT(worker != NULL);

    if ((worker->flags & WORKER_FLAGS_EVENT) != 0) {
        SEM_SIGNAL(worker->sem);
    }
}

void DP_RunWorkerOnce(int wid)
{
    Worker_t* worker;

    if (UTILS_IsCfgInited() != 1) {
        DP_LOG_ERR("Do not run worker before dp init!");
        return;
    }

    if (wid < 0 || wid >= g_workerMgr.workerCnt) {
        DP_LOG_ERR("Run worker failed, wid is invalid!");
        return;
    }

    worker = g_workerMgr.workers[wid];
    if (worker == NULL) {
        DP_LOG_ERR("Run worker failed, worker is NULL!");
        return;
    }

    RunWorkerOnce(worker);
}

static DP_WorkerGetSelfIdHook g_workerGetSelf = NULL;

int DP_RegGetSelfWorkerIdHook(DP_WorkerGetSelfIdHook getSelf)
{
    if (g_workerGetSelf != NULL) {
        DP_LOG_ERR("Get workerid hookreg failed, reged already!");
        return -1;
    }
    if (getSelf == NULL) {
        DP_LOG_ERR("Get workerid hookreg failed, getSelf func is NULL!");
        return -1;
    }

    g_workerGetSelf = getSelf;
    return 0;
}

uint32_t WORKER_GetSelfId(void)
{
    if (g_workerGetSelf != NULL) {
        return (uint32_t)g_workerGetSelf();
    }
    return 0;
}

void WORKER_DeregGetWorkerId(void)
{
    g_workerGetSelf = NULL;
}