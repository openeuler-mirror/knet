/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "securec.h"

#include "flexda_dtoe_interface.h"
#include "knet_log.h"
#include "knet_dtoe_events.h"

void KnetPollSendChannel(struct KnetSendChannel* send_channel, uint32_t maxevents)
{
    // send_channel地址即是send_channel_events地址
    flexda_dtoe_poll_send_channel((flexda_send_channel_s*)send_channel, maxevents);
}

void KnetPollRecvChannel(struct KnetRecvChannel* recv_channel, uint32_t maxevents)
{
    // recv_channel地址即是recv_channel_events地址
    flexda_dtoe_poll_receive_channel((flexda_recv_channel_s *)recv_channel, maxevents);
}