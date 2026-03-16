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
    {MOD_PACKET,  "Packet"},
    {MOD_ETH,     "Eth"},
    {MOD_RAW,     "Ipraw"},
    {MOD_IP,      "Ip"},
    {MOD_IP6,     "Ip6"},
    {MOD_TCP,     "Tcp"},
    {MOD_UDP,     "Udp"},
    {MOD_MAX,     "Max"},
};

static const DP_MibField_t g_tcpList[] = {
    {DP_TCP_ACCEPTS,                   "Accepts"},
    {DP_TCP_CLOSED,                    "Closed"},
    {DP_TCP_CONN_ATTEMPT,              "ConnAttempt"},
    {DP_TCP_CONNECTS,                  "Connects"},
    {DP_TCP_DELAYED_ACK,               "DelayedAck"},
    {DP_TCP_DROPS,                     "Drops"},
    {DP_TCP_KEEP_DROPS,                "RstKeepDrops"},
    {DP_TCP_KEEP_PROBE,                "KeepProbe"},
    {DP_TCP_KEEP_TIME_OUT,             "KeepTMO"},
    {DP_TCP_PERSIST_DROPS,             "RstPersistDrops"},
    {DP_TCP_PERSIST_TIMEOUT,           "PersistTMO"},
    {DP_TCP_RCV_ACK_BYTE,              "RcvAckBytes"},
    {DP_TCP_RCV_ACK_PACKET,            "RcvAckPkts"},
    {DP_TCP_RCV_ACK_TOO_MUCH,          "RcvAckTooMuch"},
    {DP_TCP_RCV_DUP_ACK,               "RcvDupAck"},
    {DP_TCP_RCV_BAD_OFF,               "RcvBadOff"},
    {DP_TCP_RCV_BAD_SUM,               "RcvBadSum"},
    {DP_TCP_RCV_LOCAL_ADDR,            "RcvLocalAddr"},
    {DP_TCP_RCV_WITHOUT_LISTENER,      "RcvNoTcpHash"},
    {DP_TCP_RCV_ERR_ACKFLAG,           "RstByRcvErrAckFlags"},
    {DP_TCP_LISTEN_RCV_NOTSYN,         "RcvNotSynInListen"},
    {DP_TCP_LISTEN_RCV_INVALID_WID,    "RcvInvalidWidInPassive"},
    {DP_TCP_RCV_DUP_SYN,               "RcvDupSyn"},
    {DP_TCP_SYNRCV_NONACK,             "RcvNonAckInSynRcv"},
    {DP_TCP_RCV_INVALID_ACK,           "RstByRcvInvalidAck"},
    {DP_TCP_RCV_ERR_SEQ,               "RcvErrSeq"},
    {DP_TCP_RCV_ERR_OPT,               "RcvErrOpt"},
    {DP_TCP_RCV_DATA_SYN,              "RcvDataSyn"},
    {DP_TCP_RCV_BYTE,                  "RcvBytes"},
    {DP_TCP_PASSIVE_RCV_BYTE,          "RcvBytesPassive"},
    {DP_TCP_ACTIVE_RCV_BYTE,           "RcvBytesActive"},
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
    {DP_TCP_RCV_INVALID_RST,           "RcvInvalidRST"},
    {DP_TCP_RCV_SYN_ESTABLISHED,       "RcvSynEstab"},
    {DP_TCP_RCV_FIN,                   "RcvFIN"},
    {DP_TCP_RCV_RXMT_FIN,              "RcvRxmtFIN"},
    {DP_TCP_REXMT_TIMEOUT,             "RexmtTMO"},
    {DP_TCP_RTT_UPDATED,               "RTTUpdated"},
    {DP_TCP_SND_BYTE,                  "SndBytes"},
    {DP_TCP_PASSIVE_SND_BYTE,          "SndBytesPassive"},
    {DP_TCP_ACTIVE_SND_BYTE,           "SndBytesActive"},
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
    {DP_TCP_ONCE_DRIVE_PASSIVE_TSQ,    "OnceDrivePassiveTsqCnt"},
    {DP_TCP_AGE_DROPS,                 "AgeDrops"},
    {DP_TCP_BBR_SAMPLE_CNT,            "BbrSampleCnt"},
    {DP_TCP_BBR_SLOWBW_CNT,            "BbrSlowBWCnt"},
    {DP_TCP_FRTO_SPURIOS_CNT,          "FrtoSpurios"},
    {DP_TCP_FRTO_REAL_CNT,             "FrtoReal"},
    {DP_TCP_TIMEWAIT_REUSE,            "TimewaitReuse"},
    {DP_TCP_SYNSNT_CONN_DROPS,         "SynSentConnDrops"},
    {DP_TCP_SYNRCV_CONN_DROPS,         "SynRecvConnDrops"},
    {DP_TCP_USER_TIMEOUT_DROPS,        "RstUserTimeOutDrops"},
    {DP_TCP_SYN_RETRIES_DROPS,         "RstSynRetriesDrops"},
    {DP_TCP_SYNACK_RETRIES_DROPS,      "RstSynAckRetriesDrops"},
    {DP_TCP_RCV_OLD_ACK,               "RcvOldAck"},
    {DP_TCP_RCV_ERR_SYN_OPT,           "RcvErrSynOpt"},
    {DP_TCP_RCV_ERR_SYNACK_OPT,        "RcvErrSynAckOpt"},
    {DP_TCP_RCV_ERR_ESTABLISH_ACK_OPT, "RcvErrEstabAckOpt"},
    {DP_TCP_SND_REXMIT_SYN,            "RexmitSyn"},
    {DP_TCP_SND_REXMIT_SYNACK,         "RexmitSynAck"},
    {DP_TCP_SND_REXMIT_FIN,            "RexmitFin"},
    {DP_TCP_TIME_WAIT_DROPS,           "RstTimeWaitTimerDrops"},
    {DP_TCP_XMIT_GET_DEV_NULL,         "XmitGetNullDev"},
    {DP_TCP_USER_ACCEPT,               "TcpUserAccept"},
    {DP_TCP_RCV_ERR_SACKOPT,           "TcpRcvErrSackopt"},
    {DP_TCP_RCV_DATA_AFTER_FIN,        "TcpRcvDataAfterFin"},
    {DP_TCP_RCV_AFTER_CLOSED,          "TcpRcvAfterClosed"},
    {DP_TCP_CHECK_DEFFER_ACCEPT_ERR,   "TcpCheckDefferAcceptDrops"},
    {DP_TCP_OVER_BACKLOG,              "TcpOverBackLogDrops"},
    {DP_TCP_PAEST_OVER_MAXCB,          "RstPassiveEstOverMaxCb"},
    {DP_TCP_SND_SYN,                   "TcpSndSyn"},
    {DP_TCP_CORK_LIMIT,                "TcpSndLimitedByCork"},
    {DP_TCP_MSGMORE_LIMIT,             "TcpSndLimitedByMsgMore"},
    {DP_TCP_NAGLE_LIMIT,               "TcpSndLimitedByNagle"},
    {DP_TCP_BBR_PACING_LIMIT,          "TcpSndLimitedByBbr"},
    {DP_TCP_PACING_LIMIT,              "TcpSndLimitedByPacing"},
    {DP_TCP_CWND_LIMIT,                "TcpSndLimitedByCwnd"},
    {DP_TCP_SWND_LIMIT,                "TcpSndLimitedBySwnd"},
    {DP_TCP_DROP_REASS_BYTE,           "TcpDropReassByte"},
    {DP_TCP_SND_SACK_REXMT_PACKET,     "TcpRexmitSackPkt"},
    {DP_TCP_SND_FAST_REXMT_PACKET,     "TcpFastRexmitPkt"},
    {DP_TCP_PERSIST_USER_TIMEOUT_DROPS, "RstPersistUserDrops"},
    {DP_TCP_SYN_SENT_RCV_ERR_ACK,      "RstSynSentRcvErrAck"},
    {DP_TCP_COOKIE_AFTER_CLOSED,       "RstCookieAfterClosed"},
    {DP_TCP_PARENT_CLOSED,             "RstParentClosed"},
    {DP_TCP_RCV_NON_RST,               "RstRcvNonRstPkt"},
    {DP_TCP_CHILD_CLOSE,               "RstCloseChild"},
    {DP_TCP_LINGER_CLOSE,              "RstLingerClose"},
    {DP_TCP_RCVDATA_AFTER_CLOSE,       "RstRcvDataAfterClose"},
    {DP_TCP_REXMIT_RST,                "RstRexmit"},
    {DP_TCP_RCVBUF_NOT_CLEAN,          "RstRcvBufNotClean"},
    {DP_TCP_SYNSENT_RCV_INVALID_RST,   "SynSentRcvInvalidRst"},
    {DP_TCP_CONN_KEEP_DROPS,           "TcpConnKeepDrops"},
    {DP_TCP_SYN_SENT_RCV_NO_SYN,       "TcpRcvPktNoSyn"},
    {DP_TCP_REASS_SUCC_BYTE,           "TcpReassSucBytes"},
    {DP_TCP_RCV_OUT_BYTE,              "TcpRcvOutBytes"},
    {DP_TCP_ICMP_TOOBIG_SHORT,         "TcpIcmpTooBigShort"},
    {DP_TCP_ICMP_TOOBIG_NO_TCP,        "TcpIcmpTooBigNoTcp"},
    {DP_TCP_ICMP_TOOBIG_ERR_SEQ,       "TcpIcmpTooBigErrSeq"},
    {DP_TCP_ICMP_TOOBIG_LARGE_MSS,     "TcpIcmpTooBigLargeMss"},
    {DP_TCP_ICMP_TOOBIG_ERR_STATE,     "TcpIcmpTooBigErrState"},
    {DP_TCP_CLOSE_NORCV_DATALEN,       "TcpCloseNoRcvDataLen"},
    {DP_TCP_PKT_WITHOUT_ACK,           "TcpPktWithoutAck"},
    {DP_TCP_SYN_RECV_DUP_PACKET,       "TcpSynRecvDupPacket"},
    {DP_TCP_SYN_RECV_UNEXPECT_SYN,     "TcpSynRecvUnexpectSyn"},
    {DP_TCP_RCV_ACK_ERR_COOKIE,        "TcpRcvAckErrCookie"},
    {DP_TCP_COOKIE_CREATE_FAILED,      "TcpRcvCookieCreateFailed"},
    {DP_TCP_RCV_RST_IN_RFC1337,        "TcpRcvRstInRfc1337"},
    {DP_TCP_DEFER_ACCEPT_DROP,         "TcpDeferAcceptDrop"},
    {DP_TCP_STAT_MAX,                  ""},
};

static const DP_MibField_t g_pktList[] = {
    {DP_PKT_LINK_IN,                     "LinkInPkts"},
    {DP_PKT_ETH_IN,                      "EthInPkts"},
    {DP_PKT_NET_IN,                      "NetInPkts"},
    {DP_PKT_ICMP_FORWARD,                "IcmpOutPkts"},
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
    {DP_PKT_ICMP_IN,                     "RcvIcmpPkts"},
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
    {DP_PKT_IP6_IN,                      "Ip6InPkts"},
    {DP_PKT_IP6_TOO_SHORT,               "Ip6TooShortPkts"},
    {DP_PKT_IP6_BAD_VER,                 "Ip6BadVerPkts"},
    {DP_PKT_IP6_BAD_HEAD_LEN,            "Ip6BadHeadLenPkts"},
    {DP_PKT_IP6_BAD_LEN,                 "Ip6BadLenPkts"},
    {DP_PKT_IP6_MUTICAST_DELIVER,        "Ip6MutiCastDeliverPkts"},
    {DP_PKT_IP6_EXT_HDR_CNT_ERR,         "Ip6ExtHdrCntErrPkts"},
    {DP_PKT_IP6_EXT_HDR_OVERFLOW,        "Ip6ExtHdrOverflowPkts"},
    {DP_PKT_IP6_EXT_HDR_HBH_ERR,         "Ip6HbhHdrErrPkts"},
    {DP_PKT_IP6_NO_UPPER_PROTO,          "Ip6NoUpperProtoPkts"},
    {DP_PKT_IP6_REASS_IN_FRAG,           "Ip6ReassInFragPkts"},
    {DP_PKT_IP6_EXT_HDR_FRAG_ERR,        "Ip6FragHdrErrPkts"},
    {DP_PKT_IP6_OUT,                     "Ip6OutPkts"},
    {DP_PKT_IP6_FRAG_OUT,                "Ip6FragOutPkts"},
    {DP_PKT_KERNEL_FDIR_CACHE_MISS,      "KernelFdirCacheMiss"},
    {DP_PKT_NET_DEV_TYPE_NOT_MATCH,      "IpDevTypeNoMatch"},
    {DP_PKT_NET_CHECK_ADDR_FAIL,         "IpCheckAddrFail"},
    {DP_PKT_IP_LEN_OVER_LIMIT,           "IpLenOverLimit"},
    {DP_PKT_NET_REASS_OVER_TBL_LIMIT,    "IpReassOverTblLimit"},
    {DP_PKT_NET_REASS_MALLOC_FAIL,       "IpReassMallocFail"},
    {DP_PKT_NET_REASS_NODE_OVER_LIMIT,   "IpReassNodeOverLimit"},
    {DP_PKT_ICMP_ADDR_NOT_MATCH,         "IpIcmpAddrNotMatch"},
    {DP_PKT_ICMP_PKT_LEN_SHORT,          "IpIcmpPktLenShort"},
    {DP_PKT_ICMP_PKT_BAD_SUM,            "IpIcmpPktBadSum"},
    {DP_PKT_ICMP_NOT_PORT_UNREACH,       "IpIcmpNotPortUnreach"},
    {DP_PKT_ICMP_UNREACH_TOO_SHORT,      "IpIcmpUnreachTooShort"},
    {DP_PKT_ICMP_UNREACH_TYPE_ERR,       "IpIcmpUnreachTypeErr"},
    {DP_PKT_IP6_DEV_TYPE_NOT_MATCH,      "Ip6DevTypeErr"},
    {DP_PKT_IP6_CHECK_ADDR_FAIL,         "Ip6CheckAddrFail"},
    {DP_PKT_IP6_REASS_OVER_TBL_LIMIT,    "Ip6ReassOverTblLimit"},
    {DP_PKT_IP6_REASS_MALLOC_FAIL,       "Ip6ReassMallocFail"},
    {DP_PKT_IP6_REASS_NODE_OVER_LIMIT,   "Ip6ReassNodeOverLimit"},
    {DP_PKT_IP6_PROTO_ERR,               "Ip6ProtoErr"},
    {DP_PKT_IP6_ICMP_TOO_SHORT,          "Ip6IcmpTooShort"},
    {DP_PKT_IP6_ICMP_BAD_SUM,            "Ip6IcmpBadSum"},
    {DP_PKT_IP6_ICMP_NO_PAYLOAD,         "Ip6IcmpNoPayload"},
    {DP_PKT_IP6_ICMP_CODE_NOMATCH,       "Ip6CodeNomatch"},
    {DP_PKT_ICMP6_TOOBIG_SHORT,          "Icmpv6TooBigShort"},
    {DP_PKT_ICMP6_TOOBIG_SMALL,          "Icmpv6TooBigSmall"},
    {DP_PKT_ICMP6_TOOBIG_EXTHDR_ERR,     "Icmpv6TooBigExthdrErr"},
    {DP_PKT_ICMP6_TOOBIG_NOT_TCP,        "Icmpv6TooBigNotTcp"},
    {DP_PKT_IP_BAD_OFFSET,               "IpBadOffset"},
    {DP_PKT_NF_PREROUTING_DROP,          "NfPreRoutingDrop"},
    {DP_PKT_NF_LOCALIN_DROP,             "NfLocaInDrop"},
    {DP_PKT_NF_FORWARD_DROP,             "NfForwardDrop"},
    {DP_PKT_NF_LOCALOUT_DROP,            "NfLocalOutDrop"},
    {DP_PKT_NF_POSTROUTING_DROP,         "NfPostRoutingDrop"},
    {DP_UDP_ICMP_UNREACH_SHORT,          "UdpIcmpUnReachShort"},
    {DP_PKT_ICMP6_UNREACH_SHORT,         "Ip6IcmpUnReachTooShort"},
    {DP_PKT_ICMP6_UNREACH_EXTHDR_ERR,    "Icmp6UnReachExthdrErr"},
    {DP_PKT_ICMP6_UNREACH_NOT_UDP,       "Icmp6UnReachNotUdp"},
    {DP_UDP_ICMP6_UNREACH_SHORT,         "UdpIcmp6UnReachShort"},
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
    {DP_TIMER_NODE_EXIST,           "TimerNodeExist"},
    {DP_TIMER_EXPIRED_INVAL,        "TimerExpiredInval"},
    {DP_TIMER_ACTIVE_EXCEPT,        "TimerActiveExcept"},
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
    {DP_SETOPT_CA_INVAL,            "CongetsionAlgInval"},
    {DP_SETOPT_BBR_CWND_INVAL,      "BBRproberttCwndInval"},
    {DP_SETOPT_BBR_TIMEOUT_INVAL,   "BBRproberttTimeoutInval"},
    {DP_SETOPT_BBR_CYCLE_INVAL,     "BBRproberttCycleInval"},
    {DP_SETOPT_BBR_INCRFACTOR_INVAL, "BBRincrFactorInval"},
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
    {DP_WORKER_MISS_MATCH,          "WorkerMissMatch"},
    {DP_PORT_INTERVAL_PUT_ERR,      "PortIntervalPutErr"},
    {DP_PORT_INTERVAL_CNT_ERR,      "PortIntervalCntErr"},
    {DP_TIMER_CYCLE,                "TimerCycle"},
    {DP_PBUF_HOOK_ALLOC_ERR,        "PbufHookAllocErr"},
    {DP_PBUF_BUILD_PARAM_ERR,       "PbufBuildParamErr"},
    {DP_PBUF_COPY_PARAM_ERR,        "PbufCopyParamErr"},
    {DP_NOTIFY_RCVSYN_ERR,                  "NotifyRcvSynErr"},
    {DP_NOTIFY_PASSIVE_CONNECTED_ERR,       "NotifyPassiveConnErr"},
    {DP_NOTIFY_PASSIVE_CONNECTED_FAIL_ERR,  "NotifyPassiveConnFailErr"},
    {DP_NOTIFY_ACTIVE_CONNECT_FAIL_ERR,     "NotifyActiveConnErr"},
    {DP_NOTIFY_RCVFIN_ERR,                  "NotifyRcvFinErr"},
    {DP_NOTIFY_RCVRST_ERR,                  "NotifyRcvRstErr"},
    {DP_NOTIFY_DISCONNECTED_ERR,            "NotifyDisconnectedErr"},
    {DP_NOTIFY_WRITE_ERR,                   "NotifyWriteErr"},
    {DP_NOTIFY_READ_ERR,                    "NotifyReadErr"},
    {DP_NOTIFY_FREE_SOCKCB_ERR,             "NotifyFreeSockErr"},
    {DP_SOCKET_FD_ERR,              "SocketFdErr"},
    {DP_FD_MEM_ERR,                 "FdMemErr"},
    {DP_FD_NODE_FULL,               "FdNodeFull"},
    {DP_SOCKET_CREATE_ERR,          "SocketCreateErr"},
    {DP_SOCKET_DOMAIN_ERR,          "SocketDomainErr"},
    {DP_SOCKET_NO_CREATEFN,         "SocketNoCreateFn"},
    {DP_SOCKET_TYPE_WITH_FLAGS,     "SocketTypeWithFlags"},
    {DP_SOCKET_TYPE_ERR,            "SocketTypeErr"},
    {DP_SOCKET_PROTO_INVAL,         "SocketProtoInval"},
    {DP_SOCKET_NOSUPP,              "SocketNoSupp"},
    {DP_TCP_CREATE_INVAL,           "TcpCreateInval"},
    {DP_TCP_CREATE_FULL,            "TcpCreateFull"},
    {DP_TCP_CREATE_MEM_ERR,         "TcpCreateMemErr"},
    {DP_UDP_CREATE_INVAL,           "UdpCreateInval"},
    {DP_UDP_CREATE_FULL,            "UdpCreateFull"},
    {DP_UDP_CREATE_MEM_ERR,         "UdpCreateMemErr"},
    {DP_EPOLL_CREATE_FULL,          "EpollCreateFull"},
    {DP_BIND_GET_SOCK_ERR,          "BindGetSockErr"},
    {DP_SOCK_GET_FD_ERR,            "SocketGetFdErr"},
    {DP_FD_GET_INVAL,               "FdGetInval"},
    {DP_FD_GET_CLOSED,              "FdGetClosed"},
    {DP_FD_GET_INVAL_TYPE,          "FdGetInvalType"},
    {DP_FD_GET_REF_ERR,             "FdGetRefErr"},
    {DP_SOCK_GET_SK_NULL,           "SockGetSkNull"},
    {DP_BIND_FAILED,                "BindFailed"},
    {DP_TCP_BIND_REPEAT,            "TcpBindRepeat"},
    {DP_TCP_BIND_SHUTDOWN,          "TcpBindShutdown"},
    {DP_TCP_INET_BIND_FAILED,       "TcpInetBindFailed"},
    {DP_INET_BIND_ADDR_INVAL,       "InetBindAddrInval"},
    {DP_INET_ADDR_NULL,             "InetAddrNull"},
    {DP_INET6_ADDR_NULL,            "Inet6AddrNull"},
    {DP_INET_ADDRLEN_ERR,           "InetAddrlenErr"},
    {DP_INET6_ADDRLEN_ERR,          "Inet6AddrlenErr"},
    {DP_INET_ADDR_FAMILY_ERR,       "InetAddrFamilyErr"},
    {DP_INET6_ADDR_FAMILY_ERR,      "Inet6AddrFamilyErr"},
    {DP_INET_BIND_CONNECTED,        "InetBindConnected"},
    {DP_INET_BIND_ADDR_ERR,         "InetBindAddrErr"},
    {DP_TCP_BIND_RAND_PORT_FAILED,  "TcpBindRandPortFailed"},
    {DP_TCP_BIND_PORT_FAILED,       "TcpBindPortFailed"},
    {DP_CONN_GET_SOCK_ERR,          "ConnGetSockErr"},
    {DP_CONN_ADDR_NULL,             "ConnAddrNull"},
    {DP_CONN_ADDRLEN_ERR,           "ConnAddrlenErr"},
    {DP_CONN_ADDR6LEN_ERR,          "ConnAddr6lenErr"},
    {DP_CONN_FAILED,                "ConnFailed"},
    {DP_CONN_FLAGS_ERR,             "ConnFlagsErr"},
    {DP_TCP_INET_CONN_FAILED,       "TcpInetConnFailed"},
    {DP_UDP_INET_CONN_FAILED,       "UdpInetConnFailed"},
    {DP_INET_CONN_ADDR_INVAL,       "InetConnAddrInval"},
    {DP_UPDATE_FLOW_RT_FAILED,      "UpdateFlowRtFailed"},
    {DP_UPDATE_FLOW_RT6_FAILED,     "UpdateFlowRt6Failed"},
    {DP_UPDATE_FLOW_WRONG_ADDR,     "UpdateFlowWrongAddr"},
    {DP_UPDATE_FLOW_WRONG_ADDR6,    "UpdateFlowWrongAddr6"},
    {DP_INIT_FLOW_RT_FAILED,        "InetFlowRtFailed"},
    {DP_INIT6_FLOW_RT_FAILED,       "Inet6FlowRtFailed"},
    {DP_TCP_CONN_RT_NULL,           "TcpConnRtNull"},
    {DP_TCP_CONN_DEV_DOWN,          "TcpConnDevDown"},
    {DP_TCP_CONN_VI_ANY,            "TcpConnViAny"},
    {DP_TCP_CONN_ADDR_ERR,          "TcpConnAddrErr"},
    {DP_TCP_CONN_RAND_PORT_FAILED,  "TcpConnRandPortFailed"},
    {DP_TCP_CONN_PORT_FAILED,       "TcpConnPortFailed"},
    {DP_NETDEV_RXHASH_FAILED,       "NetdevRxhashFailed"},
    {DP_TCP_RXHASH_WID_ERR,         "TcpRxhashWidErr"},
    {DP_UDP_CONN_SELF,              "UdpConnSelf"},
    {DP_UDP_CONN_RAND_PORT_FAILED,  "UdpConnRandPortFailed"},
    {DP_UDP_CONN_PORT_FAILED,       "UdpConnPortFailed"},
    {DP_TIMEOUT_ABORT,              "TimeoutAbort"},
    {DP_SYN_STATE_RCV_RST,          "SynStateRcvRst"},
    {DP_CLOSE_WAIT_RCV_RST,         "CloseWaitRcvRst"},
    {DP_ABNORMAL_RCV_RST,           "AbnormalRcvRst"},
    {DP_ACCEPT_GET_SOCK_ERR,        "AcceptGetSockErr"},
    {DP_ACCEPT_FD_ERR,              "AcceptFdErr"},
    {DP_ACCEPT_CREATE_ERR,          "AcceptCreateErr"},
    {DP_ACCEPT_ADDRLEN_NULL,        "AcceptAddrlenNull"},
    {DP_ACCEPT_ADDRLEN_INVAL,       "AcceptAddrlenInval"},
    {DP_ACCEPT_NO_SUPPORT,          "AcceptNoSupport"},
    {DP_ACCEPT_CLOSED,              "AcceptClosed"},
    {DP_ACCEPT_NOT_LISTENED,        "AcceptNotListened"},
    {DP_ACCEPT_GET_ADDR_FAILED,     "AcceptGetAddrFailed"},
    {DP_GET_DST_ADDRLEN_INVAL,      "GetDstAddrlenInval"},
    {DP_SENDTO_GET_SOCK_ERR,        "SendtoGetSockErr"},
    {DP_SENDTO_BUF_NULL,            "SendtoBufNull"},
    {DP_SEND_FLAGS_INVAL,           "SendFlagsInval"},
    {DP_SEND_GET_DATALEN_FAILED,    "SendGetDataLenFailed"},
    {DP_SEND_ZERO_DATALEN,          "SendGetDataLenZero"},
    {DP_SOCK_CHECK_MSG_NULL,        "SockCheckMsgNull"},
    {DP_SOCK_CHECK_MSGIOV_NULL,     "SockCheckMsgiovNull"},
    {DP_SOCK_CHECK_MSGIOV_INVAL,     "SockCheckMsgiovInval"},
    {DP_GET_IOVLEN_INVAL,           "GetIovlenInval"},
    {DP_GET_IOV_BASE_NULL,          "GetIovBaseNull"},
    {DP_ZIOV_CB_NULL,               "ZiovCbNull"},
    {DP_GET_TOTAL_IOVLEN_INVAL,     "GetTotalIovlenInval"},
    {DP_SOCK_SENDMSG_FAILED,        "SockSendmsgFailed"},
    {DP_TCP_SND_DEV_DOWN,           "TcpSndDevDown"},
    {DP_TCP_SND_BUF_ZCOPY_NOMEM,    "TcpSndBufZcopyNomem"},
    {DP_TCP_PBUF_CONSTRUCT_FAILED,  "TcpPbufConstructFailed"},
    {DP_TCP_PUSH_SND_PBUF_FAILED,   "TcpPushSndPbufFailed"},
    {DP_UDP_SND_LONG,               "UdpSndLong"},
    {DP_UDP_CHECK_DST_ADDR_ERR,     "UdpCheckDstAddrErr"},
    {DP_UDP_CHECK_DST_ADDR6_ERR,    "UdpCheckDstAddr6Err"},
    {DP_UDP_SND_ADDR_INVAL,         "UdpSndAddrInval"},
    {DP_UDP_SND_NO_DST,             "UdpSndNoDst"},
    {DP_UDP_FLOW_BROADCAST,         "UdpFlowBroadcast"},
    {DP_UDP_SND_NO_RT,              "UdpSndNoRt"},
    {DP_UDP_SND_DEV_DOWN,           "UdpSndDevDown"},
    {DP_UDP_SND_FLAGS_NO_SUPPORT,   "UdpSndFlagsNoSupport"},
    {DP_UDP_AUTO_BIND_FAILED,       "UdpAutoBindFailed"},
    {DP_FROM_MSG_BUILD_PBUF_FAILED, "FromMsgBuildPbufFailed"},
    {DP_FROM_MSG_APPEND_PBUF_FAILED, "FromMsgAppendPbufFailed"},
    {DP_SENDMSG_GET_SOCK_ERR,       "SendmsgGetSockErr"},
    {DP_ZSENDMSG_GET_SOCK_ERR,      "ZSendmsgGetSockErr"},
    {DP_RCVFROM_GET_SOCK_ERR,       "RecvfromGetSockErr"},
    {DP_RCVFROM_FAILED,             "RecvfromFailed"},
    {DP_RCVFROM_BUF_NULL,           "RecvfromBufNull"},
    {DP_RECV_FLAGS_INVAL,           "RecvFlagsInval"},
    {DP_RECV_GET_DATALEN_FAILED,    "RecvGetDatalenFailed"},
    {DP_RECV_CHECK_MSG_FAILED,      "RecvCheckMsgFailed"},
    {DP_SOCK_RECVMSG_FAILED,        "SockRecvmsgFailed"},
    {DP_TCP_RCV_BUF_FAILED,         "TcpRcvBufFailed"},
    {DP_RCV_GET_ADDR_FAILED,        "RcvGetAddrFailed"},
    {DP_SOCK_READ_BUFCHAIN_ZRRO,    "SockReadBufchainZero"},
    {DP_SOCK_READ_BUFCHAIN_SHORT,   "SockReadBufchainShort"},
    {DP_RCV_ZCOPY_GET_ADDR_FAILED,  "RcvZcopyGetAddrFailed"},
    {DP_RCV_ZCOPY_CHAIN_READ_FAILED, "RcvZcopyChainReadFailed"},
    {DP_RCVMSG_FAILED,              "RcvmsgFailed"},
    {DP_ZRCVMSG_FAILED,             "ZRcvmsgFailed"},
    {DP_SEND_IP_HOOK_FAILED,        "SendIpHookFailed"},
    {DP_PBUF_REF_ERR,               "PbufRefErr"},
    {DP_INET_REASS_TIME_OUT,        "InetReassTimeOut"},
    {DP_INET6_REASS_TIME_OUT,       "Inet6ReassTimeOut"},
    {DP_WORKER_GET_ERR_WID,         "WorkerGetErrWid"},
    {DP_INIT_PBUF_MP_FAILED,        "InitPbufMpFailed"},
    {DP_INIT_PBUF_HOOK_REG_FAILED,  "InitPbufHookRegFailed"},
    {DP_INIT_ZCOPY_MP_FAILED,       "InitZcopyMpFailed"},
    {DP_INIT_ZCOPY_HOOK_REG_FAILED, "InitZcopyHookRegFailed"},
    {DP_INIT_REF_MP_FAILED,         "InitRefMpFailed"},
    {DP_INIT_REF_HOOK_REG_FAILED,   "InitRefHookRegFailed"},
    {DP_CPD_DELAY_ENQUE_ERR,        "CpdDelayEnqueErr"},
    {DP_CPD_DELAY_DEQUE_ERR,        "CpdDelayDequeErr"},
    {DP_CPD_SYNC_TABLE_RECV_ERR,    "CpdSyncTableRecvErr"},
    {DP_CPD_SYNC_TABLE_SEND_ERR,    "CpdSyncTableSendErr"},
    {DP_CPD_SEND_ICMP_ERR,          "CpdSendIcmpErr"},
    {DP_CPD_TRANS_MALLOC_ERR,       "CpdTransMallocErr"},
    {DP_CPD_FIND_TAP_FAILED,        "CpdFindTapFailed"},
    {DP_CPD_FD_WRITE_FAILED,        "CpdFdWriteFailed"},
    {DP_CPD_FD_WRITEV_FAILED,       "CpdFdWritevFailed"},
    {DP_CPD_FD_READ_FAILED,         "CpdFdReadFailed"},
    {DP_UTILS_TIMER_ERR,            "UtilsTimerErr"},
    {DP_PBUF_WID_ERR,               "PbufWidErr"},
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

uint32_t UTILS_IsStatInited(void)
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

    ret = memcpy_s(showStat->fieldName, DP_MIB_FIELD_NAME_LEN_MAX, name, strlen(name) + 1);
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
        DP_LOG_ERR("Memset failed when clear mod name.");
        return 1;
    }
    ret = memcpy_s(modName, MOD_LEN, g_modName[modId].fieldName, strlen(g_modName[modId].fieldName) + 1);
    if (ret != 0) {
        DP_LOG_ERR("Memcpy failed when get mod name.");
        return 1;
    }

    return 0;
}
