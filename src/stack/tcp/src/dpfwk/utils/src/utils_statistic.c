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

#include "securec.h"

#include "utils_base.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "dp_debug_api.h"
#include "utils_statistic.h"

DP_MibStat_t g_statMibs = {0};

static const DP_MibField_t g_modName[] = {
    {MOD_INIT,    "Init"},
    {MOD_CPD,     "Cpd"},
    {MOD_DBG,     "Debug"},
    {MOD_NETDEV,  "Netdev"},
    {MOD_NS,      "Namespace"},
    {MOD_PBUF,    "Pbuf"},
    {MOD_PMGR,    "Pmgr"},
    {MOD_SHM,     "Shm"},
    {MOD_TBM,     "Tbm"},
    {MOD_UTILS,   "Utils"},
    {MOD_WORKER,  "Worker"},
    {MOD_FD,      "Fd"},
    {MOD_EPOLL,   "Epoll"},
    {MOD_POLL,    "Poll"},
    {MOD_SELECT,  "Select"},
    {MOD_SOCKET,  "Socket"},
    {MOD_NETLINK, "Netlink"},
    {MOD_ETH,     "Eth"},
    {MOD_IP,      "Ip"},
    {MOD_TCP,     "Tcp"},
    {MOD_UDP,     "Udp"},
    {MOD_MAX,     "Max"},
};

static const DP_MibField_t g_tcpList[] = {
    {DP_TCP_ACCEPTS,                   "Accepts"},
    {DP_TCP_CLOSED,                    "Closed"},
    {DP_TCP_CONN_ATTEMPT,              "ConnAttempt"},
    {DP_TCP_CONN_DROPS,                "ConnDrops"},
    {DP_TCP_CONNECTS,                  "Connects"},
    {DP_TCP_DELAYED_ACK,               "DelayedAck"},
    {DP_TCP_DROPS,                     "Drops"},
    {DP_TCP_KEEP_DROPS,                "KeepDrops"},
    {DP_TCP_KEEP_PROBE,                "KeepProbe"},
    {DP_TCP_KEEP_TIME_OUT,             "KeepTMO"},
    {DP_TCP_PERSIST_DROPS,             "PersistDrops"},
    {DP_TCP_PERSIST_TIMEOUT,           "PersistTMO"},
    {DP_TCP_RCV_ACK_BYTE,              "RcvAckBytes"},
    {DP_TCP_RCV_ACK_PACKET,            "RcvAckPkts"},
    {DP_TCP_RCV_ACK_TOO_MUCH,          "RcvAckTooMuch"},
    {DP_TCP_RCV_DUP_ACK,               "RcvDupAck"},
    {DP_TCP_RCV_BAD_OFF,               "RcvBadOff"},
    {DP_TCP_RCV_BAD_SUM,               "RcvBadSum"},
    {DP_TCP_RCV_BYTE,                  "RcvBytes"},
    {DP_TCP_RCV_DUP_BYTE,              "RcvDupBytes"},
    {DP_TCP_RCV_DUP_PACKET,            "RcvDupPkts"},
    {DP_TCP_RCV_PACKET_AFTER_WND,      "RcvAfterWndPkts"},
    {DP_TCP_RCV_BYTE_AFTER_WND,        "RcvAfterWndBytes"},
    {DP_TCP_RCV_PART_DUP_BYTE,         "RcvPartDupBytes"},
    {DP_TCP_RCV_PART_DUP_PACKET,       "RcvPartDupPkts"},
    {DP_TCP_RCV_OUT_ORDER_PACKET,      "RcvOutOrderPkts"},
    {DP_TCP_RCV_OUT_ORDER_BYTE,        "RcvOutOrderBytes"},
    {DP_TCP_RCV_SHORT,                 "RcvShort"},
    {DP_TCP_RCV_TOTAL,                 "RcvTotal"},
    {DP_TCP_RCV_PACKET,                "RcvPkts"},
    {DP_TCP_RCV_WND_PROBE,             "RcvWndProbe"},
    {DP_TCP_RCV_WND_UPDATE,            "RcvWndUpdate"},
    {DP_TCP_RCV_RST,                   "RcvRST"},
    {DP_TCP_RCV_INVALID_RST,           "RcvInvalidRST"},
    {DP_TCP_RCV_SYN_ESTABLISHED,       "RcvSynEstab"},
    {DP_TCP_RCV_FIN,                   "RcvFIN"},
    {DP_TCP_RCV_RXMT_FIN,              "RcvRxmtFIN"},
    {DP_TCP_REXMT_TIMEOUT,             "RexmtTMO"},
    {DP_TCP_RTT_UPDATED,               "RTTUpdated"},
    {DP_TCP_SEGS_TIMED,                "SegsTimed"},
    {DP_TCP_SND_BYTE,                  "SndBytes"},
    {DP_TCP_SND_CONTROL,               "SndCtl"},
    {DP_TCP_SND_PACKET,                "SndPkts"},
    {DP_TCP_SND_PROBE,                 "SndProbe"},
    {DP_TCP_SND_REXMT_BYTE,            "SndRexmtBytes"},
    {DP_TCP_SND_ACKS,                  "SndAcks"},
    {DP_TCP_SND_REXMT_PACKET,          "SndRexmtPkts"},
    {DP_TCP_SND_TOTAL,                 "SndTotal"},
    {DP_TCP_SND_WND_UPDATE,            "SndWndUpdate"},
    {DP_TCP_TIMEOUT_DROP,              "TMODrop"},
    {DP_TCP_RCV_EXD_WND_RST,           "RcvExdWndRst"},
    {DP_TCP_DROP_CONTROL_PKTS,         "DropCtlPkts"},
    {DP_TCP_DROP_DATA_PKTS,            "DropDataPkts"},
    {DP_TCP_SND_RST,                   "SndRST"},
    {DP_TCP_SND_FIN,                   "SndFIN"},
    {DP_TCP_FIN_WAIT_2_DROPS,          "FinWait2Drops"},
    {DP_TCP_RESPONSE_CHALLENGE_ACKS,   "RespChallAcks"},
    {DP_TCP_STAT_MAX,                  ""},
};

static const DP_MibField_t g_pktList[] = {
    {DP_PKT_LINK_IN,                     "LinkInPkts"},
    {DP_PKT_ETH_IN,                      "EthInPkts"},
    {DP_PKT_NET_IN,                      "NetInPkts"},
    {DP_PKT_ICMP_OUT,                    "IcmpOutPkts"},
    {DP_PKT_ARP_DELIVER,                 "ArpDeliverPkts"},
    {DP_PKT_IP_BCAST_DELIVER,            "IpBroadcastDeliverPkts"},
    {DP_PKT_NON_FRAG_DELIVER,            "NonFragDelverPkts"},
    {DP_PKT_UP_TO_CTRL_PLANE,            "UptoCtrlPlanePkts"},
    {DP_PKT_REASS_IN_FRAG,               "ReassInFragPkts"},
    {DP_PKT_REASS_OUT_REASS_PKT,         "ReassOutReassPkts"},
    {DP_PKT_NET_OUT,                     "NetOutPkts"},
    {DP_PKT_ETH_OUT,                     "EthOutPkts"},
    {DP_PKT_FRAG_IN,                     "FragInPkts"},
    {DP_PKT_FRAG_OUT,                    "FragOutPkts"},
    {DP_PKT_ARP_MISS_RESV,               "ArpMissResvPkts"},
    {DP_PKT_ARP_SEARCH_IN,               "ArpSearchInPkts"},
    {DP_PKT_ARP_HAVE_NORMAL_ARP,         "ArpHaveNormalPkts"},
    {DP_PKT_ICMP_ERR_IN,                 "RcvErrIcmpPkts"},
    {DP_PKT_NET_BAD_VER,                 "NetBadVersionPkts"},
    {DP_PKT_NET_BAD_HEAD_LEN,            "NetBadHdrLenPkts"},
    {DP_PKT_NET_BAD_LEN,                 "NetBadLenPkts"},
    {DP_PKT_NET_TOO_SHORT,               "NetTooShortPkts"},
    {DP_PKT_NET_BAD_SUM,                 "NetBadChecksumPkts"},
    {DP_PKT_NET_NO_PROTO,                "NetNoProtoPkts"},
    {DP_PKT_NET_NO_ROUTE,                "NetNoRoutePkts"},
    {DP_PKT_TCP_REASS_PKT,               "TcpReassPkts"},
    {DP_PKT_UDP_IN,                      "UdpInPkts"},
    {DP_PKT_UDP_OUT,                     "UdpOutPkts"},
    {DP_PKT_TCP_IN,                      "TcpInPkts"},
    {DP_PKT_SEND_BUF_IN,                 "SndBufInPkts"},
    {DP_PKT_SEND_BUF_OUT,                "SndBufOutPkts"},
    {DP_PKT_SEND_BUF_FREE,               "SndBufFreePkts"},
    {DP_PKT_RECV_BUF_IN,                 "RcvBufInPkts"},
    {DP_PKT_RECV_BUF_OUT,                "RcvBufOutPkts"},
    {DP_PKT_RECV_BUF_FREE,               "RcvBufFreePkts"},
    {DP_PKT_STAT_MAX,                    ""},
};

static const DP_MibField_t g_tcpConnList[] = {
    {DP_TCP_LISTEN,                "Listen"},
    {DP_TCP_SYN_SENT,              "SynSent"},
    {DP_TCP_SYN_RCVD,              "SynRcvd"},
    {DP_TCP_PASSIVE_ESTABLISHED,   "PAEstablished"},
    {DP_TCP_ACTIVE_ESTABLISHED,    "ACEstablished"},
    {DP_TCP_PASSIVE_CLOSE_WAIT,    "PACloseWait"},
    {DP_TCP_ACTIVE_CLOSE_WAIT,     "ACCloseWait"},
    {DP_TCP_PASSIVE_FIN_WAIT_1,    "PAFinWait1"},
    {DP_TCP_ACTIVE_FIN_WAIT_1,     "ACFinWait1"},
    {DP_TCP_PASSIVE_CLOSING,       "PAClosing"},
    {DP_TCP_ACTIVE_CLOSING,        "ACClosing"},
    {DP_TCP_PASSIVE_LAST_ACK,      "PALastAck"},
    {DP_TCP_ACTIVE_LAST_ACK,       "ACLastAck"},
    {DP_TCP_PASSIVE_FIN_WAIT_2,    "PAFinWait2"},
    {DP_TCP_ACTIVE_FIN_WAIT_2,     "ACFinWait2"},
    {DP_TCP_PASSIVE_TIME_WAIT,     "PATimeWait"},
    {DP_TCP_ACTIVE_TIME_WAIT,      "ACTimeWait"},
    {DP_TCP_ABORT,                 "Abort"},
    {DP_TCP_CONN_STAT_MAX,         ""},
};

static const DP_MibField_t g_abnList[] = {
    {DP_ABN_BASE,                   "AbnBase"},
    {DP_CONN_BY_LISTEN_SK,          "ConnByListenSk"},
    {DP_CONNED_SK_REPEAT,           "RepeatConn"},
    {DP_CONN_REFUSED,               "RefusedConn"},
    {DP_CONN_IN_PROGRESS,           "ConnInProg"},
    {DP_ACCEPT_NO_CHILD,            "AcceptNoChild"},
    {DP_SETOPT_PARAM_INVAL,         "SetOptInval"},
    {DP_SETOPT_KPID_INVAL,          "KpIdInval"},
    {DP_SETOPT_KPIN_INVAL,          "KpInInval"},
    {DP_SETOPT_KPCN_INVAL,          "KpCnInval"},
    {DP_SETOPT_MAXSEG_INVAL,        "MaxsegInval"},
    {DP_SETOPT_MAXSEG_STAT,         "MaxsegDisStat"},
    {DP_SETOPT_DFAC_STAT,           "DeferAcDisStat"},
    {DP_SETOPT_NO_SUPPORT,          "SetOptNotSup"},
    {DP_GETOPT_INFO_INVAL,          "TcpInfoInval"},
    {DP_GETOPT_PARAM_INVAL,         "GetOptInval"},
    {DP_GETOPT_NO_SUPPORT,          "GetOptNotSup"},
    {DP_TCP_SND_CONN_REFUSED,       "SndConnRefused"},
    {DP_TCP_SND_CANT_SEND,          "SndCantSend"},
    {DP_TCP_SND_CONN_CLOSED,        "SndConnClosed"},
    {DP_TCP_SND_NO_SPACE,           "SndNoSpace"},
    {DP_TCP_SND_BUF_NOMEM,          "SndbufNoMem"},
    {DP_TCP_RCV_CONN_REFUSED,       "RcvConnRefused"},
    {DP_TCP_RCV_CONN_CLOSED,        "RcvConnClosed"},
    {DP_ABN_STAT_MAX,               ""},
};

uint32_t UTILS_StatInit(void)
{
    size_t mibSize = 0;
    int worker = CFG_GET_VAL(DP_CFG_WORKER_MAX);
    mibSize = (sizeof(DP_TcpMibStat_t) + sizeof(DP_PktMibStat_t) + sizeof(DP_TcpConnMibStat_t)) * worker +
              sizeof(DP_AbnMibStat_t);

    if (g_statMibs.abnStat != NULL) {
        DP_LOG_ERR("UTILS_StatInit failed, statMibs allocated already.");
        return 1;
    }

    g_statMibs.abnStat = (DP_AbnMibStat_t *)MEM_MALLOC(mibSize, MOD_DBG, DP_MEM_FIX);
    if (g_statMibs.abnStat == NULL) {
        DP_LOG_ERR("Malloc memory failed for statMibs init.");
        return 1;
    }
    (void)memset_s(g_statMibs.abnStat, mibSize, 0, mibSize);

    g_statMibs.tcpStat = (DP_TcpMibStat_t *)((uint8_t *)g_statMibs.abnStat + sizeof(DP_AbnMibStat_t));
    g_statMibs.pktStat = (DP_PktMibStat_t *)((uint8_t *)g_statMibs.tcpStat + (worker * sizeof(DP_TcpMibStat_t)));
    g_statMibs.tcpConnStat =
        (DP_TcpConnMibStat_t *)((uint8_t *)g_statMibs.pktStat + (worker * sizeof(DP_PktMibStat_t)));

    return 0;
}

uint32_t UTILE_IsStatInited(void)
{
    if ((g_statMibs.tcpStat != NULL) && (g_statMibs.pktStat != NULL) &&
        (g_statMibs.abnStat != NULL) && (g_statMibs.tcpConnStat != NULL)) {
        return 1;
    }
    return 0;
}

void UTILS_StatDeinit(void)
{
    if (g_statMibs.abnStat == NULL) {
        return;
    }

    MEM_FREE(g_statMibs.abnStat, DP_MEM_FIX);
    g_statMibs.abnStat = NULL;
    g_statMibs.tcpStat = NULL;
    g_statMibs.pktStat = NULL;
    g_statMibs.tcpConnStat = NULL;
}

uint32_t GetFieldName(uint32_t fieldId, DP_StatType_t type, DP_MibStatistic_t *showStat)
{
    char *name = "";
    int ret = 0;
    switch (type) {
        case DP_STAT_TCP:
            name = g_tcpList[fieldId].fieldName;
            break;
        case DP_STAT_TCP_CONN:
            name = g_tcpConnList[fieldId].fieldName;
            break;
        case DP_STAT_PKT:
            name = g_pktList[fieldId].fieldName;
            break;
        case DP_STAT_ABN:
            name = g_abnList[fieldId].fieldName;
            break;
        default:
            break;
    }

    ret = memcpy_s(showStat[fieldId].fieldName, DP_MIB_FIELD_NAME_LEN_MAX, name, strlen(name));
    if (ret != 0) {
        DP_LOG_ERR("Memcpy failed when get field name.");
        return 1;
    }

    return 0;
}

uint64_t GetFieldValue(int workerId, uint32_t fieldId, DP_StatType_t type)
{
    uint64_t value = 0;

    switch (type) {
        case DP_STAT_TCP:
            value = DP_GET_TCP_STAT(workerId, fieldId);
            break;
        case DP_STAT_TCP_CONN:
            value = DP_GET_TCP_CONN_STAT(workerId, fieldId);
            break;
        case DP_STAT_PKT:
            value = DP_GET_PKT_STAT(workerId, fieldId);
            break;
        case DP_STAT_ABN:
            value = DP_GET_ABN_STAT(fieldId);
            break;
        default:
            break;
    }

    return value;
}

uint64_t GetSendBufPktNum(int workerId)
{
    uint64_t pktNum;
    pktNum = (DP_GET_PKT_STAT(workerId, DP_PKT_SEND_BUF_IN) -
              DP_GET_PKT_STAT(workerId, DP_PKT_SEND_BUF_OUT) -
              DP_GET_PKT_STAT(workerId, DP_PKT_SEND_BUF_FREE));
    return pktNum;
}

uint64_t GetRecvBufPktNum(int workerId)
{
    uint64_t pktNum;
    pktNum = (DP_GET_PKT_STAT(workerId, DP_PKT_RECV_BUF_IN) -
              DP_GET_PKT_STAT(workerId, DP_PKT_RECV_BUF_OUT) -
              DP_GET_PKT_STAT(workerId, DP_PKT_RECV_BUF_FREE));
    return pktNum;
}

uint32_t GetMemModName(uint32_t modId, uint8_t *modName)
{
    int ret;
    if (memset_s(modName, MOD_LEN, 0, MOD_LEN) != 0) {
        DP_LOG_ERR("Memcpy failed when clear mod name.");
        return 1;
    }
    ret = memcpy_s(modName, MOD_LEN, g_modName[modId].fieldName, strlen(g_modName[modId].fieldName));
    if (ret != 0) {
        DP_LOG_ERR("Memcpy failed when get mod name.");
        return 1;
    }

    return 0;
}