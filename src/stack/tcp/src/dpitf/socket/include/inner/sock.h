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

#ifndef SOCK_H
#define SOCK_H

#include "dp_socket_types_api.h"

#include "dp_errno.h"

#include "pbuf.h"
#include "ns.h"
#include "netdev.h"
#include "utils_base.h"
#include "utils_log.h"
#include "utils_spinlock.h"

#include "sock_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_IOV_CNT 1024 // 参考内核 iovCnt最大不能超过1024

#define DP_MSG_RECV_SUPPORT_FLAGS (DP_MSG_DONTWAIT | DP_MSG_PEEK)

typedef struct Sock Sock_t;

typedef int (*SOCK_CreateSkFn_t)(NS_Net_t* net, int type, int protocol, Sock_t** sk);

typedef struct {
    int               type;
    int               protocol;
    SOCK_CreateSkFn_t create;
} SOCK_ProtoOps_t;

typedef struct {
    int family;
    void (*add)(const SOCK_ProtoOps_t* ops);
    int (*lookup)(int type, int proto, SOCK_CreateSkFn_t* fn);
} SOCK_FamilyOps_t;

// sock内部存储ops，使用者可以使用局部变量
void SOCK_AddFamilyOps(const SOCK_FamilyOps_t* ops);
void SOCK_AddProto(int family, const SOCK_ProtoOps_t* ops);

// 以下接口为声明，由个协议实现，协议裁剪上，在sock.c中通过模块宏实现
int INET_Init(int slave);
void INET_Deinit(int slave);
int SOCK_InitInet6(int slave);
int SOCK_InitNetlink(int slave);

typedef struct {
    int type;
    int protocol;

    int (*shutdown)(Sock_t* sk, int how);
    int (*close)(Sock_t* sk);
    int (*bind)(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);
    int (*listen)(Sock_t* sk, int backlog);
    int (*accept)(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, Sock_t** newSk);
    int (*connect)(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);
    int (*setsockopt)(Sock_t* sk, int level, int optName, const void* optVal, DP_Socklen_t optLen);
    int (*getsockopt)(Sock_t* sk, int level, int optName, void* optVal, DP_Socklen_t* optLen);
    int (*keepalive)(Sock_t* sk, int enable);
    ssize_t (*sendmsg)(Sock_t* sk, const struct DP_Msghdr* msg, int flags,
                       size_t msgDataLen, size_t* index, size_t* offset);
    ssize_t (*recvmsg)(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen);

    int (*getDstAddr)(Sock_t* sk, Pbuf_t* pbuf, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen);
    int (*getAddr)(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, int peer);
} SOCK_Ops_t;

typedef LIST_HEAD(, Sock) SOCK_SkList_t;

#define SOCK_STATE_RW (SOCK_STATE_READ | SOCK_STATE_WRITE | SOCK_STATE_EXCEPTION)

struct Sock {
    union {
        LIST_ENTRY(Sock) node;
        HASH_Node_t hashNode;
    };

    atomic32_t ref;    // 引用计数
    Spinlock_t lock; // Sock 锁，解决协议栈线程和用户线程共享 Sock 资源的冲突

    const SOCK_Ops_t* ops;

    NS_Net_t* net;

    void* file;

    union {
        struct {
            uint16_t lingerOnoff : 1; // 可以通过DP_CFG_SOCK_LINGER_DFT配置
            uint16_t nonblock : 1;
            uint16_t reuseAddr : 1;
            uint16_t reusePort : 1;
            uint16_t broadcast : 1;
            uint16_t keepalive : 1;
            uint16_t bindDev : 1; // 绑定了vif
            uint16_t dontRoute : 1;
        };
        uint16_t options;
    };

    uint16_t error;
    uint16_t family;

    int linger; // close/shutdown后多久(秒)在后台关闭socket

    uint16_t flags; // SOCK flags标记
    uint8_t  state; // rw state

    int16_t  queid;
    uint16_t vrfId;

    uint16_t rdSemCnt;
    uint16_t wrSemCnt;

    Netdev_t* dev;

    DP_Sem_t rdSem;
    DP_Sem_t wrSem;

    int rcvTimeout;
    int sndTimeout;

    PBUF_Chain_t sndBuf;
    PBUF_Chain_t rcvBuf;

    uint32_t sndLowat;
    uint32_t sndHiwat;
    uint32_t rcvLowat;
    uint32_t rcvHiwat;

    int   associateFd; // 关联FD
    void* notifyCtx;
    int   notifyType; // 回调函数类型
};

#define SOCK_ALIGN_SIZE(n)            ALIGNED((n), sizeof(void*))
#define SOCK_NEXT_PTR(ptr, n)         ((uint8_t*)(ptr) + (n))
#define SOCK_NEXT_PTR_ALIGNED(ptr, n) ((uint8_t*)(ptr) + SOCK_ALIGN_SIZE(n))

extern size_t SOCK_SIZE;

// socket内存结构
// | -- Sock_t -- | -- obj -- | -- sk->lock -- | -- sk->sem -- | -- sk->sem -- |
// TCP Obj: | -- TcpCb -- | -- InetCb -- |
// TCP6 Obj: | -- TcpCb -- | -- InetCb -- |
static inline size_t SOCK_GetSkSize(size_t objSize)
{
    return objSize + SOCK_ALIGN_SIZE(SEM_Size) * 2; // socket结构包含读写2个信号量字段
}

int SOCK_InitSk(Sock_t* sk, Sock_t* parent, size_t objSize);

void SOCK_DeinitSk(Sock_t* sk);

#define SOCK_FLAGS_CANSEND     0x1
#define SOCK_FLAGS_CANRECV     0x2
#define SOCK_FLAGS_CONNECTING  0x4
#define SOCK_FLAGS_CONNECTED   0x8
#define SOCK_FLAGS_BINDED      0x10
#define SOCK_FLAGS_CONNREFUSED 0x20
#define SOCK_FLAGS_LISTENED    0x40
#define SOCK_FLAGS_CLOSED      0x80
#define SOCK_FLAGS_SHUT_RD     0x100
#define SOCK_FLAGS_SHUT_WR     0x200

#define SOCK_IS_CONNECTING(sk)   (((sk)->flags & SOCK_FLAGS_CONNECTING) != 0)
#define SOCK_IS_CONNECTED(sk)    (((sk)->flags & SOCK_FLAGS_CONNECTED) != 0)
#define SOCK_IS_CONN_REFUSED(sk) (((sk)->flags & SOCK_FLAGS_CONNREFUSED) != 0)
#define SOCK_IS_BINDED(sk)      (((sk)->flags & SOCK_FLAGS_BINDED) != 0)
#define SOCK_IS_LISTENED(sk)    (((sk)->flags & SOCK_FLAGS_LISTENED) != 0)
#define SOCK_IS_CLOSED(sk)      (((sk)->flags & SOCK_FLAGS_CLOSED) != 0)
#define SOCK_IS_SHUTRD(sk)      (((sk)->flags & SOCK_FLAGS_SHUT_RD) != 0)
#define SOCK_IS_SHUTWR(sk)      (((sk)->flags & SOCK_FLAGS_SHUT_WR) != 0)

#define SOCK_CAN_RECV_MORE(sk) (((sk)->flags & SOCK_FLAGS_CANRECV) != 0)
#define SOCK_CAN_SEND_MORE(sk) (((sk)->flags & SOCK_FLAGS_CANSEND) != 0)

#define SOCK_IS_CANT_RECV(sk) (((sk)->flags & SOCK_FLAGS_CANRECV) == 0)

#define SOCK_SET_CONNECTED(sk)                  \
    do {                                       \
        (sk)->flags |= SOCK_FLAGS_CONNECTED;   \
        (sk)->flags &= ~SOCK_FLAGS_CONNECTING; \
    } while (0)
#define SOCK_SET_CONNECTING(sk)   ((sk)->flags |= SOCK_FLAGS_CONNECTING)
#define SOCK_SET_BINDED(sk)       ((sk)->flags |= SOCK_FLAGS_BINDED)
#define SOCK_SET_LISTENED(sk)     ((sk)->flags |= SOCK_FLAGS_LISTENED)
#define SOCK_SET_RECV_MORE(sk)    ((sk)->flags |= SOCK_FLAGS_CANRECV)
#define SOCK_SET_SEND_MORE(sk)    ((sk)->flags |= SOCK_FLAGS_CANSEND)
#define SOCK_SET_CONN_REFUSED(sk) ((sk)->flags |= SOCK_FLAGS_CONNREFUSED)
#define SOCK_SET_CLOSED(sk)       ((sk)->flags |= SOCK_FLAGS_CLOSED)
#define SOCK_SET_SHUTRD(sk)       ((sk)->flags |= SOCK_FLAGS_SHUT_RD)
#define SOCK_SET_SHUTWR(sk)       ((sk)->flags |= SOCK_FLAGS_SHUT_WR)

#define SOCK_CLR_BINDED(sk)    ((sk)->flags &= ~SOCK_FLAGS_BINDED)
#define SOCK_CLR_RECV_MORE(sk) ((sk)->flags &= ~SOCK_FLAGS_CANRECV)
#define SOCK_CLR_SEND_MORE(sk) ((sk)->flags &= ~SOCK_FLAGS_CANSEND)

#define SOCK_CAN_REUSE(sk) ((sk)->reuseAddr != 0 || (sk)->reusePort != 0)

void SOCK_Notify(Sock_t* sk, uint8_t oldState);

static inline void SOCK_SetState(Sock_t* sk, uint8_t state)
{
    uint8_t old = sk->state;
    if (old == state) {
        return;
    }
    sk->state |= state;

    SOCK_Notify(sk, old);
}

static inline void SOCK_UnsetState(Sock_t* sk, uint8_t state)
{
    uint8_t old = sk->state;
    if ((old & state) == 0) {
        return;
    }
    sk->state &= ~state;

    SOCK_Notify(sk, old);
}

#define SOCK_SET_READABLE(sk)  SOCK_SetState((sk), SOCK_STATE_READ)
#define SOCK_SET_WRITABLE(sk)  SOCK_SetState((sk), SOCK_STATE_WRITE)
#define SOCK_SET_EXCEPABLE(sk) SOCK_SetState((sk), SOCK_STATE_EXCEPTION)
#define SOCK_SET_CANTSENDMORE(sk) SOCK_SetState((sk), SOCK_STATE_CANTSENDMORE)
#define SOCK_SET_CANTRCVMORE(sk)  SOCK_SetState((sk), SOCK_STATE_CANTRCVMORE)

#define SOCK_UNSET_READABLE(sk)  SOCK_UnsetState((sk), SOCK_STATE_READ)
#define SOCK_UNSET_WRITABLE(sk)  SOCK_UnsetState((sk), SOCK_STATE_WRITE)
#define SOCK_UNSET_EXCEPABLE(sk) SOCK_UnsetState((sk), SOCK_STATE_EXCEPTION)
#define SOCK_UNSET_CANTSENDMORE(sk) SOCK_UnsetState((sk), SOCK_STATE_CANTSENDMORE)
#define SOCK_UNSET_CANTRCVMORE(sk)  SOCK_UnsetState((sk), SOCK_STATE_CANTRCVMORE)

static inline void SOCK_Lock(Sock_t* sk)
{
    SPINLOCK_Lock(&sk->lock);
}

static inline void SOCK_Unlock(Sock_t* sk)
{
    SPINLOCK_Unlock(&sk->lock);
}

static inline void SOCK_Ref(Sock_t* sk)
{
    ATOMIC32_Inc(&sk->ref);
}

static inline uint32_t SOCK_Deref(Sock_t* sk)
{
    return ATOMIC32_Dec(&sk->ref);
}

static inline uint32_t SOCK_GetRef(Sock_t* sk)
{
    return ATOMIC32_Load(&sk->ref);
}

static inline void SOCK_WakeupRdSem(Sock_t* sk)
{
    if (sk->rdSemCnt > 0) {
        sk->rdSemCnt--;
        SEM_SIGNAL(sk->rdSem);
    }
}

static inline void SOCK_WakeupWrSem(Sock_t* sk)
{
    if (sk->wrSemCnt > 0) {
        sk->wrSemCnt--;
        SEM_SIGNAL(sk->wrSem);
    }
}

int SOCK_PushRcvBufSafe(Sock_t* sk, DP_Pbuf_t* pbuf);

static inline void SOCK_UpdateRcvState(Sock_t* sk)
{
    if (sk->rcvBuf.bufLen >= sk->rcvLowat) {
        SOCK_SetState(sk, SOCK_STATE_READ);
    }

    SOCK_WakeupRdSem(sk);
}

ssize_t SOCK_PopRcvBuf(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen);

ssize_t SOCK_PopRcvBufByPkt(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen);

uint16_t SOCK_PbufAppendMsg(DP_Pbuf_t* pbuf, const struct DP_Msghdr* msg);

int SOCK_Init(int slave);
void SOCK_Deinit(int slave);

ssize_t SOCK_GetMsgIovLen(const struct DP_Msghdr* msg);

static inline ssize_t SOCK_GetMsgDataLen(const struct DP_Msghdr* msg)
{
    if (msg == NULL) {
        return -EFAULT;
    } else if (msg->msg_iovlen == 0) { /* 内核行为如果msg_iovlen为0 不判断msg_iov的正确性 */
        return 0;
    } else if ((msg->msg_iov == NULL)) {
        return -EFAULT;
    } else if ((ssize_t)(msg->msg_iovlen) < 0 || msg->msg_iovlen > MAX_IOV_CNT) {
        return -EMSGSIZE;
    }

    return SOCK_GetMsgIovLen(msg);
}

#ifdef __cplusplus
}
#endif
#endif
