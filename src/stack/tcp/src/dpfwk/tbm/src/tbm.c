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

#include "tbm.h"

#include "dp_tbm_api.h"
#include "dp_tbm.h"

#include "dp_inet.h"
#include "dp_errno.h"
#include "dp_ethernet.h"

#include "utils_log.h"
#include "ns.h"
#include "netdev.h"

#include "rt.h"
#include "nd.h"
#include "tbm_notify.h"
#include "tbm_utils.h"

static void AddAddrExceptionHandle(RtItem_t* rtItem, Netdev_t* dev, NETDEV_IfAddr_t* ifAddr)
{
    if (rtItem != NULL) {
        FreeRtItem(rtItem);
    }
    if (dev->in.ifAddr != NULL) {
        NETDEV_FreeIfAddr(dev->in.ifAddr);
        dev->in.ifAddr = NULL;
    }
    if (ifAddr != NULL) {
        NETDEV_FreeIfAddr(ifAddr);
    }
}

static int AddAddr(Netdev_t* dev, DP_IfaddrMsg_t* msg, DP_TbmAttr_t** attrs, int attrCnt)
{
    RtItem_t*        rtItem;
    NETDEV_IfAddr_t* ifAddr;
    int              ret;
    DP_InAddr_t    addr = 0;

    if (dev->in.ifAddr != NULL) {
        return -EEXIST;
    }

    for (int i = 0; i < attrCnt; i++) {
        if (attrs[i]->type == DP_IFA_LOCAL) {
            addr = *DP_TBM_ATTR_GET_VAL(attrs[i], DP_InAddr_t);
        }
    }
    RtItem_t* old = GetRt(NS_GET_RT_TBL(dev->net), addr);
    if (old != NULL) {
        PutRt(old);
        return -EEXIST;
    }

    rtItem = AllocRtItem();
    ifAddr = NETDEV_AllocIfAddr();
    if (rtItem == NULL || ifAddr == NULL) {
        ret = -ENOMEM;
        goto err;
    }

    rtItem->key.mask = DP_MakeNetmask(msg->prefix);
    rtItem->key.addr = (addr & rtItem->key.mask);

    ifAddr->local     = addr;
    ifAddr->broadcast = DP_INADDR_ANY;
    ifAddr->mask      = rtItem->key.mask;
    ifAddr->dev       = dev;
    ifAddr->next      = NULL;

    rtItem->nxtHop   = DP_INADDR_ANY;
    rtItem->ifaddr   = ifAddr;
    rtItem->ref      = 1;
    rtItem->valid    = 1;

    dev->in.ifAddr = NETDEV_CopyIfAddr(ifAddr);
    if (dev->in.ifAddr == NULL) {
        ret = -ENOMEM;
        goto err;
    }

    ret = InsertRt(NS_GET_RT_TBL(dev->net), rtItem);
    if (ret != 0) {
        goto err;
    }

    return 0;

err:
    AddAddrExceptionHandle(rtItem, dev, ifAddr);
    return ret;
}

static int AddrAttrCheck(uint8_t family, DP_TbmAttr_t** attrs, int attrCnt)
{
    uint16_t expLen[] = {
        [DP_IFA_UNSPEC] = 0,
        [DP_IFA_BROADCAST] = DP_TBM_ATTR_LENGTH(sizeof(DP_InAddr_t)),
        [DP_IFA_LOCAL] = DP_TBM_ATTR_LENGTH(sizeof(DP_InAddr_t)),
    };
    if (family == DP_AF_INET6) {
        expLen[DP_IFA_BROADCAST] = DP_TBM_ATTR_LENGTH(sizeof(DP_In6Addr_t));
        expLen[DP_IFA_LOCAL] = DP_TBM_ATTR_LENGTH(sizeof(DP_In6Addr_t));
    }
    int local = 0;

    for (int i = 0; i < attrCnt; i++) {
        if (attrs[i]->type >= DP_IFA_MAX) {
            return -EINVAL;
        }

        if (attrs[i]->len != expLen[attrs[i]->type]) {
            return -EINVAL;
        }

        if (attrs[i]->type == DP_IFA_LOCAL) {
            local = 1;
        }
    }

    if (local == 0) {
        return -EINVAL;
    }

    return 0;
}

static int AddrParamCheck(DP_IfaddrMsg_t* msg, DP_TbmAttr_t** attrs, int attrCnt)
{
    // prefix不可超过32位
    if (msg == NULL || attrs == NULL || attrCnt == 0) {
        return -1;
    }

    if (msg->flags != 0 || msg->scope != 0) {
        return -1;
    }

    if (msg->family == DP_AF_INET && msg->prefix <= DP_INET_MASK_LEN) {
        return 0;
    }
    return -1;
}

static int TBM_ProcIfaMsg(NS_Net_t* net, DP_IfAddrOp_t op, DP_IfaddrMsg_t* msg, DP_TbmAttr_t** attrs, int attrCnt)
{
    int       ret = -EINVAL;
    Netdev_t* dev;

    if (AddrParamCheck(msg, attrs, attrCnt) != 0) {
        return ret;
    }

    if (AddrAttrCheck(msg->family, attrs, attrCnt) != 0) {
        return ret;
    }

    NS_Lock(net);

    dev = NETDEV_GetDev(net, msg->ifindex);
    if (dev == NULL) {
        goto out;
    }

    switch (op) {
        case DP_NEWADDR:
            if (msg->family == DP_AF_INET) {
                ret = AddAddr(dev, msg, attrs, attrCnt);
            } 
            break;
        case DP_DELADDR:
        default:
            break;
    }

out:
    NS_Unlock(net);

    return ret;
}

int DP_IfaCfg(DP_IfAddrOp_t op, DP_IfaddrMsg_t* msg, DP_TbmAttr_t** attrs, int attrCnt)
{
    NS_Net_t* net = NS_GetDftNet();

    return TBM_ProcIfaMsg(net, op, msg, attrs, attrCnt);
}

static int CheckNdAttr(uint8_t family, DP_TbmAttr_t* attrs[DP_NDA_MAX])
{
    if (attrs[DP_NDA_DST] == NULL) {
        return -EINVAL;
    }
    if (family != DP_AF_INET && family != DP_AF_INET6) {
        return -EINVAL;
    }
    if (family == DP_AF_INET && attrs[DP_NDA_DST]->len != DP_TBM_ATTR_LENGTH(sizeof(DP_InAddr_t))) {
        return -EINVAL;
    }
    if (family == DP_AF_INET6 && attrs[DP_NDA_DST]->len != DP_TBM_ATTR_LENGTH(sizeof(DP_In6Addr_t))) {
        return -EINVAL;
    }
    if (attrs[DP_NDA_NS] != NULL && attrs[DP_NDA_NS]->len != DP_TBM_ATTR_LENGTH(sizeof(int))) {
        return -EINVAL;
    }

    return 0;
}

static int AddNd(Netdev_t* dev, NS_Net_t* net, DP_NdMsg_t* msg, DP_TbmAttr_t* attrs[DP_NDA_MAX])
{
    TBM_NdItem_t* item = NULL;
    TBM_NdItem_t* old;
    TBM_IpAddr_t  dst = {0};
    NdTbl_t*      tbl = NS_GET_ND_TBL(net);

    if (msg->family == DP_AF_INET) {
        dst.ipv4 = *DP_TBM_ATTR_GET_VAL(attrs[DP_NDA_DST], DP_InAddr_t);
    } else {
        dst.ipv6 = *DP_TBM_ATTR_GET_VAL(attrs[DP_NDA_DST], DP_In6Addr_t);
    }

    old = GetNd(tbl, dst);
    if (old == NULL && attrs[DP_NDA_LLADDR] == NULL) {
        return -EINVAL;
    }

    item = AllocNdItem();
    if (item == NULL) {
        if (old != NULL) {
            PutNd(old);
        }
        return -ENOMEM;
    }

    TBM_IPADDR_COPY(&item->dst, &dst);
    item->state = msg->state;
    item->flags = 0;
    item->type  = 0;
    item->ref   = 1;
    item->dev   = dev;
    item->valid = 1;

    if (attrs[DP_NDA_LLADDR] != NULL) {
        DP_MAC_COPY(&item->mac, DP_TBM_ATTR_GET_VAL(attrs[DP_NDA_LLADDR], DP_EthAddr_t));
    } else {
        DP_MAC_COPY(&item->mac, &old->mac);
    }
    if (old != NULL) {
        PutNd(old);
    }

    if (InsertNd(tbl, item) != 0) {
        FreeNdItem(item);
        return -ENOMEM;
    }
    DP_LOG_DBG("Insert arp tbl item");
    if (item->state == DP_ND_STATE_REACHABLE || item->state == DP_ND_STATE_PERMANENT) {
        RemoveFakeNdItem(tbl, dst);
    }

    (void)NETDEV_RefDev(dev);

    return 0;
}

static int DelNd(Netdev_t* dev, NS_Net_t* net, DP_NdMsg_t* msg, DP_TbmAttr_t* attrs[DP_NDA_MAX])
{
    NdTbl_t*      tbl = NS_GET_ND_TBL(net);
    TBM_IpAddr_t dst = {0};
    if (msg->family == DP_AF_INET) {
        dst.ipv4 = *DP_TBM_ATTR_GET_VAL(attrs[DP_NDA_DST], DP_InAddr_t);
    } else {
        dst.ipv6 = *DP_TBM_ATTR_GET_VAL(attrs[DP_NDA_DST], DP_In6Addr_t);
    }
    DP_LOG_DBG("Remove arp tbl item");
    if (RemoveNd(tbl, dst, dev) != 0) {
        DP_LOG_DBG("Remove the rt fail.");
        return -ENOENT;
    }
    return 0;
}

static int TBM_ProcNdMsg(NS_Net_t* net, DP_NdOp_t op, DP_NdMsg_t* msg, DP_TbmAttr_t* attrs[DP_NDA_MAX])
{
    int       ret = -EINVAL;
    Netdev_t* dev;

    NS_Lock(net);

    dev = NETDEV_GetDev(net, msg->ifindex);
    if (dev == NULL) {
        goto out;
    }

    switch (op) {
        case DP_NEW_ND: {
            ret = AddNd(dev, net, msg, attrs);
            break;
        }
        case DP_DEL_ND: {
            ret = DelNd(dev, net, msg, attrs);
            break;
        }
        default:
            break;
    }

out:
    NS_Unlock(net);

    return ret;
}

int DP_NdCfg(DP_NdOp_t op, DP_NdMsg_t* msg, DP_TbmAttr_t* attrs[], int attrCnt)
{
    NS_Net_t* net = NS_GetDftNet();
    int       ret = -EINVAL;

    DP_TbmAttr_t* inAttrs[DP_NDA_MAX] = { 0 };

    if (msg == NULL || msg->ifindex < 0 || attrs == NULL) {
        return ret;
    }

    if ((op != DP_DEL_ND) && (msg->state & 0xFF) == 0) {
        return ret;
    }

    for (int i = 0; i < attrCnt; i++) {
        if (attrs[i] == NULL || attrs[i]->type >= DP_NDA_MAX) {
            return ret;
        }
        inAttrs[attrs[i]->type] = attrs[i];
    }

    ret = CheckNdAttr(msg->family, inAttrs);
    if (ret != 0) {
        return ret;
    }

    if (inAttrs[DP_NDA_NS] != NULL) {
        net = NS_GetNet(*DP_TBM_ATTR_GET_VAL(inAttrs[DP_NDA_NS], int));
    }
    if (net == NULL) {
        return -EINVAL;
    }

    return TBM_ProcNdMsg(net, op, msg, inAttrs);
}

static int CheckRtAttr(DP_RtOpt_t op, DP_RtInfo_t* msg, DP_TbmAttr_t* attrs[DP_RTA_MAX])
{
    uint16_t checkLen = 0;
    if (msg->family ==  DP_AF_INET && msg->dstLen <= DP_INET_MASK_LEN) {
        checkLen = DP_TBM_ATTR_LENGTH(sizeof(DP_InAddr_t));
    } else {
        DP_LOG_ERR("CheckRtAttr failed, msg info err, msg->family = %u, msg->dstLen = %u!", msg->family, msg->dstLen);
        return -EINVAL;
    }

    // 添加、删除都需要配置DST属性
    if (attrs[DP_RTA_DST] == NULL || attrs[DP_RTA_DST]->len != checkLen) {
        DP_LOG_ERR("CheckRtAttr failed, DP_RTA_DST err, checkLen = %u!", checkLen);
        return -EINVAL;
    }

    if (op == DP_NEW_ROUTE) {
        if (attrs[DP_RTA_GATEWAY] == NULL || attrs[DP_RTA_GATEWAY]->len != checkLen) {
            DP_LOG_ERR("CheckRtAttr failed, DP_RTA_GATEWAY err, checkLen = %u!", checkLen);
            return -EINVAL;
        }
        if (attrs[DP_RTA_OIF] == NULL || attrs[DP_RTA_OIF]->len != DP_TBM_ATTR_LENGTH(sizeof(int))) {
            DP_LOG_ERR("CheckRtAttr failed, DP_RTA_OIF err!");
            return -EINVAL;
        }
    }

    if (attrs[DP_RTA_NS] != NULL && attrs[DP_RTA_NS]->len != DP_TBM_ATTR_LENGTH(sizeof(int))) {
        DP_LOG_ERR("CheckRtAttr failed, DP_RTA_NS err!");
        return -EINVAL;
    }

    return 0;
}

static uint32_t CommonPrefixLen(DP_InAddr_t src, DP_InAddr_t dst)
{
    uint32_t count = 0;
    uint32_t x     = ~(src ^ dst);
    uint32_t mask  = (1U << 31);

    while ((x & mask) != 0U) {
        x <<= 1U;
        count++;
    }
    return count;
}

static NETDEV_IfAddr_t* MatchSrcAddr(NS_Net_t* net, int outIf, DP_InAddr_t nxtHop)
{
    Netdev_t*        outDev;
    NETDEV_IfAddr_t* ifAddr;
    uint32_t         maxPrefixLen = 0;
    uint32_t         curPrefixLen = 0;
    NETDEV_IfAddr_t* retAddr;

    outDev = NETDEV_GetDev(net, outIf);
    if (outDev == NULL) {
        return NULL;
    }

    ifAddr  = outDev->in.ifAddr;
    retAddr = ifAddr;
    while (ifAddr != NULL) {
        if ((ifAddr->local & ifAddr->mask) == (nxtHop & ifAddr->mask)) {
            curPrefixLen = CommonPrefixLen(nxtHop, ifAddr->local);
            if (curPrefixLen > maxPrefixLen) {
                maxPrefixLen = curPrefixLen;
                retAddr      = ifAddr;
            }
        }

        ifAddr = ifAddr->next;
    }

    return retAddr;
}

static int TryInsertRtItem(NS_Net_t* net, NETDEV_IfAddr_t* ifAddr, RtKey_t* rtKey, DP_InAddr_t nxtHop)
{
    RtItem_t* item = AllocRtItem();
    if (item == NULL) {
        return -ENOMEM;
    }

    item->ifaddr = ifAddr;
    item->nxtHop = nxtHop;
    item->key    = *rtKey;
    item->ref    = 1;
    item->valid  = 1;

    int ret = InsertRt(NS_GET_RT_TBL(net), item);
    if (ret != 0) {
        DP_LOG_ERR("Insert the rt fail\n");
        FreeRtItem(item);
        return -EAGAIN;
    }

    return 0;
}

static int AddRt(NS_Net_t* net, DP_RtInfo_t* msg, DP_TbmAttr_t* attrs[DP_RTA_MAX])
{
    int              outIf;
    NETDEV_IfAddr_t* ifAddr;
    NETDEV_IfAddr_t* srcAddr;
    int              ret;
    RtTbl_t*         tbl = NS_GET_RT_TBL(net);
    DP_InAddr_t      nxtHop;
    RtKey_t          rtKey;

    rtKey.addr = *DP_TBM_ATTR_GET_VAL(attrs[DP_RTA_DST], DP_InAddr_t);
    rtKey.mask = DP_MakeNetmask(msg->dstLen);

    /* 目的网段/HOST地址长度，超出了掩码长度，认为输入不合法 */
    if ((rtKey.addr & (~rtKey.mask)) != 0) {
        return -EINVAL;
    }

    if (rtKey.addr != DP_INADDR_ANY && LookupRt(tbl, &rtKey) != NULL) {
        return -EEXIST;
    }

    nxtHop = *DP_TBM_ATTR_GET_VAL(attrs[DP_RTA_GATEWAY], DP_InAddr_t);
    outIf  = *DP_TBM_ATTR_GET_VAL(attrs[DP_RTA_OIF], int);

    srcAddr = MatchSrcAddr(net, outIf, nxtHop);
    if (srcAddr == NULL) {
        return -EAGAIN;
    }

    ifAddr = NETDEV_AllocIfAddr();
    if (ifAddr == NULL) {
        return -ENOMEM;
    }

    ret = memcpy_s(ifAddr, sizeof(NETDEV_IfAddr_t), srcAddr, sizeof(NETDEV_IfAddr_t));
    if (ret != EOK) {
        DP_LOG_ERR("AddRt failed, memcpy_s ifAddr failed, ret = %d!", ret);
        NETDEV_FreeIfAddr(ifAddr);
        return -ret;
    }

    ret = TryInsertRtItem(net, ifAddr, &rtKey, nxtHop);
    if (ret != 0) {
        NETDEV_FreeIfAddr(ifAddr);
        return ret;
    }

    (void)NETDEV_RefDev(ifAddr->dev);
    DP_LOG_INFO("Insert new rt item");
    return 0;
}

static int DelRt(NS_Net_t* net, DP_RtInfo_t* msg, DP_TbmAttr_t* attrs[DP_RTA_MAX])
{
    RtTbl_t* tbl = NS_GET_RT_TBL(net);
    RtKey_t  key;

    key.addr = *DP_TBM_ATTR_GET_VAL(attrs[DP_RTA_DST], DP_InAddr_t);
    key.mask = DP_MakeNetmask(msg->dstLen);

    /* 目的网段/HOST地址长度，超出了掩码长度，认为输入不合法 */
    if ((key.addr & (~key.mask)) != 0) {
        DP_LOG_ERR("Remove the rt fail, key.addr is not match key.mask.");
        return -EINVAL;
    }

    if (RemoveRt(tbl, &key) != 0) {
        DP_LOG_ERR("Remove the rt fail.");
        return -ENOENT;
    }
    return 0;
}

static int TBM_ProcRtMsg(NS_Net_t* net, DP_RtOpt_t op, DP_RtInfo_t* msg, DP_TbmAttr_t* attrs[DP_RTA_MAX])
{
    int ret = -EINVAL;

    NS_Lock(net);

    switch (op) {
        case DP_NEW_ROUTE: {
            if (msg->family == DP_AF_INET) {
                ret = AddRt(net, msg, attrs);
            }
            break;
        }
        case DP_DEL_ROUTE: {
            if (msg->family == DP_AF_INET) {
                ret = DelRt(net, msg, attrs);
            }
            break;
        }
        default:
            break;
    }

    NS_Unlock(net);
    if (ret != 0) {
        DP_LOG_ERR("TBM_ProcRtMsg failed, op = %u, msg->family = %u, ret = %d!", op, msg->family, ret);
    }

    return ret;
}

int DP_RtCfg(DP_RtOpt_t op, DP_RtInfo_t* msg, DP_TbmAttr_t* attrs[], int attrCnt)
{
    NS_Net_t* net = NS_GetDftNet();
    int       ret = -EINVAL;

    DP_TbmAttr_t* inAttrs[DP_RTA_MAX] = { 0 };

    if (msg == NULL || attrs == NULL) {
        DP_LOG_ERR("RtCfg failed, msg or attrs is NULL!");
        return ret;
    }

    if (attrCnt <= 0) {
        DP_LOG_ERR("RtCfg failed, attrCnt is invalid!");
        return ret;
    }

    for (int i = 0; i < attrCnt; i++) {
        if (attrs[i] == NULL || attrs[i]->type >= DP_RTA_MAX) {
            DP_LOG_ERR("RtCfg failed, attr type is invalid!");
            return ret;
        }
        inAttrs[attrs[i]->type] = attrs[i];
    }

    ret = CheckRtAttr(op, msg, inAttrs);
    if (ret != 0) {
        DP_LOG_ERR("RtCfg failed, check attr invalid!");
        return ret;
    }

    if (inAttrs[DP_RTA_NS] != NULL) {
        net = NS_GetNet(*DP_TBM_ATTR_GET_VAL(inAttrs[DP_RTA_NS], int));
    }

    if (net == NULL) {
        DP_LOG_ERR("RtCfg failed, ns get net NULL!");
        return -EINVAL;
    }

    return TBM_ProcRtMsg(net, op, msg, inAttrs);
}

int DP_LinkCfg(DP_LinkOpt_t op, DP_LinkMsg_t* msg, DP_TbmAttr_t* attr[], int attrCnt)
{
    (void)op;
    (void)msg;
    (void)attr;
    (void)attrCnt;
    return 0;
}

TBM_RtItem_t* TBM_GetRtItem(NS_Net_t* net, int vrfId, DP_InAddr_t addr)
{
    RtTbl_t* tbl = NS_GET_RT_TBL(net);

    (void)vrfId;

    return GetRt(tbl, addr);
}

void TBM_PutRtItem(TBM_RtItem_t* rt)
{
    PutRt(rt);
}

TBM_NdItem_t* TBM_GetNdItem(NS_Net_t* net, int vrfId, TBM_IpAddr_t addr)
{
    NdTbl_t* tbl = NS_GET_ND_TBL(net);

    (void)vrfId;

    return GetNd(tbl, addr);
}

void TBM_PutNdItem(TBM_NdItem_t* nd)
{
    PutNd(nd);
}

int TBM_Init(int slave)
{
    (void)slave;

    NS_SetNetOps(NS_NET_FIB4, AllocRtTbl, FreeRtTbl);
    NS_SetNetOps(NS_NET_ND, AllocNdTbl, FreeNdTbl);
    NS_SetNetOps(NS_NET_NL, AllocNotifyList, FreeNotifyList);
    TbmUtilInit();

    return 0;
}

void TBM_PutFakeNdItem(TBM_NdFakeItem_t* nd)
{
    PutFakeNd(nd);
}

TBM_NdFakeItem_t* TBM_InsertFakeNd(Netdev_t* dev, TBM_IpAddr_t dst)
{
    NdTbl_t* tbl = NS_GET_ND_TBL(dev->net);

    return InsertFakeNd(tbl, dst, dev);
}

static void NotifyMiss(Netdev_t* dev, TBM_IpAddr_t dst, uint8_t family)
{
    TBM_NdItem_t tmp = {0};
    TBM_IPADDR_COPY(&tmp.dst, &dst);
    tmp.state = DP_NUD_INCOMPLETE;
    tmp.dev   = dev;

    TBM_Notify(dev->net, TBM_NOTIFY_TYPE_ND, 0, family, &tmp);
}

void TBM_UpdateFakeNdItem(Netdev_t* dev, Pbuf_t* pbuf, TBM_IpAddr_t dst)
{
    TBM_NdFakeItem_t* fakeNd = NULL;
    NdTbl_t* tbl = NS_GET_ND_TBL(dev->net);
    fakeNd = GetFakeNd(tbl, dst);
    uint8_t family = PBUF_GET_L3_TYPE(pbuf) == DP_ETH_P_IP ? DP_AF_INET : DP_AF_INET6;
    if (fakeNd == NULL) {
        // 未插入过假表，则触发arpmiss并插入假表项
        NotifyMiss(dev, dst, family);
        fakeNd = InsertFakeNd(tbl, dst, dev);
    } else if (!IsNeedNotify(fakeNd)) {
        // 超出arpmiss触发间隔，重新触发arpmiss
        NotifyMiss(dev, dst, family);
    }
    // 缓存报文
    PushNdMissCache(fakeNd, pbuf);

    if (fakeNd != NULL) {
        PutFakeNd(fakeNd);
    }
}

TBM_NdFakeItem_t* TBM_GetFakeNdItem(NS_Net_t* net, int vrfId, TBM_IpAddr_t addr)
{
    (void)vrfId;

    NdTbl_t* tbl = NS_GET_ND_TBL(net);

    return GetFakeNd(tbl, addr);
}

int TBM_GetNdCnt(NS_Net_t* net)
{
    (void)net;
    NdTbl_t* tbl = NS_GET_ND_TBL(net);
    return tbl->cnt;
}

void TBM_Deinit(void)
{
    TbmUtilDeinit();
}

static TBM_Notify_t g_cpTbmNotify;

int DP_TbmAddNotify(int ns, DP_TBM_Notify_t* tn)
{
    (void)ns;
    if (tn->groups != DP_RTMGRP_NEIGH) {
        return -EINVAL;
    }

    g_cpTbmNotify.pid = tn->pid;
    g_cpTbmNotify.groups = tn->groups;
    g_cpTbmNotify.cb = tn->cb;
    g_cpTbmNotify.ctx = tn->ctx;
    SPINLOCK_Init(&g_cpTbmNotify.lock);
    return TBM_AddNotify(NULL, &g_cpTbmNotify);
}