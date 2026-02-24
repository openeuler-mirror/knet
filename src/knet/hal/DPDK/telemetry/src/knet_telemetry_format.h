/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 持久化format头文件
 */
#ifndef __KNET_TELEMETRY_FORMAT_H__
#define __KNET_TELEMETRY_FORMAT_H__

#ifdef __cplusplus
}
#endif

#define FORMAT_INT_WIDTH 21 // uint64_t 最大20位
#define FORMAT_SUCCESS(ret, offset) ((ret) < 0 ? (offset) : (offset) + (ret))

#define FORMAT_FUNC_SUCCESS(function, offset)                                                                          \
    do {                                                                                                               \
        int __ret = (function);                                                                                        \
        (offset) = __ret < 0 ? __ret : (offset) + __ret;                                                               \
    } while (0)

int TelemetryPersistMzInit(void);
int FormatingInCustom(char *output, int *outputLeftLen, const char *fmt, ...);
int FormatingSingleDpStats(char *output, int *outputLeftLen, cJSON *json);
int FormatEveryDpStats(char *output, int *outputLeftLen);
int FormatXstatsDataByPortId(char *output, int *outputLeftLen, uint16_t portID);
int NotifySubprocessRefreshDpState(pid_t pid);
int TelemetryPersistInitDpJson(void);
void TelemetryPersistUninitDpJson(void);
int TelemetryPersistInitGetDpStatFunc(int32_t runMode);

#ifdef __cplusplus
}
#endif
#endif /* __KNET_TELEMETRY_FORMAT_H__ */
