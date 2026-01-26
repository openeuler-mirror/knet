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
#include <securec.h>

#include "pbuf.h"
#include "shm.h"
#include "pmgr.h"
#include "utils_atomic.h"
#include "utils_spinlock.h"

#include "ip_out.h"
#include "raw.h"

#define RAW_HASH_SIZE  (32)
#define RAW_HASH_MASK  (RAW_HASH_SIZE - 1)

typedef struct {
    atomic32_t ref;
    Spinlock_t lock;

    SOCK_SkList_t skLists[RAW_HASH_SIZE];
} RawSkTbl_t;

static void* AllocRawSkTbl(void)
{
    RawSkTbl_t* tbl;
    size_t      allocSize;

    allocSize = sizeof(RawSkTbl_t);

    tbl = SHM_MALLOC(allocSize, MOD_RAW, DP_MEM_FREE);
    if (tbl == NULL) {
        DP_LOG_ERR("Malloc memory failed for ip raw hashtbl.");
        return NULL;
    }

    tbl->ref = 0;

    SPINLOCK_Init(&tbl->lock);

    for (int i = 0; i < RAW_HASH_SIZE; i++) {
        LIST_INIT_HEAD(&tbl->skLists[i]);
    }

    return tbl;
}

static void FreeRawSkTbl(void* ptr)
{
    RawSkTbl_t* tbl = (RawSkTbl_t*)ptr;
    SPINLOCK_Deinit(&tbl->lock);
    SHM_FREE(tbl, DP_MEM_FREE);
}

static inline uint32_t HashSk(Sock_t* sk)
{
    return (RAW_INETSK(sk)->hashinfo.protocol & RAW_HASH_MASK);
}

static inline void RefRawSkTbl(RawSkTbl_t* tbl)
{
    ATOMIC32_Inc(&tbl->ref);
}

static inline void DerefRawSkTbl(RawSkTbl_t* tbl)
{
    ATOMIC32_Dec(&tbl->ref);
}

static inline void WaitTblIdle(RawSkTbl_t* tbl)
{
    while (ATOMIC32_Load(&tbl->ref) > 0) { }
}

static int InsertRaw(RawSkTbl_t* tbl, Sock_t* sk)
{
    uint32_t hash = HashSk(sk);

    SPINLOCK_Lock(&tbl->lock);

    if (!SOCK_IS_BINDED(sk)) {
        LIST_INSERT_TAIL(&tbl->skLists[hash], sk, node);
    }

    SPINLOCK_Unlock(&tbl->lock);

    return 0;
}

static void RemoveRaw(RawSkTbl_t* tbl, Sock_t* sk)
{
    uint32_t hash = HashSk(sk);

    SPINLOCK_Lock(&tbl->lock);

    if (SOCK_IS_BINDED(sk)) {
        LIST_REMOVE(&tbl->skLists[hash], sk, node);
        SOCK_CLR_BINDED(sk);
    }

    SPINLOCK_Unlock(&tbl->lock);
}

static int MatchRawCb(Sock_t* sk, const DP_IpHdr_t* ip)
{
    InetSk_t* rawSk = RAW_INETSK(sk);

    if (rawSk->hashinfo.protocol != ip->type) {
        return 0;
    }

    if (rawSk->hashinfo.laddr != DP_INADDR_ANY && rawSk->hashinfo.laddr != ip->dst) {
        return 0;
    }

    if (rawSk->hashinfo.paddr != DP_INADDR_ANY && rawSk->hashinfo.paddr != ip->src) {
        return 0;
    }

    return 1;
}

void RawInput(DP_Pbuf_t* pbuf, DP_IpHdr_t* ipHdr)
{
    Netdev_t*      dev  = PBUF_GET_DEV(pbuf);
    RawSkTbl_t*    tbl  = (RawSkTbl_t*)NS_GET_RAW_TBL(dev->net);
    SOCK_SkList_t* head = &tbl->skLists[(ipHdr->type & RAW_HASH_MASK)];
    Sock_t*        sk;
    DP_Pbuf_t*     clone;

    RefRawSkTbl(tbl);

    if (LIST_IS_EMPTY(head)) {
        DerefRawSkTbl(tbl);
        return;
    }

    SPINLOCK_Lock(&tbl->lock);
    LIST_FOREACH(head, sk, node)
    {
        if (MatchRawCb(sk, ipHdr) == 0) {
            continue;
        }

        clone = PBUF_Clone(pbuf);
        if (clone != NULL) {
            if (SOCK_PushRcvBufSafe(sk, clone) < 0) {
                PBUF_Free(clone);
            }
        }
    }
    SPINLOCK_Unlock(&tbl->lock);

    DerefRawSkTbl(tbl);
}

static int RawBind(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int       ret;
    InetSk_t* inetSk = RAW_INETSK(sk);
    uint32_t  hash   = HashSk(sk);
    RawSkTbl_t* tbl  = (RawSkTbl_t*)NS_GET_RAW_TBL(sk->net);

    const struct DP_SockaddrIn* addrIn = (const struct DP_SockaddrIn*)addr;

    ret = INET_CheckAddr(addr, addrlen);
    if (ret != 0) {
        return ret;
    }

    SPINLOCK_Lock(&tbl->lock);

    inetSk->hashinfo.laddr = addrIn->sin_addr.s_addr;
    if (!SOCK_IS_BINDED(sk)) {
        LIST_INSERT_TAIL(&tbl->skLists[hash], sk, node);
    }

    SPINLOCK_Unlock(&tbl->lock);

    return 0;
}

static int RawConnect(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int       ret;
    InetSk_t* inetSk = RAW_INETSK(sk);
    uint32_t  hash   = HashSk(sk);
    RawSkTbl_t* tbl  = (RawSkTbl_t*)NS_GET_RAW_TBL(sk->net);

    const struct DP_SockaddrIn* addrIn = (const struct DP_SockaddrIn*)addr;

    ret = INET_CheckAddr(addr, addrlen);
    if (ret != 0) {
        return ret;
    }

    SPINLOCK_Lock(&tbl->lock);

    inetSk->hashinfo.paddr = addrIn->sin_addr.s_addr;
    if (!SOCK_IS_BINDED(sk)) {
        LIST_INSERT_TAIL(&tbl->skLists[hash], sk, node);
    }

    SPINLOCK_Unlock(&tbl->lock);

    SOCK_SET_CONNECTED(sk);

    return 0;
}

static int RawCheckAddr(Sock_t* sk, const struct DP_Msghdr* msg, size_t msgDataLen, DP_InAddr_t* dstAddr)
{
    int       ret;
    InetSk_t* inetSk = RAW_INETSK(sk);

    if (inetSk->options.incHdr != 0) {
        // 如果包括 IP 头部，不需要 msg 中的地址
        if (msgDataLen < sizeof(DP_IpHdr_t)) {
            return -EINVAL;
        }
        return 0;
    }

    // 不能发送纯IP报文
    if (inetSk->hashinfo.protocol == DP_IPPROTO_IP) {
        return -EINVAL;
    }

    if (msg->msg_name != NULL) {
        ret = INET_CheckAddr(msg->msg_name, msg->msg_namelen);
        if (ret != 0) {
            return ret;
        }

        *dstAddr = ((struct DP_SockaddrIn*)msg->msg_name)->sin_addr.s_addr;
    } else if (SOCK_IS_CONNECTED(sk)) {
        *dstAddr = inetSk->hashinfo.paddr;
    } else {
        return -EFAULT;
    }

    return 0;
}

static void RawRouteSetPbuf(Pbuf_t* pbuf, InetSk_t* inetSk)
{
    PBUF_SET_DEV(pbuf, INET_GetDevByFlow(&inetSk->flow));
    PBUF_SET_QUE_ID(pbuf, 0);
    DP_PBUF_SET_WID(pbuf, (uint8_t)NETDEV_GetTxWid(PBUF_GET_DEV(pbuf), 0));
    PBUF_SET_FLOW(pbuf, &inetSk->flow);
    
    if (inetSk->options.incHdr == 0) {
        PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_ROUTE_OUT);
        return;
    }

    /* 如果设置了 IP_HDRINCL, 用户自行指定首部信息，其中部分字段修改策略如下:
       A. IP包总长度：总是填充
       B. IP头校验和(chksum)：总是填充
       C. 源IP地址：若为0，则自动填充为本机地址
       D. 包ID(packet id)，若为0，则自动填充
    */
    DP_IpHdr_t* ipHdr = PBUF_MTOD(pbuf, DP_IpHdr_t*);
    if (ipHdr->src == 0) {
        ipHdr->src = inetSk->flow.src;
    }
    if (ipHdr->ipid == 0) {
        if (PBUF_GET_L4_TYPE(pbuf) == DP_IPPROTO_TCP) {
            ipHdr->ipid = UTILS_HTONS(IpGetId(DP_PBUF_GET_WID(pbuf)));
        } else {
            ipHdr->ipid = UTILS_HTONS(IpGetGlobalId());
        }
    }
    ipHdr->totlen  = UTILS_HTONS((uint16_t)PBUF_GET_PKT_LEN(pbuf));
    ipHdr->chksum  = IpCalcCksum(pbuf, INET_GetDevByFlow(&inetSk->flow), ipHdr, sizeof(DP_IpHdr_t));

    PBUF_SET_PKT_TYPE(pbuf, inetSk->flow.flowType);
    PBUF_SET_ND(pbuf, inetSk->flow.nd);
    IpTxCb(pbuf)->mtu = inetSk->flow.mtu;
    PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_IP_OUT);
}

ssize_t RawSendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags, size_t msgDataLen, size_t* index, size_t* offset)
{
    (void)index;
    (void)offset;
    (void)flags;

    int         ret;
    Pbuf_t*     pbuf;
    InetSk_t*   inetSk  = RAW_INETSK(sk);
    DP_InAddr_t dstAddr = 0;

    ret = RawCheckAddr(sk, msg, msgDataLen, &dstAddr);
    if (ret != 0) {
        return ret;
    }

    pbuf = SOCK_PbufBuildFromMsg(msg, SOCK_INET_HEADROOM);
    if (pbuf == NULL) {
        return -ENOBUFS;
    }

    if (inetSk->options.incHdr != 0) {
        DP_IpHdr_t* ipHdr = PBUF_MTOD(pbuf, DP_IpHdr_t*);
        PBUF_SET_DST_ADDR4(pbuf, ipHdr->dst);
        PBUF_SET_L4_TYPE(pbuf, ipHdr->type);
        PBUF_SET_PKT_FLAGS(pbuf, PBUF_PKTFLAGS_IP_INCHDR);
    } else {
        PBUF_SET_DST_ADDR4(pbuf, dstAddr);
        PBUF_SET_L4_TYPE(pbuf, inetSk->hashinfo.protocol);
        PBUF_SET_PKT_FLAGS(pbuf, 0);
    }

    inetSk->flow.dst = PBUF_GET_DST_ADDR4(pbuf);

    INET_DeinitFlow(&inetSk->flow);

    ret = INET_InitFlowBySk(sk, inetSk, &inetSk->flow);
    if (ret != 0) {
        PBUF_Free(pbuf);
        return ret;
    }

    if ((inetSk->flow.rt == NULL) && ((inetSk->flow.flags & INET_FLOW_FLAGS_NO_ROUTE) == 0)) {
        PBUF_Free(pbuf);
        return -ENETUNREACH;
    }

    if ((INET_GetDevByFlow(&inetSk->flow)->ifflags & DP_IFF_UP) == 0) {
        PBUF_Free(pbuf);
        return -ENETDOWN;
    }

    if (msgDataLen > (size_t)(inetSk->flow.mtu - SOCK_INET_HEADROOM)) {
        PBUF_Free(pbuf);
        return -EMSGSIZE;
    }

    RawRouteSetPbuf(pbuf, inetSk);

    PMGR_Dispatch(pbuf);

    return (ssize_t)msgDataLen;
}

int RawClose(Sock_t* sk)
{
    RemoveRaw(NS_GET_RAW_TBL(sk->net), sk);

    WaitTblIdle(NS_GET_RAW_TBL(sk->net));

    INET_DeinitFlow(&RAW_INETSK(sk)->flow);
    SOCK_Unlock(sk);
    SOCK_DeinitSk(sk);
    SHM_FREE(sk, DP_MEM_FREE);

    return 0;
}

static int RawGetDstAddr(Sock_t* sk, Pbuf_t* pbuf, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen)
{
    (void)sk;

    struct DP_SockaddrIn* addrin = (struct DP_SockaddrIn*)addr;

    if (*addrlen < sizeof(*addrin) || pbuf == NULL) {
        return -EINVAL;
    }

    *addrlen = sizeof(*addrin);

    addrin->sin_family      = DP_AF_INET;
    addrin->sin_port        = 0;
    addrin->sin_addr.s_addr = PBUF_GET_DST_ADDR4(pbuf);

    return 0;
}

static int RawGetsockopt(Sock_t* sk, int level, int optName, void* optVal, DP_Socklen_t* optLen)
{
    if (level == DP_IPPROTO_IP) {
        return INET_Getsockopt(RAW_INETSK(sk), optName, optVal, optLen);
    }
    return -ENOPROTOOPT;
}

static int RawSetsockopt(Sock_t* sk, int level, int optName, const void* optVal, DP_Socklen_t optLen)
{
    if (level == DP_IPPROTO_IP) {
        return INET_Setsockopt(RAW_INETSK(sk), optName, optVal, optLen);
    }
    return -ENOPROTOOPT;
}

static SOCK_Ops_t g_rawOps = {
    .type       = DP_SOCK_RAW,
    .close      = RawClose,
    .bind       = RawBind,
    .connect    = RawConnect,
    .sendmsg    = RawSendmsg,
    .recvmsg    = SOCK_PopRcvBufByPkt,
    .getDstAddr = RawGetDstAddr,
    .getsockopt = RawGetsockopt,
    .setsockopt = RawSetsockopt,
};

static inline int RawProtoCheck(int protocol)
{
    if (protocol != DP_IPPROTO_ICMP &&
        protocol != DP_IPPROTO_TCP &&
        protocol != DP_IPPROTO_UDP &&
        protocol != DP_IPPROTO_RAW) {
        return -1;
    }
    return 0;
}

int CreateRawSk(NS_Net_t* net, int type, int protocol, Sock_t** out)
{
    Sock_t* sk;
    size_t  objSize = SOCK_GetSkSize(sizeof(RawSk_t));
    int     ret;

    (void)type;

    if (RawProtoCheck(protocol) != 0) {
        return -EPROTONOSUPPORT;
    }

    // sk控制块计数及上限检查涉及对外更改，后续统一补充
    sk = SHM_MALLOC(objSize, MOD_RAW, DP_MEM_FREE);
    if (sk == NULL) {
        DP_LOG_ERR("Malloc memory failed for ipraw sk.");
        return -ENOMEM;
    }

    (void)memset_s(sk, objSize, 0, objSize);

    ret = SOCK_InitSk(sk, NULL, objSize);
    if (ret != 0) {
        SHM_FREE(sk, DP_MEM_FREE);
        return ret;
    }

    sk->family = DP_AF_INET;
    sk->net    = net;
    sk->ops    = &g_rawOps;

    RAW_INETSK(sk)->hashinfo.protocol = (uint8_t)protocol;
    RAW_INETSK(sk)->hashinfo.laddr    = DP_INADDR_ANY;
    RAW_INETSK(sk)->hashinfo.paddr    = DP_INADDR_ANY;

    if (protocol == DP_IPPROTO_RAW) {
        RAW_SK(sk)->inetSk.options.incHdr = 1;
    } else {
        SOCK_SET_RECV_MORE(sk); // IPPROTO_RAW 协议类型只能用于发送报文
    }

    InsertRaw(NS_GET_RAW_TBL(net), sk);
    SOCK_SET_BINDED(sk);
    SOCK_SET_SEND_MORE(sk);

    SOCK_SET_WRITABLE(sk);

    *out = sk;

    return 0;
}

int RAW_Init(int slave)
{
    SOCK_ProtoOps_t pops = {
        .type     = DP_SOCK_RAW,
        .protocol = 0,
        .create   = CreateRawSk,
    };

    SOCK_AddProto(DP_AF_INET, &pops);

    if (slave != 0) {
        return 0;
    }

    NS_SetNetOps(NS_NET_RAW, AllocRawSkTbl, FreeRawSkTbl);

    AddRawFn(RawInput);
    return 0;
}

void RAW_Deinit(int slave)
{
    (void)slave;
}