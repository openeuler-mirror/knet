/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: io操作相关选项定义
 */

#ifndef DP_IOCTL_DEFS_API_H
#define DP_IOCTL_DEFS_API_H

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup ioctl io操作命令
 * @ingroup base
 */

/**
 * @ingroup ioctl
 * get iface name
 */
#define DP_SIOCGIFNAME        0x8910

/**
 * @ingroup ioctl
 * set iface channel
 */
#define DP_SIOCSIFLINK        0x8911

/**
 * @ingroup ioctl
 * get iface list
 */
#define DP_SIOCGIFCONF        0x8912

/**
 * @ingroup ioctl
 * get flags
 */
#define DP_SIOCGIFFLAGS       0x8913

/**
 * @ingroup ioctl
 * set flags
 */
#define DP_SIOCSIFFLAGS       0x8914

/**
 * @ingroup ioctl
 * get PA address
 */
#define DP_SIOCGIFADDR        0x8915

/**
 * @ingroup ioctl
 * set PA address
 */
#define DP_SIOCSIFADDR        0x8916

/**
 * @ingroup ioctl
 * get remote PA address
 */
#define DP_SIOCGIFDSTADDR     0x8917

/**
 * @ingroup ioctl
 * set remote PA address
 */
#define DP_SIOCSIFDSTADDR     0x8918

/**
 * @ingroup ioctl
 * get broadcast PA address
 */
#define DP_SIOCGIFBRDADDR     0x8919

/**
 * @ingroup ioctl
 * set broadcast PA address
 */
#define DP_SIOCSIFBRDADDR     0x891a

/**
 * @ingroup ioctl
 * get network PA mask
 */
#define DP_SIOCGIFNETMASK     0x891b

/**
 * @ingroup ioctl
 * set network PA mask
 */
#define DP_SIOCSIFNETMASK     0x891c

/**
 * @ingroup ioctl
 * get metric
 */
#define DP_SIOCGIFMETRIC      0x891d

/**
 * @ingroup ioctl
 * set metric
 */
#define DP_SIOCSIFMETRIC      0x891e

/**
 * @ingroup ioctl
 * get memory address (BSD)
 */
#define DP_SIOCGIFMEM         0x891f

/**
 * @ingroup ioctl
 * set memory address (BSD)
 */
#define DP_SIOCSIFMEM         0x8920

/**
 * @ingroup ioctl
 * get MTU size
 */
#define DP_SIOCGIFMTU         0x8921

/**
 * @ingroup ioctl
 * set MTU size
 */
#define DP_SIOCSIFMTU         0x8922

/**
 * @ingroup ioctl
 * set interface name
 */
#define DP_SIOCSIFNAME        0x8923

/**
 * @ingroup ioctl
 * set hardware address
 */
#define DP_SIOCSIFHWADDR      0x8924

/**
 * @ingroup ioctl
 * get encapsulations
 */
#define DP_SIOCGIFENCAP       0x8925

/**
 * @ingroup ioctl
 * set encapsulations
 */
#define DP_SIOCSIFENCAP       0x8926

/**
 * @ingroup ioctl
 * Get hardware address
 */
#define DP_SIOCGIFHWADDR      0x8927

/**
 * @ingroup ioctl
 * Driver slaving support
 */
#define DP_SIOCGIFSLAVE       0x8929

/**
 * @ingroup ioctl
 * set if slave
 */
#define DP_SIOCSIFSLAVE       0x8930

/**
 * @ingroup ioctl
 * add multicast address lists
 */
#define DP_SIOCADDMULTI       0x8931

/**
 * @ingroup ioctl
 * delete multicast address lists
 */
#define DP_SIOCDELMULTI       0x8932

/**
 * @ingroup ioctl
 * name -> if_index mapping
 */
#define DP_SIOCGIFINDEX       0x8933

/**
 * @ingroup ioctl
 * misprint compatibility :-)
 */
#define DP_SIOGIFINDEX        SIOCGIFINDEX

/**
 * @ingroup ioctl
 * set extended flags set
 */
#define DP_SIOCSIFPFLAGS      0x8934

/**
 * @ingroup ioctl
 * get extended flags set
 */
#define DP_SIOCGIFPFLAGS      0x8935

/**
 * @ingroup ioctl
 * delete PA address
 */
#define DP_SIOCDIFADDR        0x8936

/**
 * @ingroup ioctl
 * set hardware broadcast addr
 */
#define DP_SIOCSIFHWBROADCAST 0x8937

/**
 * @ingroup ioctl
 * get number of devices
 */
#define DP_SIOCGIFCOUNT       0x8938

/**
 * @ingroup ioctl
 * Get device parameters
 */
#define DP_SIOCGIFMAP         0x8970

/**
 * @ingroup ioctl
 * Set device parameters
 */
#define DP_SIOCSIFMAP         0x8971

/**
 * @ingroup ioctl
 * get if vlan
 */
#define DP_SIOCGIFVLAN        0x8982

/**
 * @ingroup ioctl
 * set if vlan
 */
#define DP_SIOCSIFVLAN        0x8983

#ifdef __cplusplus
}
#endif
#endif
