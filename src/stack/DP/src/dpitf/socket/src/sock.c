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

#include "sock.h"

#include "fd.h"
#include "netdev.h"
#include "utils_base.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_statistic.h"

#include "sock_notify.h"

#define MAX_IOV_LEN SSIZE_MAX

static SOCK_NotifyFn_t g_notifyFns[SOCK_NOTIFY_TYPE_MAX];

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
    for (int i = 0; i < (int)ARRAY_SIZE(g_familyOps); i++) {
        if (g_familyOps[i].family == ops->family) {
            g_familyOps[i] = *ops;
            return;
        }
    }

    ASSERT(0); // 正常情况下不应该走到这里
}

static const SOCK_FamilyOps_t* GetFamilyOps(int family)
{
    for (int i = 0; i < (int)ARRAY_SIZE(g_familyOps); i++) {
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
        SOCK_SetState(sk, SOCK_STATE_READ);
    }

    SOCK_WakeupRdSem(sk);

    SOCK_Unlock(sk);
    return ret;
}

static ssize_t SockPbufChainRead(PBUF_Chain_t* chain, struct DP_Msghdr* msg, int peek)
{
    size_t             readed;
    ssize_t            ret = 0;
    struct DP_Iovec* iov;

    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        iov = msg->msg_iov + i;
        // iov->base为NULL且长度不为0的情况在之前就已经判断过了
        if (iov->iov_len == 0) {
            continue;
        }
        readed = PBUF_ChainRead(chain, iov->iov_base, iov->iov_len, peek, 0);
        if (readed == 0) {
            break;
        }
        ret += (ssize_t)readed;
        if (readed < iov->iov_len) {
            break;
        }
    }

    return ret;
}

/*
    根据内核协议栈UDP Recvmsg进行整改，内核对应操作
    1. 如果传入iov->iov_len为0，则将数据拷贝到下一个有效的iov中
    2. 如果所有有效的iov长度为0，则返回为0
    3. 如果iov->iov_base为NULL 返回EFAULT错误码
    5. 只要iov->iov_len>0，这时无论报文长度为多少都只会读取一个报文，该报文读取后就被释放
*/
static ssize_t SockPbufCopy(DP_Pbuf_t* pbuf, struct DP_Msghdr* msg)
{
    ssize_t ret = 0;
    struct DP_Iovec* iov = msg->msg_iov;
    size_t cnt = 0;
    while (iov->iov_len == 0 && cnt < msg->msg_iovlen) {
        cnt++;
        iov = msg->msg_iov + cnt;
    }

    if (cnt == msg->msg_iovlen) {
        return 0;
    }

    if (iov->iov_base == NULL) {
        return -EFAULT;
    }

    ret = DP_PbufCopy(pbuf, iov->iov_base, iov->iov_len);
    if (ret <= 0) {
        return ret;
    }

    return ret;
}

ssize_t SOCK_PopRcvBuf(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen)
{
    ssize_t ret = 0;

    (void)msgDataLen;

    if (msg->msg_name != NULL) {
        ASSERT(sk->ops->getDstAddr != NULL);
        ret = sk->ops->getDstAddr(sk, NULL, msg->msg_name, &msg->msg_namelen);
        if (ret != 0) {
            return ret;
        }
    }

    ret = SockPbufChainRead(&sk->rcvBuf, msg, (uint32_t)flags & DP_MSG_PEEK);
    if (ret == 0) {
        // 如果可以收取更多数据，则返回-EAGAIN，否则返回0
        if (SOCK_CAN_RECV_MORE(sk)) {
            return -EAGAIN;
        }
        return 0;
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

uint16_t SOCK_PbufAppendMsg(DP_Pbuf_t* pbuf, const struct DP_Msghdr* msg)
{
    size_t             i;
    struct DP_Iovec* iov;
    uint16_t           ret = 0;

    ASSERT(msg->msg_iov != NULL);

    for (i = 0; i < msg->msg_iovlen; i++) {
        iov = msg->msg_iov + i;
        if (iov->iov_base == NULL || iov->iov_len <= 0) {
            continue;
        }
        ret += PBUF_Append(pbuf, iov->iov_base, (uint16_t)iov->iov_len);
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
            return -1;
        }
        return 0;
    }

    if ((int)addrlen < (int)sizeof(struct DP_SockaddrIn6)) {
        return -1;
    }

    return 0;
}

ssize_t SOCK_GetMsgIovLen(const struct DP_Msghdr* msg)
{
    size_t ret = 0;
    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        if ((ssize_t)(msg->msg_iov[i].iov_len) < 0) {
            return -EINVAL;
        }
        if (msg->msg_iov[i].iov_len == 0) {
            continue;
        }
        if (msg->msg_iov[i].iov_base == NULL) {
            return -EFAULT;
        }
        if (MAX_IOV_LEN - ret < msg->msg_iov[i].iov_len) {
            return -EINVAL;
        }
        ret += msg->msg_iov[i].iov_len;
    }
    return (ssize_t)ret;
}

int SOCK_Create(NS_Net_t* net, int domain, int type, int protocol, Sock_t** sk)
{
    const SOCK_FamilyOps_t* ops = GetFamilyOps(domain);
    SOCK_CreateSkFn_t       createFn;
    int                     ret;

    if (ops == NULL) {
        DP_LOG_ERR("Sock create failed, ops null");
        return -EAFNOSUPPORT;
    }

    ret = ops->lookup(type, protocol, &createFn);
    if (ret != 0) {
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
    SOCK_DisableNotify(sk);

    SOCK_SET_CLOSED(sk);
    // close表示用户侧不在操作此socket资源，仅有实例内部操作，交给具体协议实现释放内存，以及释放锁
    ret = sk->ops->close(sk);

    return ret;
}

int SOCK_Shutdown(Sock_t *sk, int how)
{
    int ret;

    if (how != DP_SHUT_RD && how != DP_SHUT_WR && how != DP_SHUT_RDWR) {
        DP_LOG_ERR("Sock shutdown failed, how %d invalid", how);
        return -EINVAL;
    }

    SOCK_Lock(sk);
    // 当前仅支持TCP shutdown
    if (sk->ops->shutdown == NULL) {
        SOCK_Unlock(sk);
        DP_LOG_ERR("Sock shutdown failed, shutdown not support");
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
        PBUF_ChainClean(&sk->rcvBuf);
    }

    if (how == DP_SHUT_WR || how == DP_SHUT_RDWR) {
        SOCK_SET_SHUTWR(sk);
        SOCK_CLR_SEND_MORE(sk);
        SOCK_SET_CANTSENDMORE(sk);
    }

    SOCK_Unlock(sk);
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
        DP_LOG_ERR("Sock connect failed, addr NULL.");
        return -EFAULT;
    }

    if (SOCK_CheckAddrLen(sk, addrlen) != 0) {
        DP_LOG_ERR("Sock connect failed, addrlen invalid.");
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
        DP_LOG_ERR("Sock listen failed, listen not support");
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
        DP_LOG_ERR("Sock accept failed, param null");
        goto out;
    }

    if ((addr != NULL) && ((int)*addrlen < 0)) {
        DP_LOG_ERR("Sock accept failed, param invalid");
        goto out;
    }

    if (sk->ops->accept == NULL) {
        ret = -EOPNOTSUPP;
        DP_LOG_ERR("Sock accept failed, accept null");
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
                DP_LOG_ERR("Sock ioctl failed, arg invalid");
                err = -EFAULT;
            } else {
                sk->nonblock = *(int*)arg == 0 ? 0 : 1;
            }
            break;
        default:
            err = -EINVAL;
            DP_LOG_ERR("Sock ioctl failed, request %d not support", request);
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
            DP_LOG_ERR("Sock fcntl failed, cmd %d not support", cmd);
            break;
    }

    SOCK_Unlock(sk);

    return ret;
}

ssize_t SOCK_Sendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags)
{
    ssize_t ret;
    size_t sendLen = 0;
    size_t index = 0;
    size_t offset = 0;
    if (((uint32_t)flags | DP_MSG_DONTWAIT) != DP_MSG_DONTWAIT) {
        return -EOPNOTSUPP;
    }
    ssize_t msgDataLen = SOCK_GetMsgDataLen(msg);
    if (msgDataLen < 0) {
        return msgDataLen;
    } else if (msgDataLen == 0) {
        return 0;
    }

    SOCK_Lock(sk);

    while (1) {
        // index和offset作为输入输出参数，记录当前已发送的数据长度，下次发送时直接偏移至指定位置发送
        ret = sk->ops->sendmsg(sk, msg, flags, (size_t)msgDataLen, &index, &offset);
        if ((ret < 0 && ret != -EAGAIN) ||
            ((uint32_t)flags & DP_MSG_DONTWAIT) != 0 || sk->nonblock != 0) {
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

    SOCK_Unlock(sk);
    return ret;
}

ssize_t SOCK_Sendto(
    Sock_t* sk, const void* buf, size_t len, int flags, const struct DP_Sockaddr* dstAddr, DP_Socklen_t addrlen)
{
    struct DP_Msghdr msg;
    struct DP_Iovec  iov[1];

    if (len == 0) {
        return 0;
    }

    if (buf == NULL) {
        DP_LOG_ERR("Sock send failed, param invalid");
        return -EFAULT;
    }

    iov->iov_base      = (void*)buf;
    iov->iov_len       = len;
    msg.msg_name       = (void*)dstAddr;
    msg.msg_namelen    = addrlen;
    msg.msg_controllen = 0;
    msg.msg_control    = NULL;
    msg.msg_iov        = iov;
    msg.msg_iovlen     = ARRAY_SIZE(iov);

    return SOCK_Sendmsg(sk, &msg, flags);
}

ssize_t SOCK_Recvmsg(Sock_t* sk, struct DP_Msghdr* msg, int flags)
{
    ssize_t ret;
    if (((uint32_t)flags | DP_MSG_RECV_SUPPORT_FLAGS) != DP_MSG_RECV_SUPPORT_FLAGS) {
        return -EOPNOTSUPP;
    }
    ssize_t msgDataLen = SOCK_GetMsgDataLen(msg);
    if (msgDataLen < 0) {
        return msgDataLen;
    } else if (msgDataLen == 0) {
        return 0;
    }

    SOCK_Lock(sk);

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

    SOCK_Unlock(sk);
    return ret;
}

ssize_t SOCK_Recvfrom(
    Sock_t* sk, void* buf, size_t len, int flags, struct DP_Sockaddr* srcAddr, DP_Socklen_t* addrlen)
{
    ssize_t          ret;
    struct DP_Msghdr msg;
    struct DP_Iovec  iov[1];

    if (len == 0) {
        return 0;
    }

    if (buf == NULL) {
        DP_LOG_ERR("Sock recv failed, param invalid");
        return -EFAULT;
    }

    iov->iov_base      = buf;
    iov->iov_len       = len;
    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;
    msg.msg_iov        = iov;
    msg.msg_iovlen     = ARRAY_SIZE(iov);

    if (srcAddr != NULL && addrlen != NULL) {
        msg.msg_name    = srcAddr;
        msg.msg_namelen = *addrlen;
    }

    ret = SOCK_Recvmsg(sk, &msg, flags);

    if (addrlen != NULL) {
        *addrlen = msg.msg_namelen;
    }

    return ret;
}

static int SockSetTimeout(const void* optval, DP_Socklen_t optlen, int* timeout)
{
    struct DP_Timeval* tv = (struct DP_Timeval*)optval;

    if (tv == NULL || (int)optlen < (int)sizeof(struct DP_Timeval)) {
        DP_LOG_ERR("Sock set timeout failed, invalid param");
        return -EINVAL;
    }

    // usec需要在0~1000 ms内
    if ((tv->tv_usec < 0) || (tv->tv_usec >= USEC_PER_SEC)) {
        DP_LOG_ERR("Sock set timeout failed, usec out of range");
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

static int SockSetKeepalive(const void* optval, DP_Socklen_t optlen, Sock_t* sk)
{
    if ((int)optlen < (int)sizeof(int)) {
        DP_LOG_ERR("Sock set keepalive failed, optlen %u invalid", optlen);
        return -EINVAL;
    }

    if (sk->ops->keepalive != NULL) {
        return sk->ops->keepalive(sk, *(int *)optval == 0 ? 0 : 1);
    }

    return 0;
}

static int SockSetLinger(const void* optval, DP_Socklen_t optlen, Sock_t* sk)
{
    if ((int)optlen < (int)sizeof(struct DP_Linger)) {
        DP_LOG_ERR("Sock set linger failed, optlen %u invalid", optlen);
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

static int SockSetReuseAddr(const void* optval, DP_Socklen_t optlen, Sock_t* sk)
{
    if ((int)optlen < (int)sizeof(int)) {
        DP_LOG_ERR("Sock set reuseAddr failed, optlen %u invalid", optlen);
        return -EINVAL;
    }
    sk->reuseAddr = *(int *)optval == 0 ? 0 : 1;
    return 0;
}

static int SockSetReusePort(const void* optval, DP_Socklen_t optlen, Sock_t* sk)
{
    if ((int)optlen < (int)sizeof(int)) {
        DP_LOG_ERR("Sock set reusePort failed, optlen %u invalid", optlen);
        return -EINVAL;
    }
    sk->reusePort = *(int *)optval == 0 ? 0 : 1;
    return 0;
}

static uint32_t GetValByRange(uint32_t high, uint32_t low, uint32_t val)
{
    uint32_t bufVal = val;
    bufVal = (bufVal > low) ? bufVal : low;
    bufVal = (bufVal < high) ? bufVal : high;
    return bufVal;
}

static int SockSetSndBuf(const void* optval, DP_Socklen_t optlen, Sock_t* sk)
{
    if (((int)optlen < (int)sizeof(int)) || (*(int *)optval < 0)) {
        DP_LOG_ERR("Sock set sndbuf failed with param invalid");
        return -EINVAL;
    }

    uint32_t hiWat = *(uint32_t *)optval;

    hiWat = GetValByRange((uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_WMEM_MAX), sk->sndHiwat, hiWat);
    sk->sndHiwat = hiWat;
    if (sk->sndLowat > sk->sndHiwat) {
        sk->sndLowat = sk->sndHiwat;
    }

    return 0;
}

static int SockSetRcvBuf(const void* optval, DP_Socklen_t optlen, Sock_t* sk)
{
    if (((int)optlen < (int)sizeof(int)) || (*(int *)optval < 0)) {
        DP_LOG_ERR("Sock set rcvbuf failed with param invalid");
        return -EINVAL;
    }

    uint32_t hiWat = *(uint32_t *)optval;

    hiWat = GetValByRange((uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RMEM_MAX), sk->rcvHiwat, hiWat);
    sk->rcvHiwat = hiWat;
    if (sk->rcvLowat > sk->rcvHiwat) {
        sk->rcvLowat = sk->rcvHiwat;
    }

    return 0;
}

int SOCK_Setsockopt(Sock_t* sk, int level, int optname, const void* optval, DP_Socklen_t optlen)
{
    int ret = 0;

    (void)level;

    if (optval == NULL) {
        DP_LOG_ERR("Sock setOpt optval invalid null");
        ret = -EFAULT;
        return ret;
    }

    switch (optname) {
        case DP_SO_SNDTIMEO:
            ret = SockSetTimeout(optval, optlen, &sk->sndTimeout);
            break;
        case DP_SO_RCVTIMEO:
            ret = SockSetTimeout(optval, optlen, &sk->rcvTimeout);
            break;
        case DP_SO_REUSEADDR:
            ret = SockSetReuseAddr(optval, optlen, sk);
            break;
        case DP_SO_REUSEPORT:
            ret = SockSetReusePort(optval, optlen, sk);
            break;
        case DP_SO_KEEPALIVE:
            ret = SockSetKeepalive(optval, optlen, sk);
            break;
        case DP_SO_LINGER:
            ret = SockSetLinger(optval, optlen, sk);
            break;
        case DP_SO_SNDBUF:
            ret = SockSetSndBuf(optval, optlen, sk);
            break;
        case DP_SO_RCVBUF:
            ret = SockSetRcvBuf(optval, optlen, sk);
            break;
        default:
            ret = -ENOPROTOOPT;
            break;
    }

    return ret;
}

static int SockGetTimeout(const void* optval, DP_Socklen_t *optlen, int timeout)
{
    if (*optlen < sizeof(struct DP_Timeval)) {
        DP_LOG_ERR("Sock timeval with optlen %u invalid", *optlen);
        return -EINVAL;
    }

    if (timeout < 0) {
        ((struct DP_Timeval *)optval)->tv_sec = 0;
        ((struct DP_Timeval *)optval)->tv_usec = 0;
        return 0;
    }

    ((struct DP_Timeval *)optval)->tv_sec = timeout / MSEC_PER_SEC;
    ((struct DP_Timeval *)optval)->tv_usec = (timeout % MSEC_PER_SEC) * USEC_PER_MSEC;
    return 0;
}

static int SockGetLinger(const void *optval, DP_Socklen_t *optlen, Sock_t *sk)
{
    if (*optlen < sizeof(struct DP_Linger)) {
        DP_LOG_ERR("Sock linget with optlen %u invalid", *optlen);
        return -EINVAL;
    }

    ((struct DP_Linger *)optval)->l_onoff = sk->lingerOnoff;
    ((struct DP_Linger *)optval)->l_linger = sk->linger;
    return 0;
}

static int CheckOptVal(void* optval, DP_Socklen_t* optlen)
{
    if ((optval == NULL) || (optlen == NULL)) {
        DP_LOG_ERR("Sock getOpt invalid param");
        return -EFAULT;
    }

    if ((int)*optlen < (int)sizeof(DP_Socklen_t)) {
        return -EINVAL;
    }
    return 0;
}

int SOCK_Getsockopt(Sock_t* sk, int level, int optname, void* optval, DP_Socklen_t* optlen)
{
    (void)level;
    int ret = CheckOptVal(optval, optlen);
    if (ret != 0) {
        return ret;
    }

    switch (optname) {
        case DP_SO_SNDTIMEO:
            ret = SockGetTimeout(optval, optlen, sk->sndTimeout);
            *optlen = sizeof(struct DP_Timeval);
            break;
        case DP_SO_RCVTIMEO:
            ret = SockGetTimeout(optval, optlen, sk->rcvTimeout);
            *optlen = sizeof(struct DP_Timeval);
            break;
        case DP_SO_REUSEADDR:
            *(int *)optval = sk->reuseAddr;
            *optlen = sizeof(DP_Socklen_t);
            break;
        case DP_SO_REUSEPORT:
            *(int *)optval = sk->reusePort;
            *optlen = sizeof(DP_Socklen_t);
            break;
        case DP_SO_KEEPALIVE:
            *(int *)optval = sk->keepalive;
            *optlen = sizeof(DP_Socklen_t);
            break;
        case DP_SO_LINGER:
            ret = SockGetLinger(optval, optlen, sk);
            *optlen = sizeof(struct DP_Linger);
            break;
        case DP_SO_SNDBUF:
            *(uint32_t *)optval = sk->sndHiwat * 2; // 接口行为与内核保持一致，设置缓冲区大小为A，获取到的结果为A * 2
            *optlen = sizeof(DP_Socklen_t);
            break;
        case DP_SO_RCVBUF:
            *(uint32_t *)optval = sk->rcvHiwat * 2; // 接口行为与内核保持一致，设置缓冲区大小为A，获取到的结果为A * 2
            *optlen = sizeof(DP_Socklen_t);
            break;
        case DP_SO_ERROR:
            *(int *)optval = sk->error;
            *optlen = sizeof(DP_Socklen_t);
            sk->error = 0;
            break;
        default:
            ret = -ENOPROTOOPT;
            break;
    }

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
        DP_LOG_ERR("Sock getpeername param invalid");
        return ret;
    }

    if (*addrlen == 0) {
        return 0;
    }

    SOCK_Lock(sk);

    if (SOCK_IS_SHUTWR(sk) || SOCK_IS_SHUTRD(sk)) {
        SOCK_Unlock(sk);
        return -EINVAL;
    }

    if (!SOCK_IS_CONNECTED(sk)) {
        SOCK_Unlock(sk);
        return -ENOTCONN;
    }

    ASSERT(sk->ops->getAddr != NULL);

    ret = sk->ops->getAddr(sk, addr, addrlen, 1);

    SOCK_Unlock(sk);

    return ret;
}

int SOCK_Getsockname(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen)
{
    int ret = SOCK_CheckAddrParam(addr, addrlen);
    if (ret != 0) {
        DP_LOG_ERR("Sock getsockname param invalid");
        return ret;
    }

    if (*addrlen == 0) {
        return 0;
    }

    SOCK_Lock(sk);

    if (SOCK_IS_SHUTWR(sk) || SOCK_IS_SHUTRD(sk)) {
        SOCK_Unlock(sk);
        return -EINVAL;
    }

    ASSERT(sk->ops->getAddr != NULL);

    ret = sk->ops->getAddr(sk, addr, addrlen, 0);

    SOCK_Unlock(sk);

    return ret;
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

void SOCK_Notify(Sock_t* sk, uint8_t oldState)
{
    if (sk->notifyType <= SOCK_NOTIFY_TYPE_NONE || sk->notifyType >= SOCK_NOTIFY_TYPE_MAX) {
        return;
    }

    if (g_notifyFns[sk->notifyType] != NULL) {
        g_notifyFns[sk->notifyType](sk, sk->notifyCtx, oldState, sk->state);
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

void SOCK_EnableNotify(Sock_t* sk, int type, void* ctx, int assocFd)
{
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
    }

    sk->ref = 1;
    sk->dev = NULL;

    return 0;
}

void SOCK_DeinitSk(Sock_t* sk)
{
    if (sk->dev != NULL) {
        NETDEV_DerefDev(sk->dev);
    }

    PBUF_ChainClean(&sk->rcvBuf);
    PBUF_ChainClean(&sk->sndBuf);
    SEM_DEINIT(sk->rdSem);
    SEM_DEINIT(sk->wrSem);
    SPINLOCK_Deinit(&sk->lock);
}

size_t SOCK_SIZE;

int SOCK_Init(int slave)
{
    void (*notifyFns[])(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState) = {
        NULL,
        EPOLL_Notify,
        POLL_Notify,
        SELECT_Notify,
    };

    if (FD_Init() != 0) {
        DP_LOG_ERR("Sock init fd failed.");
        FD_Deinit();
        return -1;
    }

    if (INET_Init(slave) != 0) {
        DP_LOG_ERR("Inet init sock_familyOps failed.");
        return -1;
    }

#ifdef DPITF_NETLINK
    if (SOCK_InitNetlink(slave) != 0) {
        DP_LOG_ERR("Netlink init sock_familyOps failed.");
        return -1;
    }
#endif

    SOCK_SIZE = SOCK_ALIGN_SIZE(sizeof(Sock_t));
    SOCK_SIZE += SOCK_ALIGN_SIZE(SEM_Size) * 2; // 包含读写2个信息量

    for (int i = 0; i < (int)ARRAY_SIZE(notifyFns); i++) {
        if (notifyFns[i] == NULL) {
            continue;
        }
        if (SOCK_SetNotifyFn(i, notifyFns[i]) != 0) {
            return -1;
        }
    }

    return 0;
}

void SOCK_Deinit(int slave)
{
    INET_Deinit(slave);
    FD_Deinit();
}
