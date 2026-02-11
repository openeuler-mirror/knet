#!/bin/bash

# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
# K-NET is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#     http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

KNET_CONF_FILE="/etc/knet/knet_comm.conf"
KNET_COMM_LOG_PATH="/var/log/knet"
KNET_TELEMETRY_STATS_PATH="/etc/knet/run/stats"
knet_package=knet
sp600_package=dpdk-sp600-pmd
error=0

print_help() {
    echo         "Usage: sh collect.sh"
}

collect_hw_info(){
    local path=$1
    touch ${path}/hw_info.txt
    chmod 600 ${path}/hw_info.txt

    echo "CPU:" >  ${path}/hw_info.txt
    cpu_model="$(lscpu |grep "Model name")"
    echo "$cpu_model" >>  ${path}/hw_info.txt

    cpu_MHz="$(lscpu| grep MHz)"
    echo "$cpu_MHz" >>  ${path}/hw_info.txt

    cpu_cache="$(lscpu| grep cache)"
    echo "$cpu_cache" >>  ${path}/hw_info.txt

    nic_info="null"
    if [ -e "$KNET_CONF_FILE" ]; then
        if ! jq_output=$(jq '.' "$KNET_CONF_FILE" 2>&1); then
            echo "$jq_output"
            echo "Get nic info failed, please check the format of knet_comm.conf."
            error=1
        fi
        if [ ${error} -eq 0 ]; then
            bdf_value1=$(jq -r '.interface.bdf_nums[0] // ""' "$KNET_CONF_FILE" 2>/dev/null)
            if [ -n "$bdf_value1" ]; then
                nic_bdf1=$(echo $bdf_value1 | cut -d':' -f2-)
                nic_info=$(lspci |grep "$nic_bdf1")
            fi
            bdf_value2=$(jq -r '.interface.bdf_nums[1] // ""' "$KNET_CONF_FILE" 2>/dev/null)
            if [ -n "$bdf_value2" ]; then
                nic_bdf2=$(echo $bdf_value2 | cut -d':' -f2-)
                nic_info="$nic_info"$'\n'"$(lspci |grep "$nic_bdf2")"
            fi
        fi
    fi
    echo "Nic:" >>  ${path}/hw_info.txt
    echo "$nic_info" >>  ${path}/hw_info.txt

    echo "Memory:" >>  ${path}/hw_info.txt
    mem_size="$(dmidecode --type 17 | grep -w Size|grep -v "No Module Installed")"
    echo "$mem_size" >>  ${path}/hw_info.txt

    mem_speed="$(dmidecode --type 17 | grep -w Speed|grep -v Unknown)"
    echo "$mem_speed" >>  ${path}/hw_info.txt

    mem_info="$(free -h)"
    echo "$mem_info" >>  ${path}/hw_info.txt
}

collect_sw_info(){
    local path=$1
    touch ${path}/sw_info.txt
    chmod 600 ${path}/sw_info.txt

    knet_version="$(rpm -qi $knet_package| grep Version| awk '{print $3}')"
    echo "K-NET Version : $knet_version" >  ${path}/sw_info.txt

    dpdk_version_rpm="$(rpm -qi dpdk| grep Version| awk '{print $3}')"
    echo "DPDK Version(rpm) : $dpdk_version_rpm" >>  ${path}/sw_info.txt

    if [ -L "/usr/lib64/libknet_frame.so" ]; then
        rte_line=$(ldd /usr/lib64/libknet_frame.so 2>&1 | grep "librte_eal.so")
        if [ -n "$rte_line" ]; then
            # 检查dpdk so是否不存在
            if echo "$rte_line" | grep -q "not found"; then
                echo "DPDK Version(so) : librte_eal.so not found" >> ${path}/sw_info.txt
            else
                rte_eal_so="$(ldd /usr/lib64/libknet_frame.so 2>&1|grep "librte_eal.so"|awk '{print $3}')"
                dpdk_version_so="$(basename "$rte_eal_so" | awk -F '.' '{print $NF}')"
                echo "DPDK Version(so) : $dpdk_version_so" >>  ${path}/sw_info.txt
            fi
        fi
    fi
    
    sp600_version="$(rpm -qi $sp600_package| grep Version| awk '{print $3}')"
    echo "SP600 driver Version : $sp600_version" >>  ${path}/sw_info.txt

    sp600_release="$(rpm -qi $sp600_package| grep Release| awk '{print $3}')"
    echo "SP600 driver Release : $sp600_release" >>  ${path}/sw_info.txt

    glibc_version="$(ldd --version |head -n 1| awk '{print $4}')"
    echo "GLIBC : $glibc_version" >>  ${path}/sw_info.txt

    kernel_version="$(uname -r)"
    echo "kernel : $kernel_version" >>  ${path}/sw_info.txt

    os_version="$(cat /etc/os-release | grep PRETTY_NAME| awk -F '"' '{print $2}')"
    echo "OS : $os_version" >>  ${path}/sw_info.txt
}

collect_log(){
    local path=$1
    mkdir -p ${path}/log
    
    if [ -d "$KNET_COMM_LOG_PATH/" ]; then
        # 检查是否有日志文件
        if ls $KNET_COMM_LOG_PATH/knet_comm.log* 1> /dev/null 2>&1; then
            cp $KNET_COMM_LOG_PATH/knet_comm.log* ${path}/log 2>/dev/null || true
        fi
    fi
}

collect_stats() {
    local path=$1
    if [ -d "$KNET_TELEMETRY_STATS_PATH/" ]; then
        cp -rf "$KNET_TELEMETRY_STATS_PATH" ${path}
    fi
}

knet_collect_info()
{
    timestamp=$(date +"%Y%m%d%H%M%S")
    folder=${timestamp}_info_collect
    mkdir -p ${KNET_COMM_LOG_PATH}/info_collect/${folder}

    collect_hw_info "${KNET_COMM_LOG_PATH}/info_collect/${folder}"
    collect_sw_info "${KNET_COMM_LOG_PATH}/info_collect/${folder}"
    collect_log "${KNET_COMM_LOG_PATH}/info_collect/${folder}"
    collect_stats "${KNET_COMM_LOG_PATH}/info_collect/${folder}"

    pushd "${KNET_COMM_LOG_PATH}/info_collect" > /dev/null 2>&1
        tar -zcPf "$folder.tar.gz" ${folder} --remove-files
        chmod 400 "$folder.tar.gz"
    popd > /dev/null 2>&1
}

handle_interrupt() {
    echo "Interrupted by user."
    exit 1
}

main() {
    if [ $# -ne 0 ]; then
        print_help
        exit 1
    fi

    if ! command -v jq > /dev/null 2>&1; then
        echo "Please install jq."
        exit 1
    fi
    
    trap handle_interrupt INT

    knet_collect_info
    echo "The information is collected and stored in ${KNET_COMM_LOG_PATH}/info_collect/$folder.tar.gz"

    if [ ${error} -ne 0 ]; then
        exit $error
    fi
    exit 0
}

main "$@"