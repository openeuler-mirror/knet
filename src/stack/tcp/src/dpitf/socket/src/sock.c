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
#include <limits.h>
#include <securec.h>

#include "sock.h"

#include "dp_fd.h"
#include "netdev.h"
#include "ns.h"
#include "utils_base.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_statistic.h"

#include "sock_notify.h"

#define MAX_IOV_LEN SSIZE_MAX

static SOCK_NotifyFn_t g_notifyFns[SOCK_NOTIFY_TYPE_MAX];

static SOCK_NotifyFn_t g_notifyHook = NULL;

static SOCK_FamilyOps_t g_familyOps[] = {
    {
        .family = DP_AF_INET,
    },
    {
        .family = DP_AF_INET6,
    },
    {
        .family = DP_AF_PACKET,
    },
    {
        .family = DP_AF_NETLINK,
    },
};

void SOCK_AddFamilyOps(const SOCK_FamilyOps_t* ops)
{
    for (int i = 0; i < (int)DP_ARRAY_SIZE(g_familyOps); i++) {
        if (g_familyOps[i].family == ops->family) {
            g_familyOps[i] = *ops;
            return;
        }
    }

    ASSERT(0); // 正常情况下不应该走到这里
}

static const SOCK_FamilyOps_t* GetFamilyOps(int family)
{
    for (int i = 0; i < (int)DP_ARRAY_SIZE(g_familyOps); i++) {
        if (g_familyOps[i].family == family && g_familyOps[i].lookup != NULL) {
            return (const SOCK_FamilyOps_t*)&g_familyOps[i];
        }
    }

    return NULL;
}

void SOCK_AddProto(int family, const SOCK_ProtoOps_t* ops)
{
    const SOCK_FamilyOps_t* fops = GetFamilyOps(family);

    ASSERT(fops != NULL);
    ASSERT(fops->add != NULL);

    fops->add(ops);
}

int SOCK_PushRcvBufSafe(Sock_t* sk, DP_Pbuf_t* pbuf)
{
    int ret = -1;
    SOCK_Lock(sk);

    ASSERT(PBUF_GET_PKT_LEN(pbuf) > 0);

    if ((PBUF_GET_PKT_LEN(pbuf) + sk->rcvBuf.bufLen) > sk->rcvHiwat) {
        SOCK_Unlock(sk);
        return ret;
    }

    ret = (int)pbuf->totLen;
    PBUF_ChainPush(&sk->rcvBuf, pbuf);

    if (sk->rcvBuf.bufLen >= sk->rcvLowat) {
        SOCK_WakeupRdSem(sk);
        SOCK_SetState(sk, SOCK_STATE_READ | SOCK_STATE_READ_ET);
    }

    SOCK_Unlock(sk);
    return ret;
}

static ssize_t SockPbufChainRead(Sock_t* sk, struct DP_Msghdr* msg, int peek)
{
    size_t             readed;
    ssize_t            ret = 0;
    struct DP_Iovec* iov;

    if (msg->msg_name != NULL) {
        ASSERT(sk->ops->getDstAddr != NULL);
        ret = sk->ops->getDstAddr(sk, NULL, msg->msg_name, &msg->msg_namelen);
        if (ret != 0) {
            DP_ADD_ABN_STAT(DP_RCV_GET_ADDR_FAILED);
            return ret;
        }
    }

    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        iov = msg->msg_iov + i;
        // iov->base为NULL且长度不为0的情况在之前就已经判断过了
        if (UTILS_UNLIKELY(iov->iov_len == 0)) {
            continue;
        }
        readed = PBUF_ChainRead(&sk->rcvBuf, iov->iov_base, iov->iov_len, peek, 0);
        if (UTILS_UNLIKELY(readed == 0)) {
            DP_ADD_ABN_STAT(DP_SOCK_READ_BUFCHAIN_ZRRO);
            break;
        }
        ret += (ssize_t)readed;
        if (readed < iov->iov_len) {
            DP_ADD_ABN_STAT(DP_SOCK_READ_BUFCHAIN_SHORT);
            break;
        }
    }

    return ret;
}

// 零拷贝写路径
static ssize_t SockPbufChainReadZcopy(Sock_t* sk, struct DP_ZMsghdr* msg)
{
    size_t             readed;
    ssize_t            ret = 0;
    struct DP_ZIovec* iov;

    if (msg->msg_name != NULL) {
        ASSERT(sk->ops->getDstAddr != NULL);
        ret = sk->ops->getDstAddr(sk, NULL, msg->msg_name, &msg->msg_namelen);
        if (ret != 0) {
            DP_ADD_ABN_STAT(DP_RCV_ZCOPY_GET_ADDR_FAILED);
            return ret;
        }
    }

    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        iov = msg->msg_iov + i;

        readed = PBUF_ChainReadZcopy(&sk->rcvBuf, iov);
        if (readed <= 0) {
            DP_ADD_ABN_STAT(DP_RCV_ZCOPY_CHAIN_READ_FAILED);
            break;
        }

        ret += (ssize_t)readed;
    }

    return ret;
}

static uint16_t SockPbufRead(Pbuf_t** pbuf, uint16_t* readLen, uint8_t* iov, size_t iovlen)
{
    ASSERT(*pbuf != NULL);
    uint16_t ret    = 0;
    uint16_t offset = *readLen;
    Pbuf_t*  cur    = *pbuf;

    while (cur != NULL && cur->segLen == 0) {  // 找到第一片有效cur
        cur = cur->next;
    }

    while (cur != NULL && ret < iovlen) {
        uint16_t cpyLen = (iovlen - ret > 0xFFFF) ? 0xFFFF : (uint16_t)(iovlen - ret); // 如果长度超过0xFFFF，取最大值
        cpyLen          = (cpyLen > (cur->segLen - offset)) ? (cur->segLen - offset) : cpyLen;
        if (cpyLen == 0) {
            cur = cur->next;
            continue;
        }

        if (iov != NULL) {
            (void)memcpy_s(iov + ret, cpyLen, PBUF_MTOD(cur, uint8_t*) + offset, cpyLen);
        }
        ret += cpyLen;

        *pbuf = cur;                // 出参，保存当前已读到的cur报文位置
        *readLen = offset + cpyLen; // 出参，保存当前cur报文已读的长度
        cur = cur->next;

        if (offset != 0) {
            offset = 0;
        }
    }

    return ret;
}

/*
    根据内核协议栈UDP Recvmsg进行整改，内核对应操作
    1. 如果传入iov->iov_len为0，则将数据拷贝到下一个有效的iov中
    2. 如果所有有效的iov长度为0，则返回为0
    3. 如果iov->iov_base为NULL 返回EFAULT错误码
    4. 当iov->iov_len>0时开始读取报文，填满当前iov后继续往下找有效iov
    5. 一直到有效iov全部填满或者该报文已读完，则该报文释放
*/
static ssize_t SockPbufCopy(DP_Pbuf_t* pbuf, struct DP_Msghdr* msg)
{
    ssize_t ret = 0;
    ssize_t len = 0;
    struct DP_Iovec* iov = msg->msg_iov;
    size_t cnt = 0;
    uint32_t totLen = PBUF_GET_PKT_LEN(pbuf);
    uint16_t offset = 0;
    Pbuf_t* cur = pbuf;

    while (cnt < msg->msg_iovlen && totLen > ret) {
        if (iov->iov_len == 0) { // 若当前iov没有有效空间，则继续找到有效iov
            cnt++;
            iov = msg->msg_iov + cnt;
            continue;
        }
        if (iov->iov_base == NULL) {
            return -EFAULT;
        }
        len = SockPbufRead(&cur, &offset, iov->iov_base, iov->iov_len);
        if (len <= 0) {
            return ret;
        }
        ret += len;
        cnt++;
        iov = msg->msg_iov + cnt;
    }

    return ret;
}

ssize_t SOCK_PopRcvBuf(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen)
{
    ssize_t ret = 0;

    (void)msgDataLen;

    if (((uint32_t)flags & DP_MSG_ZEROCOPY) != 0) {
        ret = SockPbufChainReadZcopy(sk, (struct DP_ZMsghdr*)msg);
    } else {
        ret = SockPbufChainRead(sk, msg, (uint32_t)flags & DP_MSG_PEEK);
    }
    if (UTILS_UNLIKELY(ret == 0)) {
        // 如果可以收取更多数据，则返回-EAGAIN，否则返回0
        if (UTILS_LIKELY(SOCK_CAN_RECV_MORE(sk))) {
            return -EAGAIN;
        }
        return 0;
    }

    if (UTILS_UNLIKELY(ret < 0)) {
        DP_ADD_ABN_STAT(DP_TCP_RCV_BUF_FAILED);
    }

    if (((uint32_t)flags & DP_MSG_PEEK) != 0) {
        return ret;
    }

    if (sk->rcvBuf.bufLen < sk->rcvLowat) {
        SOCK_UnsetState(sk, SOCK_STATE_READ);
    }

    return ret;
}

ssize_t SOCK_PopRcvBufByPkt(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen)
{
    ssize_t ret;
    Pbuf_t* pbuf;

    (void)msgDataLen;

    pbuf = PBUF_CHAIN_FIRST(&sk->rcvBuf);
    if (pbuf == NULL) {
        return -EAGAIN;
    }

    if (msg->msg_name != NULL) {
        ASSERT(sk->ops->getDstAddr != NULL);
        ret = sk->ops->getDstAddr(sk, pbuf, msg->msg_name, &msg->msg_namelen);
        if (ret != 0) {
            return ret;
        }
    }

    ret = SockPbufCopy(pbuf, msg);

    if (((uint32_t)flags & DP_MSG_PEEK) != 0) {
        return ret;
    }

    if (ret >= 0) {
        pbuf = PBUF_CHAIN_POP(&sk->rcvBuf);
        PBUF_Free(pbuf);
    }

    if (sk->rcvBuf.bufLen < sk->rcvLowat) {
        SOCK_UnsetState(sk, SOCK_STATE_READ);
    }

    return ret;
}

Pbuf_t* SOCK_PbufBuildFromMsg(const struct DP_Msghdr* msg, uint16_t headroom)
{
    Pbuf_t*          ret = NULL;
    size_t           i;
    struct DP_Iovec* iov;

    ASSERT(msg->msg_iov != NULL);

    for (i = 0; i < msg->msg_iovlen; i++) {
        iov = msg->msg_iov + i;
        if (iov->iov_base == NULL || iov->iov_len <= 0) {
            continue;
        }
        if (ret == NULL) {
            ret = PBUF_Build(iov->iov_base, (uint16_t)iov->iov_len, headroom);
            if (ret == NULL) {
                DP_ADD_ABN_STAT(DP_FROM_MSG_BUILD_PBUF_FAILED);
                return NULL;
            }
        } else {
            if (PBUF_Append(ret, iov->iov_base, (uint16_t)iov->iov_len) == 0) {
                PBUF_Free(ret);
                DP_ADD_ABN_STAT(DP_FROM_MSG_APPEND_PBUF_FAILED);
                return NULL;
            }
        }
    }

    return ret;
}

// SEM_WAIT调用SemWait，返回值为0或errno（正数）
static inline int WaitSkSem(Sock_t* sk, DP_Sem_t sem, int timeout)
{
    int ret;

    SOCK_Unlock(sk);

    ret = (int)SEM_WAIT(sem, timeout);

    SOCK_Lock(sk);

    if (ret == DP_ERR) {
        ret = EFAULT;
    }
    return -ret;
}

static inline int WaitRdSem(Sock_t* sk)
{
    int ret;
    sk->rdSemCnt++;

    ret = WaitSkSem(sk, sk->rdSem, sk->rcvTimeout);
    if (ret != 0) {
        sk->rdSemCnt--;
    }

    return ret;
}

static inline int WaitWrSem(Sock_t* sk)
{
    int ret;
    sk->wrSemCnt++;

    ret = WaitSkSem(sk, sk->wrSem, sk->sndTimeout);
    if (ret != 0) {
        sk->wrSemCnt--;
    }

    return ret;
}

static int SOCK_CheckAddrLen(Sock_t* sk, DP_Socklen_t addrlen)
{
    if (sk->family == DP_AF_INET) {
        if ((int)addrlen < (int)sizeof(struct DP_SockaddrIn)) {
            DP_ADD_ABN_STAT(DP_CONN_ADDRLEN_ERR);
            return -1;
        }
        return 0;
    }

    if ((int)addrlen < (int)sizeof(struct DP_SockaddrIn6)) {
        DP_ADD_ABN_STAT(DP_CONN_ADDR6LEN_ERR);
        return -1;
    }

    return 0;
}

static inline struct DP_Iovec* SockGetIov(const struct DP_Msghdr* msg, size_t index, int flags)
{
    struct DP_Iovec* iov = NULL;
    if (((uint32_t)flags & DP_MSG_ZEROCOPY) != 0) {
        iov = (struct DP_Iovec*)(((struct DP_ZMsghdr*)msg)->msg_iov + index);
    } else {
        iov = msg->msg_iov + index;
    }
    return iov;
}

ssize_t SOCK_GetMsgIovLen(const struct DP_Msghdr* msg, int flags)
{
    size_t ret = 0;
    struct DP_Iovec* iov;
    struct DP_ZIovec* ziov;
    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        iov = SockGetIov(msg, i, flags);
        if ((ssize_t)(iov->iov_len) < 0) {
            DP_ADD_ABN_STAT(DP_GET_IOVLEN_INVAL);
            return -EINVAL;
        }
        if (iov->iov_len == 0) {
            continue;
        }
        if (iov->iov_base == NULL) {
            DP_ADD_ABN_STAT(DP_GET_IOV_BASE_NULL);
            return -EFAULT;
        }
        if (((uint32_t)flags & DP_MSG_ZEROCOPY) != 0) {
            ziov = (struct DP_ZIovec*)iov;
            if (ziov->freeCb == NULL) {
                DP_ADD_ABN_STAT(DP_ZIOV_CB_NULL);
                return -EFAULT;
            }
        }
        if (MAX_IOV_LEN - ret < iov->iov_len) {
            DP_ADD_ABN_STAT(DP_GET_TOTAL_IOVLEN_INVAL);
            return -EINVAL;
        }
        ret += iov->iov_len;
    }
    return (ssize_t)ret;
}

static ssize_t SockCheckMsgValidity(const struct DP_Msghdr* msg)
{
    int ret = 1;
    if (msg == NULL) {
        DP_ADD_ABN_STAT(DP_SOCK_CHECK_MSG_NULL);
        ret = -EFAULT;
    } else if (msg->msg_iovlen == 0) { /* 内核行为如果msg_iovlen为0 不判断msg_iov的正确性 */
        ret = 0;
    } else if ((msg->msg_iov == NULL)) {
        DP_ADD_ABN_STAT(DP_SOCK_CHECK_MSGIOV_NULL);
        ret = -EFAULT;
    } else if ((ssize_t)(msg->msg_iovlen) < 0 || msg->msg_iovlen > MAX_IOV_CNT) {
        DP_ADD_ABN_STAT(DP_SOCK_CHECK_MSGIOV_INVAL);
        ret = -EMSGSIZE;
    }
    if (ret < 0) {
        DP_LOG_DBG("SockCheckMsgValidity failed, ret = %d.", ret);
    }
    return ret;
}

ssize_t SOCK_GetMsgDataLen(const struct DP_Msghdr* msg, int flags)
{
    ssize_t ret = SockCheckMsgValidity(msg);
    if (UTILS_UNLIKELY(ret <= 0)) {
        DP_LOG_DBG("SockCheckMsgValidity failed, ret = %d.", (int)(-ret));
        return ret;
    }

    ret = SOCK_GetMsgIovLen(msg, flags);
    if (UTILS_UNLIKELY(ret < 0)) {
        DP_LOG_DBG("SOCK_GetMsgIovLen failed, ret = %d.", (int)(-ret));
    }
    return ret;
}

int SOCK_Create(NS_Net_t* net, int domain, int type, int protocol, Sock_t** sk)
{
    const SOCK_FamilyOps_t* ops = GetFamilyOps(domain);
    SOCK_CreateSkFn_t       createFn;
    int                     ret;

    if (ops == NULL) {
        DP_LOG_DBG("Sock create failed, ops null");
        DP_ADD_ABN_STAT(DP_SOCKET_DOMAIN_ERR);
        return -EAFNOSUPPORT;
    }

    ret = ops->lookup(type, protocol, &createFn);
    if (ret != 0) {
        DP_LOG_DBG("Sock create failed, find createFn failed.");
        DP_ADD_ABN_STAT(DP_SOCKET_NO_CREATEFN);
        return ret;
    }

    ASSERT(createFn != NULL);

    return createFn(net, type, protocol, sk);
}

int SOCK_Close(Sock_t* sk)
{
    int ret;

    SOCK_Lock(sk); // close需要先通知事件，由事件处理部分删除

    SOCK_SetState(sk, SOCK_STATE_CLOSE); // 上报一个close，由适配者释放相关资源
    if (sk->notifyType != SOCK_NOTIFY_TYPE_HOOK) {
        SOCK_DisableNotify(sk);
    }

    SOCK_SET_CLOSED(sk);
    // close表示用户侧不在操作此socket资源，仅有实例内部操作，交给具体协议实现释放内存，以及释放锁
    ret = sk->ops->close(sk);

    return ret;
}

int SOCK_Shutdown(Sock_t *sk, int how)
{
    int ret;

    if (how != DP_SHUT_RD && how != DP_SHUT_WR && how != DP_SHUT_RDWR) {
        DP_LOG_DBG("Sock shutdown failed, how %d invalid", how);
        return -EINVAL;
    }

    SOCK_Lock(sk);
    // 当前仅支持TCP shutdown
    if (sk->ops->shutdown == NULL) {
        SOCK_Unlock(sk);
        DP_LOG_DBG("Sock shutdown failed, shutdown not support");
        return -ENOTCONN;
    }

    // 建链前不支持调用shutdown接口
    if (!SOCK_IS_CONNECTED(sk)) {
        SOCK_Unlock(sk);
        return -ENOTCONN;
    }

    ret = sk->ops->shutdown(sk, how);

    if (how == DP_SHUT_RD || how == DP_SHUT_RDWR) {
        SOCK_CLR_RECV_MORE(sk);
        SOCK_SET_CANTRCVMORE(sk);
        SOCK_SET_SHUTRD(sk);
    }

    if (how == DP_SHUT_WR || how == DP_SHUT_RDWR) {
        SOCK_SET_SHUTWR(sk);
        SOCK_CLR_SEND_MORE(sk);
        SOCK_SET_CANTSENDMORE(sk);
    }

    SOCK_Unlock(sk);

    if (ret < 0) {
        DP_LOG_DBG("SOCK_Shutdown failed, ret = %d", ret);
    }
    return ret;
}

static int SOCK_CheckConnStatus(Sock_t *sk)
{
    if (SOCK_IS_CONNECTED(sk)) {
        return 0;
    } else if (SOCK_IS_CONN_REFUSED(sk)) {
        DP_ADD_ABN_STAT(DP_CONN_REFUSED);
        return -ECONNREFUSED;
    } else {
        return -EINPROGRESS;
    }
}

int SOCK_Connect(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int ret = -EPROTOTYPE;

    if (addr == NULL) {
        DP_LOG_DBG("Sock connect failed, addr NULL.");
        DP_ADD_ABN_STAT(DP_CONN_ADDR_NULL);
        return -EFAULT;
    }

    if (SOCK_CheckAddrLen(sk, addrlen) != 0) {
        DP_LOG_DBG("Sock connect failed, addrlen invalid.");
        return -EINVAL;
    }

    SOCK_Lock(sk);

    if (SOCK_IS_CONN_REFUSED(sk)) {
        DP_ADD_ABN_STAT(DP_CONN_REFUSED);
        ret = -ECONNREFUSED;
    }

    if (sk->ops->connect != NULL) {
        ret = sk->ops->connect(sk, addr, addrlen);
    }

    if ((ret != 0) || (!SOCK_IS_CONNECTING(sk)) || sk->nonblock != 0) {
        if (ret == 0 && SOCK_IS_CONNECTING(sk)) {
            ret = -EINPROGRESS;
        } else {
            DP_ADD_ABN_STAT(DP_CONN_FAILED);
        }
        SOCK_Unlock(sk);
        return ret;
    }

    ret = WaitWrSem(sk); // 这里需要等待协议处理结果
    // ret不为0，返回信号量产生的错误码
    if (ret == 0) {
        if (sk->error == 0) {
            ret = SOCK_CheckConnStatus(sk);
        } else {
            ret = -sk->error;
        }
    }

    SOCK_Unlock(sk);

    return ret;
}

int SOCK_Bind(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int ret = -EINVAL;

    ASSERT(sk->ops != NULL);

    SOCK_Lock(sk);

    if (sk->ops->bind != NULL) {
        ret = sk->ops->bind(sk, addr, addrlen);
    }

    if (ret == 0) {
        SOCK_SET_BINDED(sk);
    } else {
        DP_LOG_DBG("SOCK_Bind failed, ret = %d.", ret);
        DP_ADD_ABN_STAT(DP_BIND_FAILED);
    }

    SOCK_Unlock(sk);

    return ret;
}

int SOCK_Listen(Sock_t* sk, int backlog)
{
    int ret;

    SOCK_Lock(sk);

    if (sk->ops->listen != NULL) {
        ret = sk->ops->listen(sk, backlog);
    } else {
        ret = -EOPNOTSUPP;
        DP_LOG_DBG("Sock listen failed, listen not support");
    }

    SOCK_Unlock(sk);

    return ret;
}

int SOCK_Accept(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, Sock_t** newSk)
{
    int ret = -EINVAL;

    ASSERT(sk != NULL);

    SOCK_Lock(sk);

    if ((addr != NULL) && (addrlen == NULL)) {
        ret = -EFAULT;
        DP_LOG_DBG("Sock accept failed, param null");
        DP_ADD_ABN_STAT(DP_ACCEPT_ADDRLEN_NULL);
        goto out;
    }

    if ((addr != NULL) && ((int)*addrlen < 0)) {
        DP_LOG_DBG("Sock accept failed, param invalid");
        DP_ADD_ABN_STAT(DP_ACCEPT_ADDRLEN_INVAL);
        goto out;
    }

    if (sk->ops->accept == NULL) {
        ret = -EOPNOTSUPP;
        DP_LOG_DBG("Sock accept failed, accept null");
        DP_ADD_ABN_STAT(DP_ACCEPT_NO_SUPPORT);
        goto out;
    }

    while (1) {
        ret = sk->ops->accept(sk, addr, addrlen, newSk);
        if (ret != -EAGAIN || sk->nonblock != 0) {
            break;
        }

        ret = WaitRdSem(sk);
        if (ret != 0) {
            ret = (ret == -ETIMEDOUT) ? -EAGAIN : ret;
            break;
        } else if (sk->error != 0) {
            ret = -sk->error;
            sk->error = 0;
            break;
        }
    }

out:
    SOCK_Unlock(sk);

    return ret;
}

int SOCK_Ioctl(Sock_t* sk, int request, void* arg)
{
    int err = 0;

    SOCK_Lock(sk);

    switch (request) {
        case DP_FIONBIO:
            if (arg == NULL) {
                DP_LOG_DBG("Sock ioctl failed, arg invalid");
                err = -EFAULT;
            } else {
                sk->nonblock = *(int*)arg == 0 ? 0 : 1;
            }
            break;
        case DP_FIONREAD:
            if (arg == NULL) {
                DP_LOG_DBG("Sock ioctl failed, arg invalid");
                err = -EFAULT;
            } else {
                *(int *)arg = (int)sk->rcvBuf.bufLen;
            }
            break;
        default:
            err = -EINVAL;
            DP_LOG_DBG("Sock ioctl failed, request %d not support", request);
            break;
    }

    SOCK_Unlock(sk);

    return err;
}

int SOCK_Fcntl(Sock_t* sk, int cmd, int val)
{
    int ret = 0;

    SOCK_Lock(sk);

    switch (cmd) {
        case DP_F_GETFL:
            ret = sk->nonblock == 1 ? DP_SOCK_NONBLOCK : 0;
            break;
        case DP_F_SETFL:
            sk->nonblock = (((unsigned int)val & DP_SOCK_NONBLOCK) != 0U) ? 1 : 0;
            break;
        default:
            ret = -EINVAL;
            DP_LOG_DBG("Sock fcntl failed, cmd %d not support", cmd);
            break;
    }

    SOCK_Unlock(sk);

    return ret;
}

static ssize_t PreprocessSendFlags(Sock_t* sk, int flags)
{
    if (UTILS_UNLIKELY(((uint32_t)flags | DP_MSG_SEND_SUPPORT_FLAGS) != DP_MSG_SEND_SUPPORT_FLAGS)) {
        return -EOPNOTSUPP;
    }

    if (((uint32_t)flags & DP_MSG_MORE) != 0) {
        sk->flags |= SOCK_FLAGS_MSG_MORE;
    } else {
        sk->flags &= ~SOCK_FLAGS_MSG_MORE;
    }

    return 0;
}

ssize_t SOCK_Sendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags)
{
    ssize_t ret;
    size_t sendLen = 0;
    size_t index = 0;
    size_t offset = 0;
    ssize_t msgDataLen;

    ret = PreprocessSendFlags(sk, flags);
    if (UTILS_UNLIKELY(ret != 0)) {
        DP_LOG_DBG("SOCK_Sendmsg failed with unsupport flags, flags = %d.", flags);
        DP_ADD_ABN_STAT(DP_SEND_FLAGS_INVAL);
        return ret;
    }

    msgDataLen = SOCK_GetMsgDataLen(msg, flags);
    if (UTILS_UNLIKELY(msgDataLen < 0)) {
        DP_ADD_ABN_STAT(DP_SEND_GET_DATALEN_FAILED);
        return msgDataLen;
    } else if (UTILS_UNLIKELY(msgDataLen == 0)) {
        DP_ADD_ABN_STAT(DP_SEND_ZERO_DATALEN);
        return 0;
    }

    SOCK_LockOptional(sk);

    while (1) {
        // index和offset作为输入输出参数，记录当前已发送的数据长度，下次发送时直接偏移至指定位置发送
        ret = sk->ops->sendmsg(sk, msg, flags, (size_t)msgDataLen, &index, &offset);
        if (((uint32_t)flags & DP_MSG_DONTWAIT) != 0 || sk->nonblock != 0) {
            break;
        } else if (ret < 0 && ret != -EAGAIN) {
            DP_ADD_ABN_STAT(DP_SOCK_SENDMSG_FAILED);
            break;
        }

        if (ret > 0) {
            sendLen += (size_t)ret;
            msgDataLen -= ret;
            // 数据发送完成，设置ret为已发送数据大小
            if (msgDataLen == 0) {
                ret = (ssize_t)sendLen;
                break;
            }
            // ret 大于 0，说明 space 不为 0，再发送一次
            continue;
        }

        ret = WaitWrSem(sk);
        if (ret != 0) {
            ret = (ret == -ETIMEDOUT) ? -EAGAIN : ret;
            if (sendLen > 0) {
                ret = (ssize_t)sendLen;
            }
            break;
        }
    }

    SOCK_UnLockOptional(sk);
    return ret;
}

ssize_t SOCK_Sendto(
    Sock_t* sk, const void* buf, size_t len, int flags, const struct DP_Sockaddr* dstAddr, DP_Socklen_t addrlen)
{
    struct DP_Msghdr msg;
    struct DP_Iovec  iov[1];
    uint32_t msgFlags = (uint32_t)flags;

    if (UTILS_UNLIKELY(len == 0)) {
        return 0;
    }

    if (UTILS_UNLIKELY(buf == NULL)) {
        DP_LOG_DBG("Sock send failed, param invalid");
        DP_ADD_ABN_STAT(DP_SENDTO_BUF_NULL);
        return -EFAULT;
    }

    iov->iov_base      = (void*)buf;
    iov->iov_len       = len;
    msg.msg_name       = (void*)dstAddr;
    msg.msg_namelen    = addrlen;
    msg.msg_controllen = 0;
    msg.msg_control    = NULL;
    msg.msg_iov        = iov;
    msg.msg_iovlen     = DP_ARRAY_SIZE(iov);

    if (UTILS_UNLIKELY(((uint32_t)flags & DP_MSG_ZEROCOPY) != 0)) {     // 清零零拷贝标志位
        msgFlags = ((uint32_t)flags & ~DP_MSG_ZEROCOPY);
    }

    return SOCK_Sendmsg(sk, &msg, (int)msgFlags);
}

ssize_t SOCK_Recvmsg(Sock_t* sk, struct DP_Msghdr* msg, int flags)
{
    ssize_t ret;
    if (UTILS_UNLIKELY(((uint32_t)flags | DP_MSG_RECV_SUPPORT_FLAGS) != DP_MSG_RECV_SUPPORT_FLAGS)) {
        DP_LOG_DBG("SOCK_Recvmsg failed with unsupport flags, flags = %d.", flags);
        DP_ADD_ABN_STAT(DP_RECV_FLAGS_INVAL);
        return -EOPNOTSUPP;
    }

    ssize_t msgDataLen = 0;
    if (((uint32_t)flags & DP_MSG_ZEROCOPY) == 0) {
        msgDataLen = SOCK_GetMsgDataLen(msg, flags);
        if (msgDataLen <= 0) {
            DP_ADD_ABN_STAT(DP_RECV_GET_DATALEN_FAILED);
            return msgDataLen;
        }
    } else {     // 零拷贝读时，不计算 msgDataLen
        ret = SockCheckMsgValidity(msg);
        DP_ADD_ABN_STAT(DP_RECV_CHECK_MSG_FAILED);
        if (ret <= 0) {
            return ret;
        }
    }

    SOCK_LockOptional(sk);

    while (1) {
        ret = sk->ops->recvmsg(sk, msg, flags, (size_t)msgDataLen);
        if (ret != -EAGAIN || ((uint32_t)flags & DP_MSG_DONTWAIT) != 0 || sk->nonblock != 0) {
            break;
        }

        ret = WaitRdSem(sk);
        if (ret != 0) {
            ret = (ret == -ETIMEDOUT) ? -EAGAIN : ret;
            break;
        }

        if (sk->error != 0) {
            ret       = -sk->error;
            sk->error = 0;
            break;
        }
    }

    SOCK_UnLockOptional(sk);
    if (UTILS_UNLIKELY(ret < 0 && ret != -EAGAIN)) {
        DP_ADD_ABN_STAT(DP_SOCK_RECVMSG_FAILED);
    }
    return ret;
}

ssize_t SOCK_Recvfrom(
    Sock_t* sk, void* buf, size_t len, int flags, struct DP_Sockaddr* srcAddr, DP_Socklen_t* addrlen)
{
    ssize_t          ret;
    struct DP_Msghdr msg;
    struct DP_Iovec  iov[1];
    uint32_t msgFlags = (uint32_t)flags;

    if (UTILS_UNLIKELY(len == 0)) {
        return 0;
    }

    if (UTILS_UNLIKELY(buf == NULL)) {
        DP_LOG_DBG("Sock recv failed, param invalid");
        DP_ADD_ABN_STAT(DP_RCVFROM_BUF_NULL);
        return -EFAULT;
    }

    iov->iov_base      = buf;
    iov->iov_len       = len;
    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;
    msg.msg_iov        = iov;
    msg.msg_iovlen     = DP_ARRAY_SIZE(iov);

    if (UTILS_UNLIKELY(srcAddr != NULL && addrlen != NULL)) {
        msg.msg_name    = srcAddr;
        msg.msg_namelen = *addrlen;
    }

    if (UTILS_UNLIKELY(((uint32_t)flags & DP_MSG_ZEROCOPY) != 0)) {     // 清零零拷贝标志位
        msgFlags = ((uint32_t)flags & ~DP_MSG_ZEROCOPY);
    }

    ret = SOCK_Recvmsg(sk, &msg, (int)msgFlags);

    if (UTILS_UNLIKELY(addrlen != NULL)) {
        *addrlen = msg.msg_namelen;
    }

    return ret;
}

typedef struct {
    int optname;
    int (*set)(Sock_t* sk, const void* optval, DP_Socklen_t optlen);
    int (*get)(Sock_t* sk, void* optval, DP_Socklen_t* optlen);
} SockOptOps_t;

static uint32_t GetValByRange(uint32_t high, uint32_t low, uint32_t val)
{
    uint32_t bufVal = val;
    bufVal = (bufVal > low) ? bufVal : low;
    bufVal = (bufVal < high) ? bufVal : high;
    return bufVal;
}

static int SockSetTimeout(const void* optval, DP_Socklen_t optlen, int* timeout)
{
    struct DP_Timeval* tv = (struct DP_Timeval*)optval;

    if ((int)optlen < (int)sizeof(struct DP_Timeval)) {
        DP_LOG_DBG("Sock set timeout failed, invalid param");
        return -EINVAL;
    }

    // usec需要在0~1000 ms内
    if ((tv->tv_usec < 0) || (tv->tv_usec >= USEC_PER_SEC)) {
        DP_LOG_DBG("Sock set timeout failed, usec out of range");
        return -EDOM;
    }

    if (tv->tv_sec < 0) {
        *timeout = 0;
        return 0;
    }

    if (((tv->tv_usec == 0) && (tv->tv_sec == 0)) || (tv->tv_sec > ((INT_MAX / MSEC_PER_SEC) - 1))) {
        *timeout = -1;
        return 0;
    }

    *timeout = (int)(tv->tv_sec * MSEC_PER_SEC + tv->tv_usec / USEC_PER_MSEC);  // ms

    return 0;
}

static int SockGetTimeout(void* optval, DP_Socklen_t *optlen, int timeout)
{
    struct DP_Timeval* tv = optval;

    if (*optlen < sizeof(struct DP_Timeval)) {
        DP_LOG_DBG("Sock timeval with optlen %u invalid", *optlen);
        return -EINVAL;
    }

    if (timeout < 0) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
        return 0;
    }

    tv->tv_sec = timeout / MSEC_PER_SEC;
    tv->tv_usec = (timeout % MSEC_PER_SEC) * USEC_PER_MSEC;
    return 0;
}

static int SockSetSndTimeo(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    return SockSetTimeout(optval, optlen, &sk->sndTimeout);
}

static int SockGetSndTimeo(Sock_t* sk, void* optval, DP_Socklen_t* optlen)
{
    int ret = SockGetTimeout(optval, optlen, sk->sndTimeout);
    *optlen = sizeof(struct DP_Timeval);
    return ret;
}

static int SockSetRcvTimeo(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    return SockSetTimeout(optval, optlen, &sk->rcvTimeout);
}

static int SockGetRcvTimeo(Sock_t* sk, void* optval, DP_Socklen_t* optlen)
{
    int ret = SockGetTimeout(optval, optlen, sk->rcvTimeout);
    *optlen = sizeof(struct DP_Timeval);
    return ret;
}

static int SockSetReuseAddr(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    if ((int)optlen < (int)sizeof(int)) {
        DP_LOG_DBG("Sock set reuseAddr failed, optlen %u invalid", optlen);
        return -EINVAL;
    }
    sk->reuseAddr = *(int *)optval == 0 ? 0 : 1;
    return 0;
}

static int SockGetReuseAddr(Sock_t* sk, void* optval, DP_Socklen_t* optlen)
{
    *(int *)optval = sk->reuseAddr;
    *optlen = sizeof(DP_Socklen_t);
    return 0;
}

static int SockSetReusePort(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    if ((int)optlen < (int)sizeof(int)) {
        DP_LOG_DBG("Sock set ReusePort failed, optlen %u invalid", optlen);
        return -EINVAL;
    }
    sk->reusePort = *(int *)optval == 0 ? 0 : 1;
    return 0;
}

static int SockGetReusePort(Sock_t* sk, void* optval, DP_Socklen_t* optlen)
{
    *(int *)optval = sk->reusePort;
    *optlen = sizeof(DP_Socklen_t);
    return 0;
}

static int SockSetKeepAlive(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    if ((int)optlen < (int)sizeof(int)) {
        DP_LOG_DBG("Sock set keepalive failed, optlen %u invalid", optlen);
        return -EINVAL;
    }

    if (sk->ops->keepalive != NULL) {
        return sk->ops->keepalive(sk, *(int *)optval == 0 ? 0 : 1);
    }

    return 0;
}

static int SockGetKeepAlive(Sock_t* sk, void* optval, DP_Socklen_t* optlen)
{
    *(int *)optval = sk->keepalive;
    *optlen = sizeof(DP_Socklen_t);
    return 0;
}

static int SockSetLinger(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    if ((int)optlen < (int)sizeof(struct DP_Linger)) {
        DP_LOG_DBG("Sock set linger failed, optlen %u invalid", optlen);
        return -EINVAL;
    }

    struct DP_Linger *linger = (struct DP_Linger *)optval;
    if (linger->l_onoff != 0) {
        sk->lingerOnoff = 1;
        sk->linger = linger->l_linger; // 不使用linger延时参数，参数非零时，实际不生效
    } else {
        sk->lingerOnoff = 0;
    }
    return 0;
}

static int SockGetLinger(Sock_t* sk, void* optval, DP_Socklen_t* optlen)
{
    if (*optlen < sizeof(struct DP_Linger)) {
        DP_LOG_DBG("Sock linget with optlen %u invalid", *optlen);
        return -EINVAL;
    }

    ((struct DP_Linger*)optval)->l_onoff  = sk->lingerOnoff;
    ((struct DP_Linger*)optval)->l_linger = sk->linger;

    *optlen = sizeof(struct DP_Linger);
    return 0;
}

static int SockSetSndBuf(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    if (((int)optlen < (int)sizeof(int)) || (*(int *)optval < 0)) {
        DP_LOG_DBG("Sock set sndbuf failed with param invalid");
        return -EINVAL;
    }

    uint32_t hiWat = *(uint32_t *)optval;

    hiWat = GetValByRange((uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_WMEM_MAX), DP_TCP_LOWLIMIT_WMEM_MAX, hiWat);
    sk->sndHiwat = hiWat;
    if (sk->sndLowat > sk->sndHiwat) {
        sk->sndLowat = sk->sndHiwat;
    }

    return 0;
}

static int SockGetSndBuf(Sock_t* sk, void* optval, DP_Socklen_t* optlen)
{
    *(uint32_t *)optval = sk->sndHiwat * 2; // 接口行为与内核保持一致，设置缓冲区大小为A，获取到的结果为A * 2
    *optlen = sizeof(DP_Socklen_t);
    return 0;
}

static int SockSetRcvBuf(Sock_t*sk, const void* optval, DP_Socklen_t optlen)
{
    if (((int)optlen < (int)sizeof(int)) || (*(int *)optval < 0)) {
        DP_LOG_DBG("Sock set rcvbuf failed with param invalid");
        return -EINVAL;
    }

    uint32_t hiWat = *(uint32_t *)optval;

    hiWat = GetValByRange((uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RMEM_MAX), DP_TCP_LOWLIMIT_RMEM_MAX, hiWat);
    sk->rcvHiwat = hiWat;
    if (sk->rcvLowat > sk->rcvHiwat) {
        sk->rcvLowat = sk->rcvHiwat;
    }

    return 0;
}

static int SockGetRcvBuf(Sock_t*sk, void* optval, DP_Socklen_t* optlen)
{
    *(uint32_t *)optval = sk->rcvHiwat * 2; // 接口行为与内核保持一致，设置缓冲区大小为A，获取到的结果为A * 2
    *optlen = sizeof(DP_Socklen_t);
    return 0;
}

static int SockGetError(Sock_t*sk, void* optval, DP_Socklen_t* optlen)
{
    *(int *)optval = sk->error;
    *optlen = sizeof(DP_Socklen_t);
    sk->error = 0;
    return 0;
}

static int SockSetUserData(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    if (optlen < sizeof(void*)) {
        DP_LOG_DBG("SockSetUserData failed, optlen = %u.", optlen);
        return -EINVAL;
    }
    sk->userData = *((void**)optval);
    return 0;
}

static int SockGetUserData(Sock_t* sk, void *optval, DP_Socklen_t *optlen)
{
    if (*optlen < sizeof(void*)) {
        DP_LOG_DBG("SockGetUserData failed, *optlen = %u.", *optlen);
        return -EINVAL;
    }

    *((void**)optval) = sk->userData;
    *optlen = sizeof(void*);
    return 0;
}

static int SockSetRcvLow(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    if (((uint32_t)optlen < (uint32_t)sizeof(uint32_t))) {
        DP_LOG_DBG("Sock set rcvlow failed with param invalid");
        return -EINVAL;
    }

    uint32_t lowWat = *(uint32_t *)optval;
    lowWat = (lowWat == 0) ? 1 : lowWat;

    sk->rcvLowat = UTILS_MIN(lowWat, sk->rcvHiwat >> 1);

    if (sk->rcvBuf.bufLen >= sk->rcvLowat) {
        SOCK_SetState(sk, SOCK_STATE_READ | SOCK_STATE_READ_ET);
    }

    return 0;
}

static int SockGetRcvLow(Sock_t* sk, void *optval, DP_Socklen_t *optlen)
{
    *(uint32_t *)optval = sk->rcvLowat;
    *optlen = sizeof(uint32_t);
    return 0;
}

static int SockSetPriority(Sock_t* sk, const void* optval, DP_Socklen_t optlen)
{
    if (((int)optlen < (int)sizeof(int)) || (*(int *)optval < 0) || (*(int *)optval > SOCK_PRIORITY_MAX)) {
        DP_LOG_DBG("Sock set priority failed with param invalid");
        return -EINVAL;
    }

    sk->priority = *(int *)optval;

    return 0;
}

static int SockGetPriority(Sock_t* sk, void *optval, DP_Socklen_t *optlen)
{
    *(int *)optval = sk->priority;
    *optlen = sizeof(int);
    return 0;
}

static int SockGetProtocol(Sock_t* sk, void *optval, DP_Socklen_t *optlen)
{
    *(int *)optval = sk->ops->protocol;
    *optlen = sizeof(int);
    return 0;
}

static int SockGetAcceptConn(Sock_t* sk, void *optval, DP_Socklen_t *optlen)
{
    if (sk->ops->protocol != DP_IPPROTO_TCP) {
        return -EINVAL;
    }
    *((int *)optval) = SOCK_IS_LISTENED(sk) ? 1 : 0;
    *optlen = sizeof(int);
    return 0;
}

static int SockGetSockType(Sock_t* sk, void *optval, DP_Socklen_t *optlen)
{
    *((int *)optval) = sk->sockType;
    *optlen = sizeof(int);
    return 0;
}

static int SockSetSndLowat(Sock_t*sk, const void* optval, DP_Socklen_t optlen)
{
    if (((int)optlen < (int)sizeof(int)) || (*(int *)optval < 0)) {
        DP_LOG_DBG("Sock set snd lowat failed with param invalid");
        return -EINVAL;
    }

    if (sk->ops->protocol != DP_IPPROTO_TCP) {
        return -EINVAL;
    }

    uint32_t loWat = *(uint32_t *)optval;
    sk->sndLowat = loWat > sk->sndHiwat ? sk->sndHiwat : loWat;
    return 0;
}

static int SockGetSndLowat(Sock_t* sk, void *optval, DP_Socklen_t *optlen)
{
    if (sk->ops->protocol != DP_IPPROTO_TCP) {
        return -EINVAL;
    }
    *((uint32_t *)optval) = sk->sndLowat;
    *optlen = sizeof(int);
    return 0;
}

static int SockSetBroadcast(Sock_t*sk, const void* optval, DP_Socklen_t optlen)
{
    if (((int)optlen < (int)sizeof(int)) || (*(int *)optval < 0)) {
        DP_LOG_DBG("Sock set broadcast failed with param invalid");
        return -EINVAL;
    }

    if (sk->ops->protocol != DP_IPPROTO_UDP) {
        return -EINVAL;
    }

    int isBroadcast = *(int *)optval;
    sk->broadcast = isBroadcast == 0 ? 0 : 1;
    return 0;
}

static int SockSetRcvBufForce(Sock_t*sk, const void* optval, DP_Socklen_t optlen)
{
    if (((int)optlen < (int)sizeof(int)) || (*(int *)optval < 0)) {
        DP_LOG_DBG("Sock set rcvbuf force failed with param invalid");
        return -EINVAL;
    }

    if (sk->ops->protocol != DP_IPPROTO_TCP) {
        return -EINVAL;
    }

    uint32_t hiWat = *(uint32_t *)optval;
    sk->rcvHiwat = hiWat;
    if (sk->rcvLowat > sk->rcvHiwat) {
        sk->rcvLowat = sk->rcvHiwat;
    }

    return 0;
}

static int SockSetTimestamp(Sock_t*sk, const void* optval, DP_Socklen_t optlen)
{
    if (((int)optlen < (int)sizeof(int)) || (*(int *)optval < 0)) {
        DP_LOG_DBG("Sock set Timestamp failed with param invalid");
        return -EINVAL;
    }

    sk->isTimestamp = *(int *)optval == 0 ? 0 : 1;
    return 0;
}

static int SockGetTimestamp(Sock_t* sk, void* optval, DP_Socklen_t* optlen)
{
    if (*optlen < sizeof(int)) {
        DP_LOG_DBG("Sock Timestamp with optlen %u invalid", *optlen);
        return -EINVAL;
    }

    *((int *)optval) = sk->isTimestamp;
    *optlen = sizeof(int);
    return 0;
}

static SockOptOps_t g_sockOptOps[] = {
    {DP_SO_SNDTIMEO, SockSetSndTimeo, SockGetSndTimeo},
    {DP_SO_RCVTIMEO, SockSetRcvTimeo, SockGetRcvTimeo},
    {DP_SO_REUSEADDR, SockSetReuseAddr, SockGetReuseAddr},
    {DP_SO_REUSEPORT, SockSetReusePort, SockGetReusePort},
    {DP_SO_KEEPALIVE, SockSetKeepAlive, SockGetKeepAlive},
    {DP_SO_LINGER, SockSetLinger, SockGetLinger},
    {DP_SO_SNDBUF, SockSetSndBuf, SockGetSndBuf},
    {DP_SO_RCVBUF, SockSetRcvBuf, SockGetRcvBuf},
    {DP_SO_ERROR, NULL, SockGetError},
    {DP_SO_USERDATA, SockSetUserData, SockGetUserData},
    {DP_SO_RCVLOWAT, SockSetRcvLow, SockGetRcvLow},
    {DP_SO_PRIORITY, SockSetPriority, SockGetPriority},
    {DP_SO_PROTOCOL, NULL, SockGetProtocol},
    {DP_SO_ACCEPTCONN, NULL, SockGetAcceptConn},
    {DP_SO_TYPE, NULL, SockGetSockType},
    {DP_SO_SNDLOWAT, SockSetSndLowat, SockGetSndLowat},
    {DP_SO_BROADCAST, SockSetBroadcast, NULL},
    {DP_SO_RCVBUFFORCE, SockSetRcvBufForce, NULL},
    {DP_SO_TIMESTAMP, SockSetTimestamp, SockGetTimestamp},
};

int SockSetsockopt(Sock_t* sk, int level, int optname, const void* optval, DP_Socklen_t optlen)
{
    (void)level;

    if (optval == NULL) {
        DP_LOG_DBG("Sock setOpt optval invalid null");
        return -EFAULT;
    }

    for (size_t i = 0; i < DP_ARRAY_SIZE(g_sockOptOps); i++) {
        if (g_sockOptOps[i].optname != optname) {
            continue;
        }
        if (g_sockOptOps[i].set == NULL) {
            return -ENOPROTOOPT;
        }
        return g_sockOptOps[i].set(sk, optval, optlen);
    }
    DP_LOG_DBG("Sock setOpt failed, invalid optname, optname = %d.", optname);
    return -ENOPROTOOPT;
}

int SOCK_Setsockopt(Sock_t* sk, int level, int optname, const void* optval, DP_Socklen_t optlen)
{
    int ret;

    SOCK_Lock(sk);

    switch (level) {
        case DP_SOL_SOCKET:
            ret = SockSetsockopt(sk, level, optname, optval, optlen);
            break;
        case DP_IPPROTO_IP:
        case DP_IPPROTO_IPV6:
        case DP_IPPROTO_TCP:
        case DP_IPPROTO_UDP:
            ret = -ENOPROTOOPT;
            if (sk->ops->setsockopt != NULL) {
                ret = sk->ops->setsockopt(sk, level, optname, optval, optlen);
            }
            break;
        default:
            ret = -ENOPROTOOPT;
            DP_LOG_DBG("DP_Setsockopt failed, invalid level, level = %d.", level);
            break;
    }

    ASSERT(ret <= 0);

    SOCK_Unlock(sk);

    return ret;
}

int SockGetsockopt(Sock_t* sk, int level, int optname, void* optval, DP_Socklen_t* optlen)
{
    (void)level;

    if ((optval == NULL) || (optlen == NULL)) {
        DP_LOG_DBG("Sock getOpt failed, invalid param");
        return -EFAULT;
    }

    if ((int)*optlen < (int)sizeof(DP_Socklen_t)) {
        DP_LOG_DBG("Sock getOpt failed, invalid *optlen, *optlen = %d.", (int)*optlen);
        return -EINVAL;
    }

    for (size_t i = 0; i < DP_ARRAY_SIZE(g_sockOptOps); i++) {
        if (g_sockOptOps[i].optname != optname) {
            continue;
        }
        if (g_sockOptOps[i].get == NULL) {
            return -ENOPROTOOPT;
        }
        return g_sockOptOps[i].get(sk, optval, optlen);
    }
    DP_LOG_DBG("Sock getOpt failed, invalid optname, optname = %d.", optname);
    return -ENOPROTOOPT;
}

int SOCK_Getsockopt(Sock_t* sk, int level, int optname, void* optval, DP_Socklen_t* optlen)
{
    int ret = 0;

    SOCK_Lock(sk);

    switch (level) {
        case DP_SOL_SOCKET:
            ret = SockGetsockopt(sk, level, optname, optval, optlen);
            break;
        case DP_IPPROTO_IP:
        case DP_IPPROTO_IPV6:
        case DP_IPPROTO_TCP:
        case DP_IPPROTO_UDP:
            ret = -EOPNOTSUPP;
            if (sk->ops->getsockopt != NULL) {
                ret = sk->ops->getsockopt(sk, level, optname, optval, optlen);
            }
            break;
        default:
            ret = -EOPNOTSUPP;
            DP_LOG_DBG("DP_Getsockopt failed, invalid level, level = %d.", level);
            break;
    }

    ASSERT(ret <= 0);

    SOCK_Unlock(sk);

    return ret;
}

int SOCK_CheckAddrParam(struct DP_Sockaddr* addr, DP_Socklen_t* addrlen)
{
    if (addrlen == NULL || (*addrlen != 0 && addr == NULL)) {
        return -EFAULT;
    }

    // 无符号入参*addrlen如果传入负数，返回错误，与内核保持一致
    if ((int)*addrlen < 0) {
        return -EINVAL;
    }
    return 0;
}

int SOCK_Getpeername(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen)
{
    int ret = SOCK_CheckAddrParam(addr, addrlen);
    if (ret != 0) {
        DP_LOG_DBG("Sock getpeername failed, param invalid.");
        return ret;
    }

    if (*addrlen == 0) {
        DP_LOG_INFO("Getpeername with param addrlen 0.");
        return 0;
    }

    SOCK_Lock(sk);

    if (SOCK_IS_SHUTWR(sk) || SOCK_IS_SHUTRD(sk)) {
        SOCK_Unlock(sk);
        DP_LOG_DBG("Sock getpeername failed, sk has been shutdown.");
        return -EINVAL;
    }

    if (!SOCK_IS_CONNECTED(sk)) {
        SOCK_Unlock(sk);
        DP_LOG_DBG("Sock getpeername failed, sk has not connect.");
        return -ENOTCONN;
    }

    ASSERT(sk->ops->getAddr != NULL);

    ret = sk->ops->getAddr(sk, addr, addrlen, 1);
    if (ret != 0) {
        DP_LOG_DBG("Sock getpeername failed, getAddr failed.");
    }

    SOCK_Unlock(sk);

    return ret;
}

int SOCK_Getsockname(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen)
{
    int ret = SOCK_CheckAddrParam(addr, addrlen);
    if (ret != 0) {
        DP_LOG_DBG("Sock getsockname failed, param invalid.");
        return ret;
    }

    if (*addrlen == 0) {
        DP_LOG_INFO("Getsockname with param addrlen 0.");
        return 0;
    }

    SOCK_Lock(sk);

    if (SOCK_IS_SHUTWR(sk) || SOCK_IS_SHUTRD(sk)) {
        SOCK_Unlock(sk);
        DP_LOG_DBG("Sock getsockname failed, sk has been shutdown.");
        return -EINVAL;
    }

    ASSERT(sk->ops->getAddr != NULL);

    ret = sk->ops->getAddr(sk, addr, addrlen, 0);
    if (ret != 0) {
        DP_LOG_DBG("Sock getsockname failed, getAddr failed.");
    }

    SOCK_Unlock(sk);

    return ret;
}

void SockShowInfo(Sock_t* sk)
{
    uint32_t offset = 0;
    char output[LEN_INFO] = {0};
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "\r\n-------- SockInfo --------\n");
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "protocol = %d\n", sk->ops->protocol);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsLingerOn = %u\n", sk->lingerOnoff);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsNonblock = %u\n", sk->nonblock);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsReuseAddr = %u\n", sk->reuseAddr);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsReusePort = %u\n", sk->reusePort);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsBroadcast = %u\n", sk->broadcast);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsKeepalive = %u\n", sk->keepalive);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsBindDev = %u\n", sk->bindDev);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsDontRoute = %u\n", sk->dontRoute);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "SockErr = %u\n", sk->error);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "family = %u\n", sk->family);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "linger = %d\n", sk->linger);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "flags = %u\n", sk->flags);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "state = %u\n", sk->state);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "vrfId = %u\n", sk->vrfId);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rdSemCnt = %u\n", sk->rdSemCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "wrSemCnt = %u\n", sk->wrSemCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rcvTimeout = %d\n", sk->rcvTimeout);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndTimeout = %d\n", sk->sndTimeout);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndLowat = %u\n", sk->sndLowat);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndHiwat = %u\n", sk->sndHiwat);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rcvLowat = %u\n", sk->rcvLowat);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rcvHiwat = %u\n", sk->rcvHiwat);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "priority = %d\n", sk->priority);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "associateFd = %d\n", sk->associateFd);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "notifyType = %d\n", sk->notifyType);

    DEBUG_SHOW(0, output, offset);
}

void SOCK_ShowSocketInfo(Sock_t* sk)
{
    SockShowInfo(sk);

    if (sk->ops->showInfo != NULL) {
        sk->ops->showInfo(sk);
    }
}

static void SockGetDetails(Sock_t* sk, DP_SockDetails_t* details)
{
    details->protocol = sk->ops->protocol;
    details->options = sk->options;
    details->error = sk->error;
    details->family = sk->family;
    details->linger = sk->linger;
    details->flags = sk->flags;
    details->state = sk->state;
    details->rdSemCnt = sk->rdSemCnt;
    details->wrSemCnt = sk->wrSemCnt;
    details->rcvTimeout = sk->rcvTimeout;
    details->sndTimeout = sk->sndTimeout;
    details->sndDataLen = sk->sndBuf.bufLen;
    details->rcvDataLen = sk->rcvBuf.bufLen;
    details->sndLowat = sk->sndLowat;
    details->sndHiwat = sk->sndHiwat;
    details->rcvLowat = sk->rcvLowat;
    details->rcvHiwat = sk->rcvHiwat;
    details->priority = sk->priority;
    details->associateFd = sk->associateFd;
    details->notifyType = sk->notifyType;
    details->wid = sk->wid;
}

void SOCK_GetSocketDetails(Sock_t* sk, DP_SockDetails_t* details)
{
    SockGetDetails(sk, details);

    if (sk->ops->getDetails != NULL) {
        sk->ops->getDetails(sk, details);
    }
}

int SOCK_GetSocketState(Sock_t* sk, DP_SocketState_t* state)
{
    if (sk->ops->getState != NULL) {
        sk->ops->getState(sk, state);
        return 0;
    }
    return -EOPNOTSUPP;
}

int SOCK_SetNotifyFn(int type, SOCK_NotifyFn_t notifyFn)
{
    if (type <= SOCK_NOTIFY_TYPE_NONE || type >= SOCK_NOTIFY_TYPE_MAX) {
        DP_LOG_ERR("Sock set notify type error.");
        return -1;
    }
    g_notifyFns[type] = notifyFn;
    return 0;
}

void SOCK_Notify(Sock_t* sk, uint8_t oldState, uint8_t event)
{
    if (g_notifyFns[sk->notifyType] != NULL) {
        g_notifyFns[sk->notifyType](sk, sk->notifyCtx, oldState, sk->state, event);
    }
}

uint32_t SOCK_GetRWStateSafe(Sock_t* sk)
{
    uint32_t ret;

    SOCK_Lock(sk);

    ret = sk->state;

    SOCK_Unlock(sk);

    return ret;
}

uint32_t SOCK_GetState(Sock_t* sk)
{
    uint32_t ret;

    ret = sk->state;

    return ret;
}

void SOCK_EnableNotify(Sock_t* sk, int type, void* ctx, int assocFd)
{
    if (type <= SOCK_NOTIFY_TYPE_NONE || type >= SOCK_NOTIFY_TYPE_MAX) {
        return;
    }

    sk->notifyType  = type;
    sk->notifyCtx   = ctx;
    sk->associateFd = assocFd;
}

void SOCK_EnableNotifySafe(Sock_t* sk, int type, void* ctx, int assocFd)
{
    SOCK_Lock(sk);

    SOCK_EnableNotify(sk, type, ctx, assocFd);

    SOCK_Unlock(sk);
}

void SOCK_DisableNotify(Sock_t* sk)
{
    sk->notifyType  = SOCK_NOTIFY_TYPE_NONE;
    sk->notifyCtx   = NULL;
    sk->associateFd = -1;
}

void SOCK_DisableNotifySafe(Sock_t* sk)
{
    SOCK_Lock(sk);

    SOCK_DisableNotify(sk);

    SOCK_Unlock(sk);
}

static int InitSockMem(Sock_t* sk, size_t objSize)
{
    sk->wrSem = PTR_PREV((uint8_t*)sk + objSize, SOCK_ALIGN_SIZE(SEM_Size));
    sk->rdSem = PTR_PREV(sk->wrSem, SOCK_ALIGN_SIZE(SEM_Size));

    if (SPINLOCK_Init(&sk->lock) != 0) {
        return -ENOMEM;
    }
    if (SEM_INIT(sk->rdSem) != 0) {
        SPINLOCK_Deinit(&sk->lock);
        return -ENOMEM;
    }
    if (SEM_INIT(sk->wrSem) != 0) {
        SPINLOCK_Deinit(&sk->lock);
        SEM_DEINIT(sk->rdSem);
        return -ENOMEM;
    }

    return 0;
}

int SOCK_InitSk(Sock_t* sk, Sock_t* parent, size_t objSize)
{
    int ret;

    ret = InitSockMem(sk, objSize);
    if (ret != 0) {
        DP_LOG_ERR("Sock init semaphore or spinlock failed!");
        return ret;
    }

    if (parent == NULL) {
        sk->sndTimeout = -1; // 默认没有超时时间
        sk->rcvTimeout = -1;
        sk->sndHiwat   = (uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_WMEM_DEFAULT);
        sk->rcvHiwat   = (uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RMEM_DEFAULT);
        sk->sndLowat   = 1;
        sk->rcvLowat   = 1;
        sk->flags      = 0;
    } else {
        sk->sndTimeout = parent->sndTimeout;
        sk->rcvTimeout = parent->rcvTimeout;
        sk->sndHiwat   = parent->sndHiwat;
        sk->rcvHiwat   = parent->rcvHiwat;
        sk->sndLowat   = parent->sndLowat;
        sk->rcvLowat   = parent->rcvLowat;

        sk->ops     = parent->ops;
        sk->options = parent->options;

        sk->net = parent->net;
        sk->wid = parent->wid;
        sk->glbHashTblIdx = parent->glbHashTblIdx;
        sk->vpnid = parent->vpnid;
        sk->sockType = parent->sockType;
    }

    sk->ref = 1;
    sk->dev = NULL;

    if (g_notifyHook != NULL) {
        SOCK_EnableNotify(sk, SOCK_NOTIFY_TYPE_HOOK, NULL, -1);
    }

    return 0;
}

void SOCK_DeinitSk(Sock_t* sk)
{
    if (sk->dev != NULL) {
        NETDEV_PutDev(sk->dev);
    }

    if (sk->pfDev != NULL) {
        NETDEV_PutDev(sk->pfDev);
    }

    if (sk->file != NULL) {
        FD_Free(sk->file);
    }

    if (sk->pacingCb != NULL) {
        MEM_FREE(sk->pacingCb, DP_MEM_FREE);
    }

    PBUF_ChainClean(&sk->rcvBuf);
    PBUF_ChainClean(&sk->sndBuf);
    SEM_DEINIT(sk->rdSem);
    SEM_DEINIT(sk->wrSem);
    SPINLOCK_Deinit(&sk->lock);
    SOCK_DisableNotify(sk);
}

int SOCK_NotifyHookCreate(SOCK_NotifyFn_t hook)
{
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("SFE_NotifyCreate failed, dp already init.");
        return -1;
    }

    g_notifyHook = hook;

    return 0;
}

size_t SOCK_SIZE;

int SOCK_Init(int slave)
{
    (void)slave;
    if (FD_Init() != 0) {
        DP_LOG_ERR("Sock init fd failed.");
        FD_Deinit();
        return -1;
    }

    SOCK_SIZE = SOCK_ALIGN_SIZE(sizeof(Sock_t));
    SOCK_SIZE += SOCK_ALIGN_SIZE(SEM_Size) * 2; // 包含读写2个信息量

    (void)SOCK_SetNotifyFn(SOCK_NOTIFY_TYPE_HOOK, g_notifyHook);

    return 0;
}

void SOCK_Deinit(int slave)
{
    (void)slave;
    g_notifyHook = NULL;
    SockResetFdIdx();
    FD_Deinit();
}
