#!/bin/bash
KNET_CONF_FILE="/etc/knet/knet_comm.conf"
KNET_CONF_FILE_BAK="/etc/knet/knet_comm.conf.bak"
KNET_CONF_FILE_TEMP="/etc/knet/knet_comm.conf.tmp"
error=0

print_help() {
    echo         "Usage: sh merge_conf.sh"
}

check_deps() {
    if ! command -v jq > /dev/null 2>&1; then
        echo "Please install jq."
        exit 1
    fi

    # 检查备份配置文件是否存在
    if [ ! -e "$KNET_CONF_FILE_BAK" ]; then
        exit 0
    fi

    # 校验备份配置文件格式
    if ! jq_output=$(jq '.' "$KNET_CONF_FILE_BAK" 2>&1); then
        echo "$jq_output"
        echo "Please check the format of knet_comm.conf.bak."
        exit 1
    fi

    # 检查默认配置文件是否存在
    if [ ! -e "$KNET_CONF_FILE" ]; then
        echo "knet_comm.conf is not found."
        exit 1
    fi

    # 校验默认配置文件格式
    if ! jq_output=$(jq '.' "$KNET_CONF_FILE" 2>&1); then
        echo "$jq_output"
        echo "Please check the format of knet_comm.conf."
        exit 1
    fi
}

merge_conf()
{
    local key_array=("common" "interface" "hw_offload" "proto_stack" "dpdk")
    for first_key in "${key_array[@]}"
    do
        second_keys=($(jq ".$first_key |keys[]" $KNET_CONF_FILE))
        for second_key in "${second_keys[@]}"
        do
            value=$(jq ".$first_key.$second_key" $KNET_CONF_FILE)
            value_bak=$(jq ".$first_key.$second_key" $KNET_CONF_FILE_BAK)
            if [ "$value_bak" != "null" ] && [ "$value" != "$value_bak" ]; then       
                jq ".$first_key.$second_key = $value_bak" "$KNET_CONF_FILE" > "$KNET_CONF_FILE_TEMP" 2>&1
                if [ $? -ne 0 ]; then
                    echo "Failed to merge knet_comm.conf."
                    rm -f $KNET_CONF_FILE_TEMP
                    cp -pf $KNET_CONF_FILE_BAK $KNET_CONF_FILE
                    error=1
                    return
                fi
                mv $KNET_CONF_FILE_TEMP $KNET_CONF_FILE
            fi
        done
    done

    chmod --reference=$KNET_CONF_FILE_BAK $KNET_CONF_FILE
    chown --reference=$KNET_CONF_FILE_BAK $KNET_CONF_FILE
}

main() {
    if [ $# -ne 0 ]; then
        print_help
        exit 1
    fi

    check_deps

    stty intr undef 2>/dev/null
    stty susp undef 2>/dev/null
    stty quit undef 2>/dev/null

    merge_conf

    #恢复禁用的信号
    stty intr ^C 2>/dev/null
    stty susp ^Z 2>/dev/null
    stty quit ^\\ 2>/dev/null

    if [ ${error} -ne 0 ]; then
        exit $error
    fi
    exit 0
}

main "$@"