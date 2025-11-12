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

#ifndef UTILS_STATISTIC_H
#define UTILS_STATISTIC_H

#include <stdint.h>

#include "dp_show_api.h"
#include "dp_debug_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 维测信息show全局结构体变量 */
extern DP_DebugShowHook g_debugShow;

#define DEBUG_SHOW g_debugShow

/* 管理信息库统计项的描述信息最大长度 */
#define DP_MIB_FIELD_NAME_LEN_MAX 40

#define MOD_LEN 16

/**
 * @ingroup pub
 * 数据面处理自动统计项ID
*/
enum DP_ABN_STAT_MIB_E {
    DP_ABN_BASE,              /**< 预留异常基础统计项 */
    DP_CONN_BY_LISTEN_SK,     /**< 监听sk不能发起connect */
    DP_CONNED_SK_REPEAT,      /**< 已建链的sk不能重复建链 */
    DP_CONN_REFUSED,          /**< 连接被拒绝 */
    DP_CONN_IN_PROGRESS,      /**< 连接正在进行中 */
    DP_ACCEPT_NO_CHILD,       /**< accept没有有效的子socket */
    DP_SETOPT_PARAM_INVAL,    /**< 设置tcp选项参数非法 */
    DP_SETOPT_KPID_INVAL,     /**< 设置tcp选项keepidle参数非法 */
    DP_SETOPT_KPIN_INVAL,     /**< 设置tcp选项keepintvl参数非法 */
    DP_SETOPT_KPCN_INVAL,     /**< 设置tcp选项keepcnt参数非法 */
    DP_SETOPT_MAXSEG_INVAL,   /**< 设置tcp选项maxseg参数非法 */
    DP_SETOPT_MAXSEG_STAT,    /**< 设置tcp选项maxseg当前状态异常 */
    DP_SETOPT_DFAC_STAT,      /**< 设置tcp选项defer accept当前状态异常 */
    DP_SETOPT_NO_SUPPORT,     /**< 设置tcp选项opt参数不支持 */
    DP_GETOPT_INFO_INVAL,     /**< 获取tcp选项tcpInfo参数非法 */
    DP_GETOPT_PARAM_INVAL,    /**< 获取tcp选项参数非法 */
    DP_GETOPT_NO_SUPPORT,     /**< 获取tcp选项opt参数不支持 */
    DP_TCP_SND_CONN_REFUSED,  /**< 发送连接已中断 */
    DP_TCP_SND_CANT_SEND,     /**< 当前连接不能再发送数据 */
    DP_TCP_SND_CONN_CLOSED,   /**< 当前发送连接被closed */
    DP_TCP_SND_NO_SPACE,      /**< 发送缓冲区不足 */
    DP_TCP_SND_BUF_NOMEM,     /**< 发送空间无法写入数据 */
    DP_TCP_RCV_CONN_REFUSED,  /**< 接收连接已中断 */
    DP_TCP_RCV_CONN_CLOSED,   /**< 当前接收连接被closed */
    DP_ABN_STAT_MAX,
};

/* TCP统计项枚举 */
enum DP_TCP_STAT_MIB_E {
    DP_TCP_ACCEPTS,                    /**< 被动打开的连接数 */
    DP_TCP_CLOSED,                     /**< 关闭的连接数 (包括丢弃的连接) */
    DP_TCP_CONN_ATTEMPT,               /**< 试图建立连接的次数(调用connect) */
    DP_TCP_CONN_DROPS,                 /**< 在连接建立阶段失败的连接次数(SYN收到之前) */
    DP_TCP_CONNECTS,                   /**< 建链成功的次数 */
    DP_TCP_DELAYED_ACK,                /**< 延迟发送的ACK数 */
    DP_TCP_DROPS,                      /**< 意外丢失的连接数(收到SYN之后) */
    DP_TCP_KEEP_DROPS,                 /**< 在保活阶段丢失的连接数(已建立或正等待SYN) */
    DP_TCP_KEEP_PROBE,                 /**< 保活探测探测报文发送次数 */
    DP_TCP_KEEP_TIME_OUT,              /**< 保活定时器或者连接建立定时器超时次数 */
    DP_TCP_PERSIST_DROPS,              /**< 持续定时器超时次数达到最大值的次数 */
    DP_TCP_PERSIST_TIMEOUT,            /**< 持续定时器超时次数 */
    DP_TCP_RCV_ACK_BYTE,               /**< 由收到的ACK报文确认的发送字节数 */
    DP_TCP_RCV_ACK_PACKET,             /**< 收到的ACK报文数 */
    DP_TCP_RCV_ACK_TOO_MUCH,           /**< 收到对未发送数据进行的ACK报文数 */
    DP_TCP_RCV_DUP_ACK,                /**< 收到的重复ACK数 */
    DP_TCP_RCV_BAD_OFF,                /**< 收到的首部长度无效的报文数 */
    DP_TCP_RCV_BAD_SUM,                /**< 收到的校验和错误的报文数 */
    DP_TCP_RCV_BYTE,                   /**< 连续收到的字节数 */
    DP_TCP_RCV_DUP_BYTE,               /**< 完全重复报文中的重复字节数 */
    DP_TCP_RCV_DUP_PACKET,             /**< 完全重复报文的报文数 */
    DP_TCP_RCV_PACKET_AFTER_WND,       /**< 携带数据超出滑动窗口通告值的报文数 */
    DP_TCP_RCV_BYTE_AFTER_WND,         /**< 在滑动窗口已满时收到的字节数 */
    DP_TCP_RCV_PART_DUP_BYTE,          /**< 部分数据重复的报文重复字节数 */
    DP_TCP_RCV_PART_DUP_PACKET,        /**< 部分数据重复的报文数 */
    DP_TCP_RCV_OUT_ORDER_PACKET,       /**< 收到失序的报文数 */
    DP_TCP_RCV_OUT_ORDER_BYTE,         /**< 收到失序的字节数 */
    DP_TCP_RCV_SHORT,                  /**< 长度过短的报文数 */
    DP_TCP_RCV_TOTAL,                  /**< 收到的报文总数 */
    DP_TCP_RCV_PACKET,                 /**< 顺序接收的报文数 */
    DP_TCP_RCV_WND_PROBE,              /**< 收到的窗口探测报文数 */
    DP_TCP_RCV_WND_UPDATE,             /**< 收到的窗口更新报文数 */
    DP_TCP_RCV_RST,                    /**< 收到RST报文 */
    DP_TCP_RCV_INVALID_RST,            /**< 收到的无效的RST报文 */
    DP_TCP_RCV_SYN_ESTABLISHED,        /**< 建链完成后收到序号合法的SYN报文 */
    DP_TCP_RCV_FIN,                    /**< 收到第一个FIN报文个数 */
    DP_TCP_RCV_RXMT_FIN,               /**< 收到重传FIN报文个数 */
    DP_TCP_REXMT_TIMEOUT,              /**< 重传超时次数 */
    DP_TCP_RTT_UPDATED,                /**< RTT估算值更新次数 */
    DP_TCP_SEGS_TIMED,                 /**< 可用于RTT测算的报文数 */
    DP_TCP_SND_BYTE,                   /**< 发送的字节数 */
    DP_TCP_SND_CONTROL,                /**< 发送的控制报文数(SYN FIN RST) */
    DP_TCP_SND_PACKET,                 /**< 发送的数据报文数(数据长度大于0) */
    DP_TCP_SND_PROBE,                  /**< 发送的窗口探测次数 */
    DP_TCP_SND_REXMT_BYTE,             /**< 重传的数据字节数 */
    DP_TCP_SND_ACKS,                   /**< 发送的纯ACK报文数(数据长度为0) */
    DP_TCP_SND_REXMT_PACKET,           /**< 重传的报文数 */
    DP_TCP_SND_TOTAL,                  /**< 发送的报文总数 */
    DP_TCP_SND_WND_UPDATE,             /**< 只携带窗口更新信息的报文数 */
    DP_TCP_TIMEOUT_DROP,               /**< 由于重传超时而丢失的连接数 */
    DP_TCP_RCV_EXD_WND_RST,            /**< 收到超窗reset报文 */
    DP_TCP_DROP_CONTROL_PKTS,          /**< 丢弃的控制报文 */
    DP_TCP_DROP_DATA_PKTS,             /**< 丢弃的数据报文 */
    DP_TCP_SND_RST,                    /**< 发送的RST报文数 */
    DP_TCP_SND_FIN,                    /**< 发送的FIN报文数 */
    DP_TCP_FIN_WAIT_2_DROPS,           /**< 默认FIN_WAIT_2定时器超时断链次数 */
    DP_TCP_RESPONSE_CHALLENGE_ACKS,    /**< 回复挑战ACK个数 */
    DP_TCP_STAT_MAX
};

/* 协议栈报文统计项枚举 */
enum DP_PKT_STAT_MIB_E {
    DP_PKT_LINK_IN,                        /**< 接收入口统计 */
    DP_PKT_ETH_IN,                         /**< eth接收入口统计 */
    DP_PKT_NET_IN,                         /**< net接收入口统计 */
    DP_PKT_ICMP_OUT,                       /**< 数据面产生的icmp差错报文统计 */
    DP_PKT_ARP_DELIVER,                    /**< ARP上送处理统计 */
    DP_PKT_IP_BCAST_DELIVER,               /**< 广播报文上送统计 */
    DP_PKT_NON_FRAG_DELIVER,               /**< 本地接收的非分片非OSPF报文上交FWD分发统计 */
    DP_PKT_UP_TO_CTRL_PLANE,               /**< 上送控制面的报文统计 */
    DP_PKT_REASS_IN_FRAG,                  /**< 进行真重组的分片报文个数 */
    DP_PKT_REASS_OUT_REASS_PKT,            /**< 真重组成功返回的完整报文个数 */
    DP_PKT_NET_OUT,                        /**< net发送出口报文统计 */
    DP_PKT_ETH_OUT,                        /**< eth发送出口报文统计,交给驱动发送 */
    DP_PKT_FRAG_IN,                        /**< 分片报文入口总数 */
    DP_PKT_FRAG_OUT,                       /**< 分片报文出口总数 */
    DP_PKT_ARP_MISS_RESV,                  /**< ARP查找失败,缓存报文并返回去保序的报文统计 */
    DP_PKT_ARP_SEARCH_IN,                  /**< ARP查找入口报文统计 */
    DP_PKT_ARP_HAVE_NORMAL_ARP,            /**< ARP查找成功报文统计(存在正常ARP) */
    DP_PKT_ICMP_ERR_IN,                    /**< 收到ICMP差错报文,不产生差错报文的个数 */
    DP_PKT_NET_BAD_VER,                    /**< IP版本号错误的报文统计 */
    DP_PKT_NET_BAD_HEAD_LEN,               /**< IP首部长度无效的报文统计 */
    DP_PKT_NET_BAD_LEN,                    /**< IP首部和IP数据长度不一致的报文统计 */
    DP_PKT_NET_TOO_SHORT,                  /**< 具有无效数据长度的报文统计 */
    DP_PKT_NET_BAD_SUM,                    /**< 校验和错误的报文统计 */
    DP_PKT_NET_NO_PROTO,                   /**< 具有不支持的协议的报文统计 */
    DP_PKT_NET_NO_ROUTE,                   /**< 路由查找失败的报文统计 */
    DP_PKT_TCP_REASS_PKT,                  /**< TCP重组乱序队列中的报文DB统计 */
    DP_PKT_UDP_IN,                         /**< UDP接收入口统计 */
    DP_PKT_UDP_OUT,                        /**< UDP发送报文统计 */
    DP_PKT_TCP_IN,                         /**< TCP接收入口统计 */
    DP_PKT_SEND_BUF_IN,                    /**< 进入发送缓冲区报文个数统计 */
    DP_PKT_SEND_BUF_OUT,                   /**< 从发送缓冲区释放的报文个数统计(ACK掉的报文统计) */
    DP_PKT_SEND_BUF_FREE,                  /**< 从发送缓冲区释放的报文个数统计(释放socket节点释放的报文统计) */
    DP_PKT_RECV_BUF_IN,                    /**< 进入接收缓冲区报文个数统计 */
    DP_PKT_RECV_BUF_OUT,                   /**< 从接收缓冲区释放的报文个数统计(用户接收走的报文统计) */
    DP_PKT_RECV_BUF_FREE,                  /**< 从接收缓冲区释放的报文个数统计 */
    DP_PKT_STAT_MAX
};

/* TCP连接统计项枚举 */
enum DP_TCP_CONN_STAT_MIB_E {
    DP_TCP_LISTEN = 0,             /**< 监听socket的状态计数 */
    DP_TCP_SYN_SENT,               /**< 主动建链SYN_SEND状态计数 */
    DP_TCP_SYN_RCVD,               /**< 被动建链SYN_RCVD 状态计数 */
    DP_TCP_PASSIVE_ESTABLISHED,    /**< 被动建链ESTABLISHED 状态计数 */
    DP_TCP_ACTIVE_ESTABLISHED,     /**< 主动建链ESTABLISHED 状态计数 */
    DP_TCP_PASSIVE_CLOSE_WAIT,     /**< 被动建链CLOSE_WAIT 状态计数 */
    DP_TCP_ACTIVE_CLOSE_WAIT,      /**< 主动建链CLOSE_WAIT 状态计数 */
    DP_TCP_PASSIVE_FIN_WAIT_1,     /**< 被动建链FIN_WAIT_1 状态计数 */
    DP_TCP_ACTIVE_FIN_WAIT_1,      /**< 主动建链FIN_WAIT_1 状态计数 */
    DP_TCP_PASSIVE_CLOSING,        /**< 被动建链CLOSING 状态计数 */
    DP_TCP_ACTIVE_CLOSING,         /**< 主动建链CLOSING 状态计数 */
    DP_TCP_PASSIVE_LAST_ACK,       /**< 被动建链LAST_ACK 状态计数 */
    DP_TCP_ACTIVE_LAST_ACK,        /**< 主动建链LAST_ACK 状态计数 */
    DP_TCP_PASSIVE_FIN_WAIT_2,     /**< 被动建链FIN_WAIT_2 状态计数 */
    DP_TCP_ACTIVE_FIN_WAIT_2,      /**< 主动建链FIN_WAIT_2 状态计数 */
    DP_TCP_PASSIVE_TIME_WAIT,      /**< 被动建链TIME_WAIT 状态计数 */
    DP_TCP_ACTIVE_TIME_WAIT,       /**< 主动建链TIME_WAIT 状态计数 */
    DP_TCP_ABORT,                  /**< 被动建链接收到RST报文断链状态计数*/
    DP_TCP_CONN_STAT_MAX
};

/* 协议栈统计项描述字段结构 */
typedef struct {
    uint32_t fieldId;    /* 协议栈统计字段项目编号 */
    char *fieldName;     /* 协议栈统计字段项目名称 */
} DP_MibField_t;

/* 协议栈统计类别信息结构 */
typedef struct {
    uint32_t mibType;                              /* MIB类型 */
    uint32_t fieldNum;                             /* MIB统计项目数量 */
    int8_t mibDescript[DP_MIB_FIELD_NAME_LEN_MAX]; /* MIB类型含义 */
} DP_MibDetail_t;

/* 协议栈统计项打点详细信息结构 */
typedef struct {
    uint8_t fieldName[DP_MIB_FIELD_NAME_LEN_MAX]; /* MIB统计项名称 */
    uint64_t fieldValue;                          /* MIB统计项打点数量 */
} DP_MibStatistic_t;

/* TCP统计结构 */
typedef struct {
    uint64_t fieldStat[DP_TCP_STAT_MAX];
} DP_TcpMibStat_t;

/* 协议栈报文统计结构 */
typedef struct {
    uint64_t fieldStat[DP_PKT_STAT_MAX];
} DP_PktMibStat_t;

/* 协议栈异常打点统计结构 */
typedef struct {
    uint64_t fieldStat[DP_ABN_STAT_MAX];
} DP_AbnMibStat_t;

/* TCP连接统计结构 */
typedef struct {
    uint64_t fieldStat[DP_TCP_CONN_STAT_MAX];
} DP_TcpConnMibStat_t;

/* 协议栈MIB汇总统计结构 */
typedef struct {
    DP_TcpMibStat_t *tcpStat;
    DP_PktMibStat_t *pktStat;
    DP_AbnMibStat_t *abnStat;
    DP_TcpConnMibStat_t *tcpConnStat;
} DP_MibStat_t;

extern DP_MibStat_t g_statMibs;
typedef struct {
    uint64_t ipFragPktNum;    /**< ipv4重组缓存pbuf数目 */
    uint64_t tcpReassPktNum;  /**< ipv4重组缓存pbuf数目 */
    uint64_t sendBufPktNum;   /**< ipv4重组缓存pbuf数目 */
    uint64_t recvBufPktNum;   /**< ipv4重组缓存pbuf数目 */
} DP_PbufStat_t;

/* 全局异常打点统计，不支持workerId统计，通过原子操作增减 */
#define DP_GET_ABN_STAT(field) g_statMibs.abnStat->fieldStat[(field)]
#define DP_ADD_ABN_STAT(field) ATOMIC64_Inc(&(DP_GET_ABN_STAT(field)))
#define DP_SUB_ABN_STAT(field) ATOMIC64_Dec(&(DP_GET_ABN_STAT(field)))

/* 基于workerId统计，tcp打点数据获取及增减 */
#define DP_GET_TCP_STAT(wid, field)        g_statMibs.tcpStat[(wid)].fieldStat[(field)]
#define DP_ADD_TCP_STAT(wid, field, value) DP_GET_TCP_STAT((wid), (field)) += (uint32_t)(value)
#define DP_SUB_TCP_STAT(wid, field, value) DP_GET_TCP_STAT((wid), (field)) -= (uint32_t)(value)
#define DP_INC_TCP_STAT(wid, field)        DP_ADD_TCP_STAT((wid), (field), 1)
#define DP_DEC_TCP_STAT(wid, field)        DP_SUB_TCP_STAT((wid), (field), 1)

/* 基于workerId统计，tcp conn打点数据获取及增减 */
#define DP_GET_TCP_CONN_STAT(wid, field)        g_statMibs.tcpConnStat[(wid)].fieldStat[(field)]
#define DP_ADD_TCP_CONN_STAT(wid, field, value) DP_GET_TCP_CONN_STAT((wid), (field)) += (uint32_t)(value)
#define DP_SUB_TCP_CONN_STAT(wid, field, value) DP_GET_TCP_CONN_STAT((wid), (field)) -= (uint32_t)(value)
#define DP_INC_TCP_CONN_STAT(wid, field)        DP_ADD_TCP_CONN_STAT((wid), (field), 1)
#define DP_DEC_TCP_CONN_STAT(wid, field)        DP_SUB_TCP_CONN_STAT((wid), (field), 1)

/* 基于workerId统计，package打点数据获取及增减 */
#define DP_GET_PKT_STAT(wid, field)        g_statMibs.pktStat[(wid)].fieldStat[(field)]
#define DP_ADD_PKT_STAT(wid, field, value) DP_GET_PKT_STAT((wid), (field)) += (uint32_t)(value)
#define DP_SUB_PKT_STAT(wid, field, value) DP_GET_PKT_STAT((wid), (field)) -= (uint32_t)(value)
#define DP_INC_PKT_STAT(wid, field)        DP_ADD_PKT_STAT((wid), (field), 1)
#define DP_DEC_PKT_STAT(wid, field)        DP_SUB_PKT_STAT((wid), (field), 1)

typedef void (*DP_ShowStatByType)(DP_StatType_t type, int workerId, uint32_t flag);

typedef struct {
    uint32_t type;
    DP_ShowStatByType showStat;
} DP_TYPE_STATIS_S;

uint32_t GetMemModName(uint32_t modId, uint8_t *modName);
uint32_t GetFieldName(uint32_t fieldId, DP_StatType_t type, DP_MibStatistic_t *showStat);
uint64_t GetFieldValue(int workerId, uint32_t fieldId, DP_StatType_t type);

uint64_t GetSendBufPktNum(int workerId);
uint64_t GetRecvBufPktNum(int workerId);

uint32_t UTILS_StatInit(void);
void UTILS_StatDeinit(void);

uint32_t UTILE_IsStatInited(void);

#ifdef __cplusplus
}
#endif
#endif
