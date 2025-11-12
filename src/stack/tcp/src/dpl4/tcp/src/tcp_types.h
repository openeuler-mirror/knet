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

#ifndef TCP_TYPES_H
#define TCP_TYPES_H

#include "dp_types.h"

#include "inet_sk.h"
#include "pbuf.h"
#include "netdev.h"
#include "worker.h"

#include "utils_cfg.h"
#include "utils_base.h"
#include "utils_spinlock.h"
#include "utils_statistic.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TCP_TIMERID_FAST,
    TCP_TIMERID_SLOW,
    TCP_TIMERID_DELAYACK,
    TCP_TIMERID_BUTT,
};

enum {
    TCP_ACTIVE,    // 主动建链
    TCP_PASSIVE,   // 被动建链
};

enum {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECV,
    TCP_ESTABLISHED,
    TCP_CLOSE_WAIT,
    TCP_FIN_WAIT1,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_FIN_WAIT2,
    TCP_TIME_WAIT,
    TCP_ABORT, // 用来区别接受RST报文时被动断链时的状态

    TCP_MAX_STATES /* Leave at the end! */
};

typedef struct TcpSk TcpSk_t;

typedef struct {
    LIST_ENTRY(TcpSk) node;
    uint32_t flags;
} TcpEvNode_t;

typedef struct {
    int freeCnt;
    Spinlock_t lock;
} TcpCfgCtx_t;

typedef struct {
    uint8_t rcvSynOpt;

    uint16_t mss;
    uint16_t ws;
    uint32_t tsVal;
    uint32_t tsEcho;
} TcpSynOpts_t;

typedef struct {
    uint8_t  pentry;
    uint16_t mss;
    uint16_t tsoSize;
    void* flow;
    Netdev_t* dev;
} TcpXmitInfo_t;

/**
 * @brief TCP 拥塞算法初始化，在tcp申请时调用
 */
typedef int (*TcpCaInitFn_t)(TcpSk_t* tcp);

/**
 * @brief TCP拥塞算法去初始化
 */
typedef int (*TcpCaDeinitFn_t)(TcpSk_t* tcp);

/**
 * @brief 两个调用点，TCP建链完成后以及长时间无数据传输后
 */
typedef void (*TcpCaRestartFn_t)(TcpSk_t* tcp);

/**
 * @brief TCP正常ACK确认
 *        acked: 本次确认的数据长度
 *        rtt: 未经过平滑的rtt时间
 */
typedef void (*TcpCaAckedFn_t)(TcpSk_t* tcp, uint32_t acked, uint32_t rtt);

/**
 * @brief TCP重复ACK确认
 */
typedef void (*TcpCaDupAckFn_t)(TcpSk_t* tcp);

/**
 * @brief TCP超时处理
 */
typedef void (*TcpCaTimeoutFn_t)(TcpSk_t* tcp);

typedef struct TcpCaMeth {
    int                     algId;
    TcpCaInitFn_t           caInit;
    TcpCaInitFn_t           caDeinit;
    TcpCaRestartFn_t        caRestart;
    TcpCaAckedFn_t          caAcked;
    TcpCaDupAckFn_t         caDupAck;
    TcpCaTimeoutFn_t        caTimeout;
    const struct TcpCaMeth* next;
} TcpCaMeth_t;

#define TCP_TIMER_CNT 3 // REXMIT和KEEPTIMER为固定的是定时器，坚持、2MSL公用一个定时器

// 记录listen和普通tcp的公共数据，继承时也仅需要继承common中的内容

typedef LIST_HEAD(TcpBacklog, TcpSk) TcpBacklog_t;

typedef struct TcpListenerCb {
    InetSk_t inetSk;

    uint8_t state;

    union {
        struct {
            uint8_t noVerifyCksum : 1; // 不校验校验和，通过配置读取
            uint8_t ackNow : 1;
            uint8_t delayAckEnable : 1; // 使能延迟ACK功能
            uint8_t nodelay : 1; // nagle算法使能标记位
            uint8_t rttRecord : 1; // rtt开始记录标记
        };
        uint8_t options;
    }; // tcp options

    TcpBacklog_t uncomplete;
    TcpBacklog_t complete;

    int childCnt;
} TcpListenerCb_t;

// 数组 记录sack块（数据接收方）
typedef struct {
    uint32_t seqStart;    /* SACK块左边缘 */
    uint32_t seqEnd;      /* SACK块右边缘（不包含） */
} TcpSackBlock_t;

// 有序链表 记录空洞（报文发送方）
typedef struct TcpSackHole {
    LIST_ENTRY(TcpSackHole) node;
    uint32_t seqStart;      /* 空洞起始序号 */
    uint32_t seqEnd;        /* 空洞结束序号 */
    uint32_t seqRetrans;   /* 当前SACK空洞重传起始位(此序号未重传) */
} TcpSackHole_t;

typedef LIST_HEAD(, TcpSackHole) TcpSackHoleHead;

#define TCP_SACK_BLOCK_SIZE 6       // tcp选项中多写4个block块，预留2组

typedef struct {
    TcpSackHoleHead  sackHoleHead;      // 有序链表
    TcpSackBlock_t   sackBlock[TCP_SACK_BLOCK_SIZE];
    uint32_t         rcvSackEnd;        // 数据发送端 收到的sack信息中的最后  为填写最新的hole提供信息
    uint32_t         sackHoleNum;
    uint8_t          sackBlockNum;
    uint8_t          reserve[3];
} TcpSackInfo_t;

typedef struct {
    int (*hash)(Sock_t* sk); // per worker hash表插入
    void (*unhash)(Sock_t* sk); // per worker hash表移除
    int  (*getXmitInfo)(Sock_t* sk, TcpXmitInfo_t* info); // ipv4/ipv6 报文下行信息获取
    void (*freeFunc)(Sock_t* sk); // ipv4/ipv6 sock释放
    void (*waitIdle)(Sock_t *sk); // 等待tbl空闲
    void (*listenerInsert)(Sock_t *sk);
    void (*listenerRemove)(Sock_t *sk);
    int (*connectTblInsert)(Sock_t *sk);
    void (*connectTblRemove)(Sock_t *sk);
    void (*globalInsert)(Sock_t *sk);
    void (*globalRemove)(Sock_t *sk);
} TcpFamilyOps_t;

// TCP内存结构：
// | -- Sock_t -- | -- TcpSk_t -- | -- InetSk/Inet6Sk -- |
struct TcpSk {
    Sock_t sk;

    uint8_t state;

    uint8_t connType;

    union {
        struct {
            uint8_t noVerifyCksum : 1; // 不校验校验和，通过配置读取
            uint8_t ackNow : 1;
            uint8_t delayAckEnable : 1; // 使能延迟ACK功能
            uint8_t nodelay : 1; // nagle算法使能标记位
            uint8_t rttRecord : 1; // rtt开始记录标记
            uint8_t cork : 1; // cork标志
            uint8_t deferAccept : 1; // 延迟建链标记
        };
        uint8_t options;
    }; // tcp options
    uint8_t flags;

    uint16_t lport;
    uint16_t pport;

    int16_t wid;
    int16_t txQueid;

    uint32_t pseudoHdrCksum;

    int childCnt;
    int backlog;

    TcpBacklog_t uncomplete;
    TcpBacklog_t complete;
    LIST_ENTRY(TcpSk) childNode;

    uint8_t synOpt; // 可协商的选项
    uint8_t negOpt; // 已协商的选项

    uint8_t rcvWs;
    uint8_t sndWs;

    uint16_t rcvMss;
    uint16_t mss;

    uint8_t accDataCnt; // accumulate累计数据报文个数，记录当前收取的数据报文个数
    uint8_t accDataMax; // 设置N个数据回复一个ACK

    uint32_t iss; // initial send sequence number
    uint32_t irs; // initial receive sequence number

    uint32_t sndUna; // send unacknowledged
    uint32_t sndNxt; // send next
    uint32_t sndMax; // send max
    uint32_t sndWnd; // send window, peer window
    uint32_t sndUp; // send urgent pointer
    uint32_t sndWl1; // segment sequence number used for last window update
    // uint32_t sndWl2; // segment acknowledgment number used for last window update

    uint32_t rcvNxt; // receive next
    uint32_t rcvWnd; // receive window, local window
    union {
        uint32_t rcvUp; // receive urgent pointer
        uint32_t rcvMax; // 收到FIN后，记录此时的最大序号
    };
    uint32_t rcvWup; // 最后一次发送窗口更新时的 rcvNxt

    uint8_t dupAckCnt;
    uint8_t caState; // ca状态

    uint32_t cwnd; // cong
    uint32_t ssthresh;
    uint32_t seqRecover;
    uint32_t reorderCnt; // 乱序报文个数，重复ACK超过此数值后，触发快速重传

    void*              caCb; // 拥塞算法控制块
    const TcpCaMeth_t* caMeth; // 拥塞算法

    uint32_t rttStartSeq; // 记录rtt评估开始时的序号

    uint32_t srtt;
    uint32_t rttval;
    uint32_t tsVal;
    uint32_t tsEcho;

    uint32_t lastChallengeAckTime;

    TW_Node_t twNode[TCP_TIMERID_BUTT];
    uint16_t  expiredTick[TCP_TIMERID_BUTT];

    uint16_t idleStart;
    uint16_t maxIdleTime;
    uint16_t keepIdle;
    uint16_t keepIntvl;
    uint8_t  keepProbes;
    uint8_t  keepProbeCnt;
    uint8_t  backoff;
    uint8_t  maxRexmit; // 用户设置DefferAccept场景下的最大重传次数
    uint8_t  fastMode; // 加入快定时器的模式，重传or坚持定时器

    uint32_t tsqFlags;
    uint32_t tsqFlagsLock;

    TcpSk_t* parent;

    DP_Pbuf_t* rtxHead; // next pbuf to rexmit

    PBUF_Chain_t sndQue;
    PBUF_Chain_t rcvQue;

    PBUF_Chain_t rexmitQue;
    PBUF_Chain_t reassQue;

    TcpSackInfo_t* sackInfo;

    LIST_ENTRY(TcpSk) txEvNode;
    LIST_ENTRY(TcpSk) rxEvNode;
};

typedef struct {
    uint8_t  hdrLen;
    uint8_t  thFlags;
    uint16_t dataLen;
    uint32_t seq;
    uint32_t ack;
    uint32_t endSeq;
    uint32_t sndWnd;
    uint32_t tsVal;
    uint32_t tsEcho;
    uint8_t* sackOpt;       // optSize sackBlock[x]
} TcpPktInfo_t;

typedef struct {
    uint8_t tcpState; /* TCP当前状态 */
    uint8_t tcpCaState; /* TCP拥塞控制状态 */
    uint8_t tcpRexmits;
    uint8_t tcpProbes;
    uint8_t tcpBackoff;
    uint8_t tcpOptions;
    uint8_t tcpSndWScale : 4;  /**< 对端的窗口扩大因子 */
    uint8_t tcpRcvWScale : 4;  /**< 本端的窗口扩大因子 */
    uint8_t tcpDeliveryRateLimited : 1;

    uint32_t tcpRto;           /**< 超时重传时间，单位为微秒 */
    uint32_t tcpAto;
    uint32_t tcpSndMSS;        /* mss值 对照 tcpi_snd_mss */
    uint32_t tcpRcvMSS;        /**< 对端通告的MSS */

    uint32_t tcpUnack;
    uint32_t tcpSacked;
    uint32_t tcpLost;
    uint32_t tcpRetrans;
    uint32_t tcpFackets;

    uint32_t tcpLastDataSent;
    uint32_t tcpLastAckSend;
    uint32_t tcpLastDataRecv;
    uint32_t tcpLastAckRecv;

    uint32_t tcpMtu;
    uint32_t tcpRcvSshThresh;
    uint32_t tcpRtt;        /* rtt值 对照 tcpi_rtt */
    uint32_t tcpRttVar;
    uint32_t tcpSndSshThresh;
    uint32_t tcpSndCwnd;    /* 拥塞控制窗口大小 对照tcpi_snd_cwnd */
} TcpInfo_t; // iperf测试需要，当前只提供这么多字段，以供测试使用

#define TcpSK(sk)     ((TcpSk_t*)(sk))
#define TcpSk2Sk(tcp) ((Sock_t*)(tcp))

typedef LIST_HEAD(, TcpSk) TcpListHead_t;

#define TcpGetLport(tcp)    ((tcp)->lport)
#define TcpGetPport(tcp)    ((tcp)->pport)
#define TcpSetLport(tcp, p) ((tcp)->lport) = (p)
#define TcpSetPport(tcp, p) ((tcp)->pport) = (p)

#define TcpSeqCmp(a, b) (int)((a) - (b))
#define TcpSeqLt(a, b)  (TcpSeqCmp((a), (b)) < 0)
#define TcpSeqGt(a, b)  (TcpSeqCmp((a), (b)) > 0)
#define TcpSeqLeq(a, b) (TcpSeqCmp((a), (b)) <= 0)
#define TcpSeqGeq(a, b) (TcpSeqCmp((a), (b)) >= 0)
#define TcpTimeLt(a, b) (TcpSeqCmp((a), (b)) < 0)

#define TCP_SEQ_MAX(a, b)                \
    ({                                 \
        typeof(a) aTemp = (a);         \
        typeof(b) bTemp = (b);         \
        TcpSeqGt(aTemp, bTemp) ? aTemp : bTemp; \
    })

#define TCP_SEQ_MIN(a, b)                \
    ({                                 \
        typeof(a) aTemp = (a);         \
        typeof(b) bTemp = (b);         \
        TcpSeqLt(aTemp, bTemp) ? aTemp : bTemp; \
    })

#define TcpSetState(tcpcb, newstate) TcpConnStateStat((tcpcb), (newstate))
#define TcpState(tcpcb)              ((tcpcb)->state)

#define TCP_DEFAULT_SND_WND 8192
#define TCP_DEFAULT_WS_MIN  7

#define TCP_OPT_TSTAMP_APPA_VAL \
    UTILS_HTONL(                \
        ((DP_TCPOPT_NOP << 24) | (DP_TCPOPT_NOP << 16) | (DP_TCPOPT_TIMESTAMP << 8) | (DP_TCPOLEN_TIMESTAMP)))

#define MAX_TCP_KEEPIDLE  32767
#define MAX_TCP_KEEPINTVL 32767
#define MAX_TCP_KEEPCNT   127

#define TCP_SYN_OPT_MSS            0x1
#define TCP_SYN_OPT_WINDOW         0x2
#define TCP_SYN_OPT_SACK_PERMITTED 0x4
#define TCP_SYN_OPT_TIMESTAMP      0x8

// TCP_SYN_OPT_SACK_PERMITTED根据配置项添加
#define TCP_SYN_OPTIONS (TCP_SYN_OPT_MSS | TCP_SYN_OPT_WINDOW | TCP_SYN_OPT_TIMESTAMP)

#define TcpHasMss(tcp)           (((tcp)->synOpt & TCP_SYN_OPT_MSS) != 0)
#define TcpHasWs(tcp)            (((tcp)->synOpt & TCP_SYN_OPT_WINDOW) != 0)
#define TcpHasSackPermitted(tcp) (((tcp)->synOpt & TCP_SYN_OPT_SACK_PERMITTED) != 0)
#define TcpHasTs(tcp)            (((tcp)->synOpt & TCP_SYN_OPT_TIMESTAMP) != 0)

#define TcpNegTs(tcp) (((tcp)->negOpt & TCP_SYN_OPT_TIMESTAMP) != 0)
#define TcpNegSack(tcp) (((tcp)->negOpt & TCP_SYN_OPT_SACK_PERMITTED) != 0)
#define TcpNegWs(tcp) (((tcp)->negOpt & TCP_SYN_OPT_WINDOW) != 0)

// 支持Sack 且 sackInfo内存申请成功
#define TCP_SACK_AVAILABLE(tcp)      ((tcp)->sackInfo != NULL)

static inline uint32_t TcpGetRcvSpace(TcpSk_t* tcp)
{
    uint32_t totBufLen = (uint32_t)TcpSk2Sk(tcp)->rcvBuf.bufLen + (uint32_t)tcp->rcvQue.bufLen;

    if (totBufLen > TcpSk2Sk(tcp)->rcvHiwat) {
        return 0;
    }

    return TcpSk2Sk(tcp)->rcvHiwat - totBufLen;
}

static inline uint32_t TcpGetSndSpace(TcpSk_t* tcp)
{
    uint32_t totBufLen =
        (uint32_t)TcpSk2Sk(tcp)->sndBuf.bufLen + (uint32_t)tcp->rexmitQue.bufLen + (uint32_t)tcp->sndQue.bufLen;

    if (totBufLen >= TcpSk2Sk(tcp)->sndHiwat) {
        return 0;
    }

    return TcpSk2Sk(tcp)->sndHiwat - totBufLen;
}

static inline uint32_t TcpGetRcvWnd(TcpSk_t* tcp)
{
    /*
      计算当前接收窗口的大小
          |--------- tcp->rcvWnd ---------|
          |-------------------------------|
          |              |
      tcp->rvWup      tcp->rcvNxt
                         |----- win ------|
     */
    int win = (int)(tcp->rcvWup + tcp->rcvWnd - tcp->rcvNxt);
    if (win < 0) {
        win = 0;
    }
    return (uint32_t)win;
}

static inline uint32_t TcpSelectRcvWnd(TcpSk_t* tcp)
{
    /*
      选择接收窗口大小，当前简单使用接收缓冲区剩余空间
    */
    return TcpGetRcvSpace(tcp);
}

/* 测试接口，方便测试观测拥塞窗口变化 */
static inline uint32_t TcpGetCwnd(TcpSk_t* tcp)
{
    return tcp->cwnd;
}

/* 测试接口，方便测试观测拥塞状态变化 */
static inline uint32_t TcpGetCaState(TcpSk_t* tcp)
{
    return tcp->caState;
}

static const uint32_t paField[TCP_MAX_STATES] = {
    DP_TCP_CONN_STAT_MAX,
    DP_TCP_LISTEN,
    DP_TCP_SYN_SENT,
    DP_TCP_SYN_RCVD,
    DP_TCP_PASSIVE_ESTABLISHED,
    DP_TCP_PASSIVE_CLOSE_WAIT,
    DP_TCP_PASSIVE_FIN_WAIT_1,
    DP_TCP_PASSIVE_CLOSING,
    DP_TCP_PASSIVE_LAST_ACK,
    DP_TCP_PASSIVE_FIN_WAIT_2,
    DP_TCP_PASSIVE_TIME_WAIT,
    DP_TCP_ABORT,
};

static const uint32_t acField[TCP_MAX_STATES] = {
    DP_TCP_CONN_STAT_MAX,
    DP_TCP_LISTEN,
    DP_TCP_SYN_SENT,
    DP_TCP_SYN_RCVD,
    DP_TCP_ACTIVE_ESTABLISHED,
    DP_TCP_ACTIVE_CLOSE_WAIT,
    DP_TCP_ACTIVE_FIN_WAIT_1,
    DP_TCP_ACTIVE_CLOSING,
    DP_TCP_ACTIVE_LAST_ACK,
    DP_TCP_ACTIVE_FIN_WAIT_2,
    DP_TCP_ACTIVE_TIME_WAIT,
    DP_TCP_ABORT,
};

static inline void ProcTcpOldState(int wid, uint8_t state, uint8_t type)
{
    if (state != TCP_CLOSED) {
        if (type == TCP_ACTIVE) {
            DP_DEC_TCP_CONN_STAT(wid, acField[state]);
        } else {
            DP_DEC_TCP_CONN_STAT(wid, paField[state]);
        }
    }
}

static inline void ProcTcpNewState(int wid, uint8_t state, uint8_t type)
{
    if (state != TCP_CLOSED) {
        if (type == TCP_ACTIVE) {
            DP_INC_TCP_CONN_STAT(wid, acField[state]);
        } else {
            DP_INC_TCP_CONN_STAT(wid, paField[state]);
        }
    }
}

/* 设置TCP连接状态，并更新状态统计 */
static inline void TcpConnStateStat(TcpSk_t* tcp, uint8_t newState)
{
    uint8_t oldState = tcp->state;
    int wid = (tcp->wid != -1) ? tcp->wid : (int)WORKER_GetSelfId();

    ASSERT(wid < CFG_GET_VAL(DP_CFG_WORKER_MAX));

    tcp->state = newState;

    ProcTcpOldState(wid, oldState, tcp->connType);
    ProcTcpNewState(wid, newState, tcp->connType);
}

#ifdef __cplusplus
}
#endif
#endif
