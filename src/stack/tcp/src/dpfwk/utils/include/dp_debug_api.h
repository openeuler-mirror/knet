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
/**
 * @file dp_debug_api.h
 * @brief 调试信息相关对外接口
 */

#ifndef DP_DEBUG_API_H
#define DP_DEBUG_API_H

#include "dp_debug_types_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup debug 维测信息
 * @ingroup debug
 */

/**
 * @ingroup debug
 * @brief 统计信息类型
 */
typedef enum {
    DP_STAT_TCP = 0,        /**< TCP相关统计 */
    DP_STAT_TCP_CONN,       /**< TCP连接状态统计 */
    DP_STAT_PKT,            /**< 协议栈各类报文统计 */
    DP_STAT_ABN,            /**< 协议栈异常打点统计 */
    DP_STAT_MEM,            /**< 协议栈内存使用统计 */
    DP_STAT_PBUF,           /**< 协议栈PBUF使用统计 */
    DP_STAT_MAX             /**< 统计最大枚举 */
} DP_StatType_t;

/**
 * @ingroup debug
 * @brief 维测统计信息打印到show接口钩子
 *
 * @par 描述:
 * 将维测统计信息打印到show接口钩子，用于信息查看或者问题定位
 * @attention
 *
 * @param type [IN]  统计信息类型<请参见DP_StatType_t>
 * @param workerId [IN]  需要是有效worker，如果为-1则获取所有worker的统计信息
 * @param flag [IN]  调用时的用户自定义标识，在show钩子中透传。可以用于指定输出重定向方式
 *
 * @retval NA

 * @see DP_DebugShowHook | DP_DebugShowHookReg
 */
void DP_ShowStatistics(DP_StatType_t type, int workerId, uint32_t flag);

/**
 * @ingroup debug
 * @brief socket类型类型
 */
#define DP_SOCKET_TYPE_TCP 0   /**< TCP控制块 */
#define DP_SOCKET_TYPE_UDP 1   /**< UDP控制块 */
#define DP_SOCKET_TYPE_EPOLL 2 /**< EPOLL控制块 */

/**
 * @ingroup debug
 * @brief 维测统计信息获取协议栈使用的socket数量
 *
 * @par 描述:
 * 维测统计信息获取协议栈使用的socket数量，用于信息查看或者问题定位
 * @attention
 *
 * @param type [IN]  socket类型
 *
 * @retval -1: 获取失败
 *         >= 0: 对应类型的socket数量

 */
int DP_SocketCountGet(int type);

/* 管理信息库统计项的描述信息最大长度 */
#define DP_MIB_FIELD_NAME_LEN_MAX 40

/* 协议栈统计项打点详细信息结构 */
typedef struct {
    uint8_t fieldName[DP_MIB_FIELD_NAME_LEN_MAX]; /* MIB统计项名称 */
    uint64_t fieldValue;                          /* MIB统计项打点数量 */
} DP_MibStatistic_t;

/**
 * @ingroup pub
 * 数据面处理异常统计项ID
*/
enum DP_ABN_STAT_MIB_E {
    DP_ABN_BASE,              /**< 预留异常基础统计项 */
    DP_TIMER_NODE_EXIST,      /**< 定时器已经在链表中*/
    DP_TIMER_EXPIRED_INVAL,   /**< 无效的 expired tick */
    DP_TIMER_ACTIVE_EXCEPT,   /**< 激活定时器异常 */
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
    DP_SETOPT_CA_INVAL,       /**< 设置tcp选项caMeth参数非法 */
    DP_SETOPT_BBR_CWND_INVAL,    /**< 设置tcp选项PROBERTT阶段CWND参数非法 */
    DP_SETOPT_BBR_TIMEOUT_INVAL, /**< 设置tcp选项进入PROBERTT阶段超时时间参数非法 */
    DP_SETOPT_BBR_CYCLE_INVAL,   /**< 设置tcp选项PROBERTT阶段探测时间参数非法 */
    DP_SETOPT_BBR_INCRFACTOR_INVAL, /**< 设置bbr带宽增加因子参数错误 */
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
    DP_WORKER_MISS_MATCH,     /**< 共线程部署模式下，socket/epoll被跨线程使用 */
    DP_PORT_INTERVAL_PUT_ERR, /**< 端口释放时，查找端口区间失败 */
    DP_PORT_INTERVAL_CNT_ERR, /**< 端口区间计数异常 */
    DP_TIMER_CYCLE,           /**< 定时器链表环 */
    DP_PBUF_HOOK_ALLOC_ERR,   /**< 使用hook申请内存失败 */
    DP_PBUF_BUILD_PARAM_ERR,  /**< DP_PbufBuild入参异常 */
    DP_PBUF_COPY_PARAM_ERR,   /**< DP_PbufCopy入参异常 */
    DP_NOTIFY_RCVSYN_ERR,                   /**< 被动建连收到syn 事件回调失败 */
    DP_NOTIFY_PASSIVE_CONNECTED_ERR,        /**< 被动建连成功 事件回调失败 */
    DP_NOTIFY_PASSIVE_CONNECTED_FAIL_ERR,   /**< 被动建连失败 事件回调失败 */
    DP_NOTIFY_ACTIVE_CONNECT_FAIL_ERR,      /**< 主动建连失败 事件回调失败 */
    DP_NOTIFY_RCVFIN_ERR,                   /**< 收到fin通知 事件回调失败 */
    DP_NOTIFY_RCVRST_ERR,                   /**< 收到rst通知 事件回调失败 */
    DP_NOTIFY_DISCONNECTED_ERR,             /**< 老化断链通知 事件回调失败 */
    DP_NOTIFY_WRITE_ERR,                    /**< 写 事件回调失败 */
    DP_NOTIFY_READ_ERR,                     /**< 读 事件回调失败 */
    DP_NOTIFY_FREE_SOCKCB_ERR,              /**< sock资源即将释放 事件回调失败 */
    DP_SOCKET_FD_ERR,                   /**< 创建socket时fd失败 */
    DP_FD_MEM_ERR,                      /**< 创建fd时内存申请失败 */
    DP_FD_NODE_FULL,                    /**< 创建fd时无可用node */
    DP_SOCKET_CREATE_ERR,               /**< 创建socket失败 */
    DP_SOCKET_DOMAIN_ERR,               /**< 创建socket时不支持对应domain */
    DP_SOCKET_NO_CREATEFN,              /**< 创建socket时无法找到创建钩子 */
    DP_SOCKET_TYPE_WITH_FLAGS,          /**< 创建socket时type带有flags */
    DP_SOCKET_TYPE_ERR,                 /**< 创建socket时type不支持 */
    DP_SOCKET_PROTO_INVAL,              /**< 创建socket时proto非法 */
    DP_SOCKET_NOSUPP,                   /**< 创建socket时入参组合不支持 */
    DP_TCP_CREATE_INVAL,                /**< 创建socket时proto和domain不对应 */
    DP_TCP_CREATE_FULL,                 /**< 创建socket时tcpcb已满 */
    DP_TCP_CREATE_MEM_ERR,              /**< 创建socket时tcpsk申请内存失败 */
    DP_UDP_CREATE_INVAL,                /**< 创建socket时proto和domain不对应 */
    DP_UDP_CREATE_FULL,                 /**< 创建socket时udpcb已满 */
    DP_UDP_CREATE_MEM_ERR,              /**< 创建socket时udpsk申请内存失败 */
    DP_EPOLL_CREATE_FULL,               /**< 创建epoll socket时数量已满 */
    DP_BIND_GET_SOCK_ERR,               /**< bind时获取sk失败 */
    DP_SOCK_GET_FD_ERR,                 /**< 获取fd失败 */
    DP_FD_GET_INVAL,                    /**< 因fd非法获取失败 */
    DP_FD_GET_CLOSED,                   /**< 因fd获取node已关闭而失败 */
    DP_FD_GET_INVAL_TYPE,               /**< 因获取fd时type不符而失败 */
    DP_FD_GET_REF_ERR,                  /**< 因获取fd时ref异常而失败 */
    DP_SOCK_GET_SK_NULL,                /**< 因获取sk时sk为空或ops为空而失败 */
    DP_BIND_FAILED,                     /**< bind失败 */
    DP_TCP_BIND_REPEAT,                 /**< tcp重复bind */
    DP_TCP_BIND_SHUTDOWN,               /**< tcp被shutdown后bind */
    DP_TCP_INET_BIND_FAILED,            /**< inet_bind失败导致tcp_bind失败 */
    DP_INET_BIND_ADDR_INVAL,            /**< inet检查地址非法 */
    DP_INET_ADDR_NULL,                  /**< inet检查地址为空 */
    DP_INET6_ADDR_NULL,                 /**< inet6检查地址为空 */
    DP_INET_ADDRLEN_ERR,                /**< inet检查地址长度错误 */
    DP_INET6_ADDRLEN_ERR,               /**< inet6检查地址长度错误 */
    DP_INET_ADDR_FAMILY_ERR,            /**< inet检查地址族错误 */
    DP_INET6_ADDR_FAMILY_ERR,           /**< inet6检查地址族错误 */
    DP_INET_BIND_CONNECTED,             /**< inet_bind时已经调用过connect */
    DP_INET_BIND_ADDR_ERR,              /**< inet_bind本机地址失败 */
    DP_TCP_BIND_RAND_PORT_FAILED,       /**< tcp随机绑定端口失败 */
    DP_TCP_BIND_PORT_FAILED,            /**< tcp绑定端口失败 */
    DP_CONN_GET_SOCK_ERR,               /**< bind时获取sk失败 */
    DP_CONN_ADDR_NULL,                  /**< connect地址为空 */
    DP_CONN_ADDRLEN_ERR,                /**< connect地址长度异常 */
    DP_CONN_ADDR6LEN_ERR,               /**< connect地址长度异常 */
    DP_CONN_FAILED,                     /**< connect失败 */
    DP_CONN_FLAGS_ERR,                  /**< connect时状态不正确 */
    DP_TCP_INET_CONN_FAILED,            /**< inet_connect失败导致tcp_connect失败 */
    DP_UDP_INET_CONN_FAILED,            /**< inet_connect失败导致udp_connect失败 */
    DP_INET_CONN_ADDR_INVAL,            /**< inet检查地址非法 */
    DP_UPDATE_FLOW_RT_FAILED,           /**< 更新表项查找rt表失败 */
    DP_UPDATE_FLOW_RT6_FAILED,          /**< 更新表项查找rt6表失败 */
    DP_UPDATE_FLOW_WRONG_ADDR,          /**< 表项更新前后地址异常 */
    DP_UPDATE_FLOW_WRONG_ADDR6,         /**< 表项更新前后地址异常 */
    DP_INIT_FLOW_RT_FAILED,             /**< 初始化flow查找rt表失败 */
    DP_INIT6_FLOW_RT_FAILED,            /**< 初始化flow查找rt6表失败 */
    DP_TCP_CONN_RT_NULL,                /**< flow中rt表为空 */
    DP_TCP_CONN_DEV_DOWN,               /**< connect时设备已经down */
    DP_TCP_CONN_VI_ANY,                 /**< VI设备地址为全0 */
    DP_TCP_CONN_ADDR_ERR,               /**< 路由表对应地址不正确 */
    DP_TCP_CONN_RAND_PORT_FAILED,       /**< 插入连接表时随机端口失败 */
    DP_TCP_CONN_PORT_FAILED,            /**< 插入连接表时绑定端口失败 */
    DP_NETDEV_RXHASH_FAILED,            /**< 处理netdev的rxhash失败 */
    DP_TCP_RXHASH_WID_ERR,              /**< netdev获取rxwid失败 */
    DP_UDP_CONN_SELF,                   /**< udp的flow中本端对端地址一致 */
    DP_UDP_CONN_RAND_PORT_FAILED,       /**< udp随机绑定连接端口失败 */
    DP_UDP_CONN_PORT_FAILED,            /**< udp绑定连接端口失败 */
    DP_TIMEOUT_ABORT,                   /**< 因超时导致链路断开 */
    DP_SYN_STATE_RCV_RST,               /**< 因建连期间收到rst而断链 */
    DP_CLOSE_WAIT_RCV_RST,              /**< 因close_wait状态下收到rst而断链 */
    DP_ABNORMAL_RCV_RST,                /**< 因其他状态下收到rst而断链 */
    DP_ACCEPT_GET_SOCK_ERR,             /**< accept时获取sk失败*/
    DP_ACCEPT_FD_ERR,                   /**< accept创建socket时fd失败*/
    DP_ACCEPT_CREATE_ERR,               /**< accept创建socket失败 */
    DP_ACCEPT_ADDRLEN_NULL,             /**< accept时addr不为空但addrlen为空 */
    DP_ACCEPT_ADDRLEN_INVAL,            /**< accept时addr不为空但addrlen非法 */
    DP_ACCEPT_NO_SUPPORT,               /**< sk不支持accept */
    DP_ACCEPT_CLOSED,                   /**< 因已经被close而accept失败 */
    DP_ACCEPT_NOT_LISTENED,             /**< 因没有被监听而accept失败 */
    DP_ACCEPT_GET_ADDR_FAILED,          /**< 因获取对端地址失败而accept失败 */
    DP_GET_DST_ADDRLEN_INVAL,           /**< 因获取对端地址长度非法而accept失败 */
    DP_SENDTO_GET_SOCK_ERR,             /**< sendto时获取sk失败 */
    DP_SENDTO_BUF_NULL,                 /**< sendto时buf为空 */
    DP_SEND_FLAGS_INVAL,                /**< send时flags不支持 */
    DP_SEND_GET_DATALEN_FAILED,         /**< send时获取数据长度失败 */
    DP_SEND_ZERO_DATALEN,               /**< send时发送数据长度为0 */
    DP_SOCK_CHECK_MSG_NULL,             /**< 收发检查时msg为空 */
    DP_SOCK_CHECK_MSGIOV_NULL,          /**< 收发检查时msg_iov为空 */
    DP_SOCK_CHECK_MSGIOV_INVAL,         /**< 收发检查时msg_iov非法 */
    DP_GET_IOVLEN_INVAL,                /**< 获取到iov_len异常 */
    DP_GET_IOV_BASE_NULL,               /**< 获取到iov_base为空 */
    DP_ZIOV_CB_NULL,                    /**< 零拷贝获取ziov中freeCb或cb为空 */
    DP_GET_TOTAL_IOVLEN_INVAL,          /**< 获取到iov_len总长异常 */
    DP_SOCK_SENDMSG_FAILED,             /**< send相关接口失败 */
    DP_TCP_SND_DEV_DOWN,                /**< send时dev不可用 */
    DP_TCP_SND_BUF_ZCOPY_NOMEM,         /**< 零拷贝写sndbuf失败 */
    DP_TCP_PBUF_CONSTRUCT_FAILED,       /**< 构建间接pbuf失败 */
    DP_TCP_PUSH_SND_PBUF_FAILED,        /**< tcp写sndbuf失败 */
    DP_UDP_SND_LONG,                    /**< udp发送长度超过限制 */
    DP_UDP_CHECK_DST_ADDR_ERR,          /**< udp检查发送地址失败 */
    DP_UDP_CHECK_DST_ADDR6_ERR,         /**< udp6检查发送地址失败 */
    DP_UDP_SND_ADDR_INVAL,              /**< udp目的地址与连接地址不一致 */
    DP_UDP_SND_NO_DST,                  /**< udp发送无对端信息 */
    DP_UDP_FLOW_BROADCAST,              /**< udp的flow为广播类型但不支持 */
    DP_UDP_SND_NO_RT,                   /**< udp没有路由信息且未绑定dev */
    DP_UDP_SND_DEV_DOWN,                /**< udp发送dev不可用 */
    DP_UDP_SND_FLAGS_NO_SUPPORT,        /**< udp发送时flags不支持 */
    DP_UDP_AUTO_BIND_FAILED,            /**< udp发送随机绑定端口失败 */
    DP_FROM_MSG_BUILD_PBUF_FAILED,      /**< 由msg构建pbuf时build失败 */
    DP_FROM_MSG_APPEND_PBUF_FAILED,     /**< 由msg构建pbuf时append失败 */
    DP_SENDMSG_GET_SOCK_ERR,            /**< sendmsg时获取sk失败 */
    DP_ZSENDMSG_GET_SOCK_ERR,           /**< zsendmsg时获取sk失败 */
    DP_RCVFROM_GET_SOCK_ERR,            /**< rcvfrom时获取sk失败 */
    DP_RCVFROM_FAILED,                  /**< rcvfrom失败 */
    DP_RCVFROM_BUF_NULL,                /**< rcvfrom时buf为空 */
    DP_RECV_FLAGS_INVAL,                /**< recv的flags不支持 */
    DP_RECV_GET_DATALEN_FAILED,         /**< recv时获取数据长度失败 */
    DP_RECV_CHECK_MSG_FAILED,           /**< 零拷贝检查msg失败 */
    DP_SOCK_RECVMSG_FAILED,             /**< recv相关接口失败 */
    DP_TCP_RCV_BUF_FAILED,              /**< tcp接收数据失败 */
    DP_RCV_GET_ADDR_FAILED,             /**< recv时获取目的地址失败 */
    DP_SOCK_READ_BUFCHAIN_ZRRO,         /**< 读取bufchain为0 */
    DP_SOCK_READ_BUFCHAIN_SHORT,        /**< 读取bufchain长度不足 */
    DP_RCV_ZCOPY_GET_ADDR_FAILED,       /**< 零拷贝recv时获取目的地址失败 */
    DP_RCV_ZCOPY_CHAIN_READ_FAILED,     /**< 零拷贝读取bufchain失败 */
    DP_RCVMSG_FAILED,                   /**< rcvmsg失败 */
    DP_ZRCVMSG_FAILED,                  /**< 零拷贝rcvmsg失败 */
    DP_SEND_IP_HOOK_FAILED,             /**< 产品注册钩子发送IP报文失败 */
    DP_PBUF_REF_ERR,                    /**< PBUF引用计数错误 */
    DP_INET_REASS_TIME_OUT,             /**< ipv4重组定时器超时 */
    DP_INET6_REASS_TIME_OUT,            /**< ipv6重组定时器超时 */
    DP_WORKER_GET_ERR_WID,              /**< worker获取对应wid异常 */
    DP_INIT_PBUF_MP_FAILED,             /**< pbuf内存池申请失败，未使用 */
    DP_INIT_PBUF_HOOK_REG_FAILED,       /**< pbuf内存池注册失败，未使用 */
    DP_INIT_ZCOPY_MP_FAILED,            /**< zcopy内存池申请失败，未使用 */
    DP_INIT_ZCOPY_HOOK_REG_FAILED,      /**< zcopy内存池注册失败，未使用 */
    DP_INIT_REF_MP_FAILED,              /**< ref内存池申请失败，未使用 */
    DP_INIT_REF_HOOK_REG_FAILED,        /**< ref内存池注册失败，未使用 */
    DP_CPD_DELAY_ENQUE_ERR,             /**< cpd delay转发入队异常 */
    DP_CPD_DELAY_DEQUE_ERR,             /**< cpd delay转发出队异常 */
    DP_CPD_SYNC_TABLE_RECV_ERR,         /**< cpd同步表项收内核报文异常 */
    DP_CPD_SYNC_TABLE_SEND_ERR,         /**< cpd给内核发送获取表项更新报文失败 */
    DP_CPD_SEND_ICMP_ERR,               /**< cpd发送给内核icmp报文失败 */
    DP_CPD_TRANS_MALLOC_ERR,            /**< cpd转发报文内存申请失败 */
    DP_CPD_FIND_TAP_FAILED,             /**< cpd 获取tap口异常 */
    DP_CPD_FD_WRITE_FAILED,             /**< cpd 转发报文write失败 */
    DP_CPD_FD_WRITEV_FAILED,            /**< cpd 转发报文writev失败 */
    DP_CPD_FD_READ_FAILED,              /**< cpd 同步内核报文read失败 */
    DP_UTILS_TIMER_ERR,                 /**< 基础时钟返回值异常 */
    DP_PBUF_WID_ERR,                    /**< PBUF wid与worker id不一致 */
    DP_ABN_STAT_MAX,
};

/* TCP统计项枚举 */
enum DP_TCP_STAT_MIB_E {
    DP_TCP_ACCEPTS,                    /**< 被动建链的连接数 */
    DP_TCP_CLOSED,                     /**< 关闭的连接数 (包括丢弃的连接) */
    DP_TCP_CONN_ATTEMPT,               /**< 试图建立连接的次数(调用connect) */
    DP_TCP_CONNECTS,                   /**< 建链成功的次数 */
    DP_TCP_DELAYED_ACK,                /**< 延迟发送的ACK数 */
    DP_TCP_DROPS,                      /**< 意外丢失的连接数(收到SYN之后) */
    DP_TCP_KEEP_DROPS,                 /**< 在保活阶段丢失的连接数(已建立或正等待SYN) */
    DP_TCP_KEEP_PROBE,                 /**< 保活探测探测报文发送次数 */
    DP_TCP_KEEP_TIME_OUT,              /**< 保活定时器或者连接建立定时器超时次数 */
    DP_TCP_PERSIST_DROPS,              /**< 持续定时器超时次数达到最大值的次数 */
    DP_TCP_PERSIST_TIMEOUT,            /**< 持续定时器超时次数 */
    DP_TCP_RCV_ACK_BYTE,               /**< 由收到的ACK报文确认的发送字节数 */
    DP_TCP_RCV_ACK_PACKET,             /**< 收到的正常ACK报文数 */
    DP_TCP_RCV_ACK_TOO_MUCH,           /**< 收到对未发送数据进行的ACK报文数 */
    DP_TCP_RCV_DUP_ACK,                /**< 收到的重复ACK数 */
    DP_TCP_RCV_BAD_OFF,                /**< 因首部长度无效丢弃的报文数 */
    DP_TCP_RCV_BAD_SUM,                /**< 因校验和错误丢弃的报文数 */
    DP_TCP_RCV_LOCAL_ADDR,             /**< 因地址为本机地址而丢弃的报文数 */
    DP_TCP_RCV_WITHOUT_LISTENER,       /**< 因找不到tcp而丢弃的报文数 */
    DP_TCP_RCV_ERR_ACKFLAG,            /**< 因异常ackFlag而丢弃的报文数 */
    DP_TCP_LISTEN_RCV_NOTSYN,          /**< 因在listen状态收到非syn而丢弃的报文数 */
    DP_TCP_LISTEN_RCV_INVALID_WID,     /**< 因共线程收到的异常wid报文而丢弃的报文数 */
    DP_TCP_RCV_DUP_SYN,                /**< 因收到重复syn而丢弃的报文数 */
    DP_TCP_SYNRCV_NONACK,              /**< 因syn_rcv下收到不带ack而丢弃的报文数 */
    DP_TCP_RCV_INVALID_ACK,            /**< 因syn_rcv下收到异常序号ack而丢弃的报文数 */
    DP_TCP_RCV_ERR_SEQ,                /**< 因syn_rcv下收到异常seq序号而丢弃的报文数量 */
    DP_TCP_RCV_ERR_OPT,                /**< 因收到异常tcpOpt的报文数量 */
    DP_TCP_RCV_DATA_SYN,               /**< 因syn报文带数据而丢弃的报文数量 */
    DP_TCP_RCV_BYTE,                   /**< 连续收到的字节数 */
    DP_TCP_PASSIVE_RCV_BYTE,           /**< 流量统计：被动建链收到的总字节数 */
    DP_TCP_ACTIVE_RCV_BYTE,            /**< 流量统计：主动建链收到的总字节数 */
    DP_TCP_RCV_DUP_BYTE,               /**< 完全重复报文中的重复字节数 */
    DP_TCP_RCV_DUP_PACKET,             /**< 完全重复报文的报文数 */
    DP_TCP_RCV_PACKET_AFTER_WND,       /**< 携带数据超出滑动窗口通告值的报文数 */
    DP_TCP_RCV_BYTE_AFTER_WND,         /**< 收到的超窗数据报文中超窗字节数 */
    DP_TCP_RCV_PART_DUP_BYTE,          /**< 部分数据重复的报文重复字节数 */
    DP_TCP_RCV_PART_DUP_PACKET,        /**< 部分数据重复的报文数 */
    DP_TCP_RCV_OUT_ORDER_PACKET,       /**< 收到失序的报文数 */
    DP_TCP_RCV_OUT_ORDER_BYTE,         /**< 收到失序的字节数 */
    DP_TCP_RCV_SHORT,                  /**< 因为长度过短丢弃的的报文数 */
    DP_TCP_RCV_TOTAL,                  /**< 收到的报文总数 */
    DP_TCP_RCV_PACKET,                 /**< 顺序接收的报文数 */
    DP_TCP_RCV_WND_PROBE,              /**< 收到的窗口探测报文数 */
    DP_TCP_RCV_WND_UPDATE,             /**< 收到的窗口更新报文数 */
    DP_TCP_RCV_INVALID_RST,            /**< 收到的无效的RST报文 */
    DP_TCP_RCV_SYN_ESTABLISHED,        /**< 建链完成后收到序号合法的SYN报文 */
    DP_TCP_RCV_FIN,                    /**< 收到非重传的FIN报文个数 */
    DP_TCP_RCV_RXMT_FIN,               /**< 收到重传FIN报文个数 */
    DP_TCP_REXMT_TIMEOUT,              /**< 重传超时次数 */
    DP_TCP_RTT_UPDATED,                /**< RTT估算值更新次数 */
    DP_TCP_SND_BYTE,                   /**< 发送的字节数 */
    DP_TCP_PASSIVE_SND_BYTE,           /**< 流量统计：被动建链发送的总字节数 */
    DP_TCP_ACTIVE_SND_BYTE,            /**< 流量统计：主动建链发送的总字节数 */
    DP_TCP_SND_CONTROL,                /**< 发送的控制报文数(SYN FIN RST) */
    DP_TCP_SND_PACKET,                 /**< 发送的数据报文数(数据长度大于0) */
    DP_TCP_SND_PROBE,                  /**< 发送的窗口探测次数 */
    DP_TCP_SND_REXMT_BYTE,             /**< 重传的数据字节数 */
    DP_TCP_SND_ACKS,                   /**< 发送的纯ACK报文数(数据长度为0) */
    DP_TCP_SND_REXMT_PACKET,           /**< 重传的报文数 */
    DP_TCP_SND_TOTAL,                  /**< 发送的报文总数 */
    DP_TCP_SND_WND_UPDATE,             /**< 发送的只携带窗口更新信息的报文数 */
    DP_TCP_TIMEOUT_DROP,               /**< 由于重传超时而丢失的连接数 */
    DP_TCP_RCV_EXD_WND_RST,            /**< 收到超窗reset报文 */
    DP_TCP_DROP_CONTROL_PKTS,          /**< 丢弃的控制报文 */
    DP_TCP_DROP_DATA_PKTS,             /**< 丢弃的数据报文 */
    DP_TCP_SND_RST,                    /**< 发送的RST报文数 */
    DP_TCP_SND_FIN,                    /**< 发送的FIN报文数 */
    DP_TCP_FIN_WAIT_2_DROPS,           /**< fin_wait定时器超时断链次数 */
    DP_TCP_RESPONSE_CHALLENGE_ACKS,    /**< 回复挑战ACK个数 */
    DP_TCP_ONCE_DRIVE_PASSIVE_TSQ,     /**< 单次被动调度tsq循环次数 */
    DP_TCP_AGE_DROPS,                  /**< 由于老化而丢弃的连接数 */
    DP_TCP_BBR_SAMPLE_CNT,             /**< bbr采样次数 */
    DP_TCP_BBR_SLOWBW_CNT,             /**< bbr触发slowBW次数 */
    DP_TCP_FRTO_SPURIOS_CNT,           /**< FRTO 判断假超时次数 */
    DP_TCP_FRTO_REAL_CNT,              /**< FRTO 判断真超时次数 */
    DP_TCP_TIMEWAIT_REUSE,             /**< TIMEWAIT状态复用计数 */
    DP_TCP_SYNSNT_CONN_DROPS,          /**< syn_sent时收到rst断链数 */
    DP_TCP_SYNRCV_CONN_DROPS,          /**< syn_recv时收到rst断链数 */
    DP_TCP_USER_TIMEOUT_DROPS,         /**< user_timeout超时断链数 */
    DP_TCP_SYN_RETRIES_DROPS,          /**< synRetries超过导致断链数(包括syn、syn/ack) */
    DP_TCP_SYNACK_RETRIES_DROPS,       /**< SYNACK_RETRIES超过导致断链数 */
    DP_TCP_RCV_OLD_ACK,                /**< 收到的老旧ack数 */
    DP_TCP_RCV_ERR_SYN_OPT,            /**< 收到的syn报文选项异常的数量 */
    DP_TCP_RCV_ERR_SYNACK_OPT,         /**< 收到的syn/ack报文选项异常的数量 */
    DP_TCP_RCV_ERR_ESTABLISH_ACK_OPT,  /**< 收到的建连ack报文选项异常的数量 */
    DP_TCP_SND_REXMIT_SYN,             /**< 重传的syn报文数量 */
    DP_TCP_SND_REXMIT_SYNACK,          /**< 重传的syn/ack报文数量 */
    DP_TCP_SND_REXMIT_FIN,             /**< 重传的fin报文数量 */
    DP_TCP_TIME_WAIT_DROPS,            /**< 2msl超时断链的连接数量 */
    DP_TCP_XMIT_GET_DEV_NULL,          /**< 发送报文获取dev为空的次数 */
    DP_TCP_USER_ACCEPT,                /**< 被用户accept拿走的socket数 */
    DP_TCP_RCV_ERR_SACKOPT,            /**< 收到的带异常sack选项的报文数量 */
    DP_TCP_RCV_DATA_AFTER_FIN,         /**< 收到的FIN之后收到的数据报文数量 */
    DP_TCP_RCV_AFTER_CLOSED,           /**< Tcp状态为CLOSED之后收到数据报文数量 */
    DP_TCP_CHECK_DEFFER_ACCEPT_ERR,    /**< Tcp校验DefferAccept失败拒绝连接的数量 */
    DP_TCP_OVER_BACKLOG,               /**< 因达到Server的backlog限制丢弃的连接数量 */
    DP_TCP_PAEST_OVER_MAXCB,           /**< Server端因最大TCPCB数量限制丢弃的连接数量发送RST */
    DP_TCP_SND_SYN,                    /**< 发送的SYN报文数 */
    DP_TCP_CORK_LIMIT,                 /**< 因Cork限制未发送的报文数 */
    DP_TCP_MSGMORE_LIMIT,              /**< 因MsgMore限制未发送的报文数 */
    DP_TCP_NAGLE_LIMIT,                /**< 因Nagle限制未发送的报文数 */
    DP_TCP_BBR_PACING_LIMIT,           /**< 因BBR算法 PACING限制未发送的报文数 */
    DP_TCP_PACING_LIMIT,               /**< 因PACING限制未发送的报文数 */
    DP_TCP_CWND_LIMIT,                 /**< 因拥塞窗口限制未发送的报文数 */
    DP_TCP_SWND_LIMIT,                 /**< 因对端窗口限制未发送的报文数 */
    DP_TCP_DROP_REASS_BYTE,            /**< 丢弃的重组报文字节数 */
    DP_TCP_SND_SACK_REXMT_PACKET,      /**< SACK重传的报文数 */
    DP_TCP_SND_FAST_REXMT_PACKET,      /**< 快速重传的报文数 */
    DP_TCP_PERSIST_USER_TIMEOUT_DROPS, /**< 坚持定时器超过用户配置时间丢弃的链接数 */
    DP_TCP_SYN_SENT_RCV_ERR_ACK,       /**< SYNSENT状态下接受到报文ACK异常发送RST */
    DP_TCP_COOKIE_AFTER_CLOSED,        /**< 已经被关闭的socket处理cookie异常发送RST */
    DP_TCP_PARENT_CLOSED,              /**< 父socket被关闭异常发送RST */
    DP_TCP_RCV_NON_RST,                /**< 没有五元组状态下接受到不包含RST的报文发送RST */
    DP_TCP_CHILD_CLOSE,                /**< 子socket关闭时发送RST */
    DP_TCP_LINGER_CLOSE,               /**< linger模式关闭时发送RST */
    DP_TCP_RCVDATA_AFTER_CLOSE,        /**< 在关闭socket后接受到数据报文，发送RST报文 */
    DP_TCP_REXMIT_RST,                 /**< 重传RST报文 */
    DP_TCP_RCVBUF_NOT_CLEAN,           /**< RCVBUF有报文时close发送RST报文 */
    DP_TCP_SYNSENT_RCV_INVALID_RST,    /**< SYN SENT状态下接受到无效RST报文 */
    DP_TCP_CONN_KEEP_DROPS,            /**< 在建链阶段因保活丢失的连接数 */
    DP_TCP_SYN_SENT_RCV_NO_SYN,        /**< SYNSENT状态下接受到报文不带SYN标志 */
    DP_TCP_REASS_SUCC_BYTE,            /**< TCP重组完成的报文字节数 */
    DP_TCP_RCV_OUT_BYTE,               /**< TCP用户接收走的字节数 */
    DP_TCP_ICMP_TOOBIG_SHORT,          /**< Tcp层处理Icmp TOO BIG时报文长度不足 */
    DP_TCP_ICMP_TOOBIG_NO_TCP,         /**< Tcp层处理Icmp TOO BIG时找不到对应tcp */
    DP_TCP_ICMP_TOOBIG_ERR_SEQ,        /**< Tcp层处理Icmp TOO BIG时不符合tcp序号 */
    DP_TCP_ICMP_TOOBIG_LARGE_MSS,      /**< Tcp层处理Icmp TOO BIG时传入mtu计算的mss比当前大 */
    DP_TCP_ICMP_TOOBIG_ERR_STATE,      /**< Tcp层处理Icmp TOO BIG时tcp状态不正确 */
    DP_TCP_CLOSE_NORCV_DATALEN,        /**< Tcp层处理close时候，接收缓冲区数据长度 */
    DP_TCP_PKT_WITHOUT_ACK,            /**< 建链后接收到不带ACK的报文 */
    DP_TCP_SYN_RECV_DUP_PACKET,        /**< SYN RECV状态下收到重复的报文 */
    DP_TCP_SYN_RECV_UNEXPECT_SYN,      /**< SYN RECV状态下收到非预期的SYN报文 */
    DP_TCP_RCV_ACK_ERR_COOKIE,         /**< syn cookie场景收到ack校验cookie值异常 */
    DP_TCP_COOKIE_CREATE_FAILED,       /**< syn cookie场景创建socket失败 */
    DP_TCP_RCV_RST_IN_RFC1337,         /**< TIME WAIT状态下，使能RFC1337配置项收到的RST报文 */
    DP_TCP_DEFER_ACCEPT_DROP,          /**< 因重传超过 defer accept 设置的重传次数丢弃的报文  */
    DP_TCP_STAT_MAX
};

/* 协议栈报文统计项枚举 */
enum DP_PKT_STAT_MIB_E {
    DP_PKT_LINK_IN,                        /**< 接收入口统计 */
    DP_PKT_ETH_IN,                         /**< eth接收入口统计 */
    DP_PKT_NET_IN,                         /**< net接收入口统计 */
    DP_PKT_ICMP_FORWARD,                   /**< 转发给内核的ICMP报文统计 */
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
    DP_PKT_ICMP_IN,                        /**< 收到ICMP报文的数量 */
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
    DP_PKT_IP6_IN,                         /**< ip6接收入口统计 */
    DP_PKT_IP6_TOO_SHORT,                  /**< 具有无效数据长度的ip6报文统计 */
    DP_PKT_IP6_BAD_VER,                    /**< IP6版本号错误的报文统计 */
    DP_PKT_IP6_BAD_HEAD_LEN,               /**< IP6首部长度无效的报文统计 */
    DP_PKT_IP6_BAD_LEN,                    /**< IP6首部和数据长度不一致的报文统计 */
    DP_PKT_IP6_MUTICAST_DELIVER,           /**< IP6组播报文上送统计 */
    DP_PKT_IP6_EXT_HDR_CNT_ERR,            /**< IP6扩展首部数量异常的报文统计 */
    DP_PKT_IP6_EXT_HDR_OVERFLOW,           /**< IP6扩展首部长度异常的报文统计 */
    DP_PKT_IP6_EXT_HDR_HBH_ERR,            /**< IP6 Hop-by-Hop扩展首部异常的报文统计 */
    DP_PKT_IP6_NO_UPPER_PROTO,             /**< IP6报文不携带上层协议数据的报文统计 */
    DP_PKT_IP6_REASS_IN_FRAG,              /**< 进行IP6真重组的分片统计 */
    DP_PKT_IP6_EXT_HDR_FRAG_ERR,           /**< IP6分片扩展首部异常的报文统计 */
    DP_PKT_IP6_OUT,                        /**< ip6发送出口报文统计 */
    DP_PKT_IP6_FRAG_OUT,                   /**< ip6分片发送出口报文统计 */
    DP_PKT_KERNEL_FDIR_CACHE_MISS,         /**< netdev使用缓存miss统计 */
    DP_PKT_NET_DEV_TYPE_NOT_MATCH,         /**< netdev类型不匹配丢弃报文 */
    DP_PKT_NET_CHECK_ADDR_FAIL,            /**< IPV4校验地址失败 */
    DP_PKT_IP_LEN_OVER_LIMIT,              /**< 报文长度超过最大值 */
    DP_PKT_NET_REASS_OVER_TBL_LIMIT,       /**< 重组节点超过表最大值 */
    DP_PKT_NET_REASS_MALLOC_FAIL,          /**< 重组节点内存申请失败 */
    DP_PKT_NET_REASS_NODE_OVER_LIMIT,      /**< 重组节点报文数量达到限制 */
    DP_PKT_ICMP_ADDR_NOT_MATCH,            /**< ICMP地址不匹配 */
    DP_PKT_ICMP_PKT_LEN_SHORT,             /**< ICMP报文长度过小 */
    DP_PKT_ICMP_PKT_BAD_SUM,               /**< ICMP校验和错误 */
    DP_PKT_ICMP_NOT_PORT_UNREACH,          /**< ICMP报文非PORT_UNREACH */
    DP_PKT_ICMP_UNREACH_TOO_SHORT,         /**< PORT_UNREACH报文长度过小 */
    DP_PKT_ICMP_UNREACH_TYPE_ERR,          /**< UNREACH上层协议非UDP */
    DP_PKT_IP6_DEV_TYPE_NOT_MATCH,         /**< IPV6设备类型不匹配 */
    DP_PKT_IP6_CHECK_ADDR_FAIL,            /**< IPV6 地址校验错误 */
    DP_PKT_IP6_REASS_OVER_TBL_LIMIT,       /**< IPV6重组节点超过表限制 */
    DP_PKT_IP6_REASS_MALLOC_FAIL,          /**< IPV6重组节点内存申请失败 */
    DP_PKT_IP6_REASS_NODE_OVER_LIMIT,      /**< IPV6重组节点报文超限 */
    DP_PKT_IP6_PROTO_ERR,                  /**< IPV6找不到上层协议 */
    DP_PKT_IP6_ICMP_TOO_SHORT,             /**< IPV6 ICMP长度过小 */
    DP_PKT_IP6_ICMP_BAD_SUM,               /**< IPV6 ICMP校验和错误 */
    DP_PKT_IP6_ICMP_NO_PAYLOAD,            /**< IPV6 ICMP没有Payload */
    DP_PKT_IP6_ICMP_CODE_NOMATCH,          /**< IPV6 ICMP类型不匹配 */
    DP_PKT_ICMP6_TOOBIG_SHORT,             /**< ICMPv6 TOO BIG报文ip层长度过短 */
    DP_PKT_ICMP6_TOOBIG_SMALL,             /**< ICMPv6 TOO BIG报文mtu信息过小 */
    DP_PKT_ICMP6_TOOBIG_EXTHDR_ERR,        /**< ICMPv6 TOO BIG报文内部扩展首部异常 */
    DP_PKT_ICMP6_TOOBIG_NOT_TCP,           /**< ICMPv6 TOO BIG报文内部四层非TCP */
    DP_PKT_IP_BAD_OFFSET,                  /**< IP报文offset错误 */
    DP_PKT_NF_PREROUTING_DROP,             /**< nf PreRouting 丢弃报文*/
    DP_PKT_NF_LOCALIN_DROP,                /**< nf LocalIn 丢弃报文 */
    DP_PKT_NF_FORWARD_DROP,                /**< nf Forward 丢弃报文 */
    DP_PKT_NF_LOCALOUT_DROP,               /**< nf LocalOut 丢弃报文 */
    DP_PKT_NF_POSTROUTING_DROP,            /**< nf PostRouting 丢弃报文 */
    DP_UDP_ICMP_UNREACH_SHORT,             /**< UNREACH报文udp层长度过短 */
    DP_PKT_ICMP6_UNREACH_SHORT,            /**< ICMPv6 UNREACHip层过短 */
    DP_PKT_ICMP6_UNREACH_EXTHDR_ERR,       /**< ICMPv6 UNREACH报文内部扩展首部异常 */
    DP_PKT_ICMP6_UNREACH_NOT_UDP,          /**< ICMPv6 UNREACH报文内部四层非UDP */
    DP_UDP_ICMP6_UNREACH_SHORT,            /**< ICMPv6 UNREACH报文长度过短 */
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

/**
 * @ingroup debug
 * @brief 维测统计信息获取指定类型统计信息
 *
 * @par 描述:
 * 维测统计信息获取指定类型统计信息，用于信息查看或者问题定位
 * @attention
 *
 * @param type [IN]  统计信息类型<请参见DP_StatType_t>   \n
 *                   DP_STAT_TCP 对应枚举 DP_TCP_STAT_MIB_E \n
 *                   DP_STAT_TCP_CONN 对应枚举 DP_TCP_CONN_STAT_MIB_E \n
 *                   DP_STAT_PKT 对应枚举 DP_PKT_STAT_MIB_E \n
 *                   DP_STAT_ABN 对应枚举 DP_ABN_STAT_MIB_E \n
 *                   其余不支持
 * @param fieldId [IN] type对应枚举中序号
 * @param showStat [IN/OUT] 非空指针,统计信息
 *
 * @retval 0 成功， -1失败

 */
int DP_GetMibStat(DP_StatType_t type, uint32_t fieldId, DP_MibStatistic_t *showStat);

/**
 * @ingroup debug
 * @brief 获取socket详细信息
 *
 * @par 描述:
 * 获取socket详细信息，用于信息打印或问题定位
 * @attention
 *
 * @param fd [IN] Socket描述符，只支持DP协议栈创建的socket描述符
 * @param details [OUT] 详细信息
 *
 * @retval NA

 * @see DP_Socket | DP_PosixSocket
 */
int DP_GetSocketDetails(int fd, DP_SockDetails_t* details);

/**
 * @ingroup debug
 * @brief 打印socket节点信息
 *
 * @par 描述:
 * 打印socket节点信息，用于信息查看或者问题定位
 * @attention
 *
 * @param fd [IN] Socket描述符，只支持DP协议栈创建的socket描述符
 *
 * @retval NA

 * @see DP_Socket | DP_PosixSocket
 */
void DP_ShowSocketInfoByFd(int fd);

#define DP_SOCKET_STATE_CLOSED 0
#define DP_SOCKET_STATE_LISTEN 1
#define DP_SOCKET_STATE_SYN_SENT 2
#define DP_SOCKET_STATE_SYN_RECV 3
#define DP_SOCKET_STATE_ESTABLISHED 4
#define DP_SOCKET_STATE_CLOSE_WAIT 5
#define DP_SOCKET_STATE_FIN_WAIT1 6
#define DP_SOCKET_STATE_CLOSING 7
#define DP_SOCKET_STATE_LAST_ACK 8
#define DP_SOCKET_STATE_FIN_WAIT2 9
#define DP_SOCKET_STATE_TIME_WAIT 10
#define DP_SOCKET_STATE_INVALID 11

/**
 * @ingroup debug
 * @brief 维测获取socket的连接状态
 *
 * @par 描述:
 * 维测获取socket的连接状态，用于信息查看或者问题定位。暂时只支持IP4类型socket
 * @attention
 *
 * @param fd [IN] Socket描述符，只支持DP协议栈创建的socket描述符
 * @param state [OUT] Socket连接信息结构体指针
 *
 * @retval -1: 获取失败
 *          0: 获取成功

 * @see DP_Socket | DP_PosixSocket
 */
int DP_GetSocketState(int fd, DP_SocketState_t* state);

/**
 * @ingroup debug
 * @brief 维测获取epoll的详细信息
 *
 * @par 描述:
 * 维测获取epoll的详细信息，用于信息查看或者问题定位
 * @attention
 * details的内存由用户分配管理，当len小于epoll监听的socket数量时，只会填写len个成员。
 * epoll可以监听的socket数量无上限，这里约定获取到的监听数量最大值为INT_MAX，即监听socket数量超过INT_MAX时只返回INT_MAX。
 *
 * @param epFd [IN] epoll描述符，只支持DP协议栈创建的epoll描述符
 * @param details [IN/OUT] epoll详细信息结构体指针
 * @param len [IN] details的数组长度，用户保证有效性
 * @param wid [OUT] epoll所属worker id，共线程模式有意义，非共线程模式均为-1
 *
 * @retval -1: 获取失败
 *         >=0: 获取成功，返回值为epoll中监听的socket数量

 * @see DP_PosixEpollCreate | DP_EpollCreateNotify
 */
int DP_GetEpollDetails(int epFd, DP_EpollDetails_t* details, int len, int *wid);

#ifdef __cplusplus
}
#endif
#endif
