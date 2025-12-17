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

#include "dp_tcp_cc_api.h"

#include "dp_types.h"

#include "inet_sk.h"
#include "pbuf.h"
#include "netdev.h"
#include "worker.h"

#include "utils_cfg.h"
#include "utils_base.h"
#include "utils_minmax.h"
#include "utils_spinlock.h"
#include "utils_statistic.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TCP_TIMERID_FAST,
    TCP_TIMERID_SLOW,
    TCP_TIMERID_DELAYACK,
    TCP_TIMERID_PACING,
    TCP_TIMERID_BUTT,
};

enum {
    TCP_ACTIVE,    // 主动建链
    TCP_PASSIVE,   // 被动建链
};

// 与DP_SOCKET_STATE保持一致(dp_debug_api.h)
enum {
    TCP_CLOSED = 0,
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

    TCP_MAX_STATES /* Leave at the end! */
};

#define DP_TCP_MAXWIN 0xFFFF //!< TCP未进行窗口因子缩放时窗口最大值
#define DP_TCP_MAX_WINSHIFT 14 //!< 最大窗口缩放因子

typedef struct TcpSk TcpSk_t;

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

typedef struct {
    uint32_t priorAckTime; /* starting timestamp for interval */
    uint32_t priorAckCnt;  /* tp->delivered at "prior_mstamp" */
    int32_t incrAckCnt;
    int32_t intervalMs; /* time for tp->delivered to incr "delivered" */
    int32_t rttMs;
    uint32_t losses;          /* number of packets marked lost upon ACK */
    uint32_t ackedSacked;    /* number of packets newly (S)ACKed upon ACK */
    uint32_t priorInFlight; /* in flight before this ACK */     // 报文数量
    bool isAppLimited;
    uint8_t reserved[7];
} TcpRateSample_t;

struct tcpSendTimeState {
    uint32_t packetTxStamp;
    uint32_t shotConnFirstTxMstamp;     /* Tcp连接中 周期开始时间戳快照信息 */
    uint32_t shotConnDeliveredCnt;      /* Tcp连接中 周期开始发包总数快照信息 */
    uint32_t shotConnDeliveredMstamp;   /* Tcp连接中 周期开始发包总数时间戳快照信息 */
    bool     isAppLimited;
    uint8_t  reserved[3];
};

struct tcpRecvTimeState {
    uint32_t packetRecvTime;
};

#define TF_SEG_NONE 0x0000U
#define TF_SEG_SACKED 0x0001U        /* Segment Sacked */
#define TF_SEG_RETRANSMITTED 0x0002U /* Retransmitted as part of SACK based loss recovery alg */

typedef struct TcpScoreBoard {
    uint32_t startSeq;         /* 该报文起始序列号 */
    uint32_t endSeq;           /* 该报文结束序列号 */

    /* SACK状态控制块 */
    uint32_t state;

    union {
        struct tcpSendTimeState tx;
        struct tcpRecvTimeState rx;
    } rtxTimeState;
} TcpScoreBoard_t;

#define PBUF_GET_SCORE_BOARD(pbuf) ((TcpScoreBoard_t*)((pbuf)->scb))

typedef LIST_HEAD(TcpBacklog, TcpSk) TcpBacklog_t;

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
    void (*mFree)(Sock_t* sk); // ipv4/ipv6 sock释放
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

    uint16_t maxSegNum;

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
    uint32_t sndSml; // send small pkt end seq

    uint32_t rcvNxt; // receive next
    uint32_t rcvWnd; // receive window, local window
    uint32_t rcvAdvertise; // 本端向对端通告的接收窗口大小的右边沿序号值, 等于rcvNxt加上通告的窗口大小
    union {
        uint32_t rcvUp; // receive urgent pointer
        uint32_t rcvMax; // 收到FIN后，记录此时的最大序号
    };
    uint32_t rcvWup; // 最后一次发送窗口更新时的 rcvNxt

    uint8_t dupAckCnt;
    uint8_t caIsInited : 1;
    uint8_t caState : 7; // ca状态
    int8_t  nextCaMethId; // 建链后切换拥塞算法时暂存算法ID，等待TSQ调度

    uint8_t  trType; // 当前使用的定时器类型, 见TCP_TimetType_t

    uint32_t cwnd; // cong
    uint32_t ssthresh;
    uint32_t seqRecover;
    uint32_t reorderCnt; // 乱序报文个数，重复ACK超过此数值后，触发快速重传

    void*              caCb; // 拥塞算法控制块
    const DP_TcpCaMeth_t* caMeth; // 拥塞算法
    uint32_t initCwnd; // 拥塞窗口初始值

    uint32_t rttStartSeq; // 记录rtt评估开始时的序号

    uint32_t srtt;
    uint32_t rttval;
    uint32_t maxRtt;
    uint32_t tsVal;
    uint32_t tsEcho;

    Minmax_t rttMin;
    uint32_t pacingRate;   /* bytes per second for bbr */
    /* RTT measurement */
    uint32_t tcpMstamp;  /* most recent packet received/sent */
    uint32_t ConnDeliveredMstamp;       // 上一个到达的ack的时间
    uint32_t ConnFirstTxMstamp;
    uint32_t inflight;      // 在途数据量
    TcpRateSample_t rs;

    uint32_t connDeliveredCnt;
    uint32_t connLostCnt; /* Total data packets cum_lost_cnt incl. rexmits */
    uint32_t appLimited;  /* limited until "delivered" reaches this val */

    uint32_t lastChallengeAckTime;

    TW_Node_t twNode[TCP_TIMERID_BUTT];
    uint16_t  expiredTick[TCP_TIMERID_BUTT];

    uint16_t rxtMin;
    uint32_t idleStart;
    uint16_t quickAckNum; // 逐包回复ACK
    uint16_t maxIdleTime;
    uint32_t keepIdle;
    uint32_t keepIntvl;
    uint8_t  keepProbes;
    uint8_t  keepProbeCnt;
    uint8_t  backoff;
    uint8_t  maxRexmit; // 用户设置DefferAccept场景下的最大重传次数
    uint8_t  fastMode; // 加入快定时器的模式，重传or坚持定时器
    uint8_t  frto : 1; // 当前是否在 frto 状态
    uint8_t  rttFlag : 1;   // 标识是否需要更新rtt，在非重传情况发送报文时（建链、数据）设置为1，表示可以更新rtt；在重传时、更新rtt后设置成0
    uint8_t  cookie : 1;    // 是否通过配置项启用socket
    uint8_t  resv : 5;
    uint8_t  force; // 本次是否强制发送数据

    uint8_t  tsqNested;     // 共线程模式使用，记录处于tsq流程的嵌套层数
    uint32_t tsqFlags;
    uint32_t tsqFlagsLock;

    uint32_t rcvDrops;
    uint32_t rexmitCnt;
    uint32_t startConn;
    uint32_t connLatency;       // 建链时延

    uint32_t userTimeout;
    uint32_t userTimeStartFast;
    uint32_t userTimeStartSlow;

    uint32_t synRetries; // SYN或SYN/ACK重传的次数
    uint32_t keepIdleLimit; // keep idle超时上限
    uint32_t keepIdleCnt;   // keep idle超时次数

    long int tid;

    TcpSk_t* parent;

    DP_Pbuf_t* rtxHead; // next pbuf to rexmit

    PBUF_Chain_t sndQue;
    PBUF_Chain_t rcvQue;

    PBUF_Chain_t rexmitQue;
    PBUF_Chain_t reassQue;

    TcpSackInfo_t* sackInfo;

    LIST_ENTRY(TcpSk) txEvNode;
    LIST_ENTRY(TcpSk) rxEvNode;
    uint8_t reserv[8]; // 预留8字节
};

#define TCP_PI_SACKED_DATA     0x01 /* SACK 了新数据 */
#define TCP_PI_HAS_TIMESTAMP   0x02 /* 收到报文携带了时间戳 */

#define TCP_PI_ERR_TIMESTAMP 0x01   // 时间戳异常，此时协议栈记录的tsEcho大于报文的tsVal

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
    uint32_t pktType;       // 报文在处理过程中设置，方便其它步骤处理
    uint32_t errCode;       // 错误码，临时方案，传递报文中的错误信息用于后续处理
    uint8_t* sackOpt;       // optSize sackBlock[x]
} TcpPktInfo_t;

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

#define TcpState(tcpcb)              ((tcpcb)->state)

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
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD &&
        (TcpSk2Sk(tcp)->sndBuf.pktCnt + tcp->rexmitQue.pktCnt + tcp->sndQue.pktCnt >=
        (uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_SNDBUF_PBUFCNT_MAX))) {
        return 0;
    }

    size_t totBufLen = TcpSk2Sk(tcp)->sndBuf.bufLen + tcp->rexmitQue.bufLen + tcp->sndQue.bufLen;

    if (totBufLen >= (size_t)TcpSk2Sk(tcp)->sndHiwat) {
        return 0;
    }

    return TcpSk2Sk(tcp)->sndHiwat - (uint32_t)totBufLen;
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
static inline void TcpSetState(TcpSk_t* tcp, uint8_t newState)
{
    uint8_t oldState = tcp->state;
    int wid = (tcp->wid != -1) ? tcp->wid : WORKER_GetSelfId();
    ASSERT(wid < CFG_GET_VAL(DP_CFG_WORKER_MAX));
    // 获取的wid异常时在0号统计
    if (wid < 0) {
        wid = 0;
    }

    tcp->state = newState;

    ProcTcpOldState(wid, oldState, tcp->connType);
    ProcTcpNewState(wid, newState, tcp->connType);
}

// 标记哪些事件目的是通知用户调用 Close
static const uint8_t g_eventNeedClose[SOCK_EVENT_MAX] = {
    [SOCK_EVENT_RCVSYN] = 0,
    [SOCK_EVENT_ACTIVE_CONNECTFAIL] = 1,
    [SOCK_EVENT_RCVFIN] = 1,
    [SOCK_EVENT_RCVRST] = 1,
    [SOCK_EVENT_DISCONNECTED] = 1,
    [SOCK_EVENT_WRITE] = 0,
    [SOCK_EVENT_READ] = 0,
    [SOCK_EVENT_FREE_SOCKCB] = 0,
    [SOCK_EVENT_UPDATE_MTU] = 0,
};

// 标记哪些事件必须在 tsq 处理中通知，让用户在回调中能够安全的调用 Close
static const uint8_t g_eventNeedTsq[SOCK_EVENT_MAX] = {
    // 事件会 Close 监听 fd ，监听 fd 有引用计数保护，可以不用在 tsq 中通知
    [SOCK_EVENT_RCVSYN] = 0,
    // 以下事件会 Close 连接 fd ，需要在 tsq 中通知
    [SOCK_EVENT_ACTIVE_CONNECTFAIL] = 1,
    [SOCK_EVENT_RCVFIN] = 1,
    [SOCK_EVENT_RCVRST] = 1,
    [SOCK_EVENT_DISCONNECTED] = 1,
    [SOCK_EVENT_WRITE] = 1,
    [SOCK_EVENT_READ] = 1,
    // FREE 事件中调用 Close 会失败，不需要在 tsq 中通知
    [SOCK_EVENT_FREE_SOCKCB] = 0,
    [SOCK_EVENT_UPDATE_MTU] = 0,
};

static inline int TcpNotifyCheck(uint8_t event, uint8_t tsqNested)
{
    // 事件需要在 tsq 处理中通知，但是当前没有在 tsq 处理中
    if (g_eventNeedTsq[event] == 1 && tsqNested == 0) {
        return -1;
    }
    return 0;
}

static inline void TcpNotifyEvent(Sock_t* sk, uint8_t event, uint8_t tsqNested)
{
    (void)tsqNested;
    ASSERT(TcpNotifyCheck(event, tsqNested) == 0);

    // 如果此事件目的是要用户调用 Close 释放资源，但是用户已经调用了 Close ，不必再通知此事件
    if ((g_eventNeedClose[event] == 1) && SOCK_IS_CLOSED(sk)) {
        return;
    }

    SOCK_NotifyEvent(sk, event);
}

#ifdef __cplusplus
}
#endif
#endif
