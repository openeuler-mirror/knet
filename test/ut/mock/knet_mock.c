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

#include "stdio.h"
#include "mock.h"
#include "rte_ethdev.h"
#include "rte_mbuf.h"
#include "rte_malloc.h"
#include "rte_timer.h"

unsigned int mock_rte_socket_id(void)
{
    return 0;
}

int mock_rte_eth_dev_socket_id(uint16_t port_id)
{
    return 0;
}

int mock_rte_eth_tx_queue_setup(uint16_t port_id, uint16_t tx_queue_id,
                uint16_t nb_tx_desc, unsigned int socket_id,
                const struct rte_eth_txconf *tx_conf)
{
    return 0;
}

int mock_rte_eth_rx_queue_setup(uint16_t port_id, uint16_t rx_queue_id,
                uint16_t nb_rx_desc, unsigned int socket_id,
                const struct rte_eth_rxconf *rx_conf,
                struct rte_mempool *mb_pool)
{
    return 0;
}

int mock_rte_eth_dev_start(uint16_t port_id)
{
    return 0;
}

int mock_rte_eth_dev_stop(uint16_t port_id)
{
    return 0;
}

void mock_rte_mempool_obj_iter(struct rte_mempool *mp, rte_mempool_mem_cb_t *mem_cb, void *mem_cb_arg)
{
    printf("mock_rte_mempool_obj_iter\n");
}


void *mock_rte_malloc(const char *type, size_t size, unsigned align)
{
    printf("%s\n", __func__);
	return calloc(1, size);
}

void *mock_rte_zmalloc(const char *type, size_t size, unsigned align)
{
    printf("%s\n", __func__);
	return calloc(1, size);
}

void *mock_rte_malloc_socket(const char *type, size_t size, unsigned align, int socket)
{
    printf("%s\n", __func__);
	return calloc(1, size);
}

void *mock_rte_zmalloc_socket(const char *type, size_t size, unsigned align, int socket)
{
    printf("%s\n", __func__);
	return calloc(1, size);
}

void mock_rte_free(void *addr)
{
    printf("%s\n", __func__);
	return free(addr);
}

int mock_rte_eal_init(void)
{
    return 0;
}

int mock_rte_eal_cleanup(int argc, char **argv)
{
    return 0;
}

int mock_rte_eth_dev_get_port_by_name(const char *name, uint16_t *port_id)
{
    char path[256];
    FILE *file;
    int ifindex;
    printf("mock_rte_eth_dev_get_port_by_name\n");
 
    // 构建网卡目录下的ifindex文件路径
    snprintf(path, sizeof(path), "/sys/class/net/%s/ifindex", name);
 
    // 打开ifindex文件
    file = fopen(path, "r");
    if (file == NULL) {
        perror("Error opening ifindex file");
        return -EINVAL;
    }
 
    // 读取ifindex值
    if (fscanf(file, "%d", &ifindex) != 1) {
        fclose(file);
        return -EINVAL;
    }
 
    fclose(file);
    *port_id = ifindex;
    

    return 0;
}

void mock_rte_timer_subsystem_finalize(void)
{
    return;
}

int mock_rte_eth_dev_configure(uint16_t port_id, uint16_t nb_rx_queue,
                uint16_t nb_tx_queue, const struct rte_eth_conf *eth_conf)
{
    return 0;
}

struct rte_mempool *mock_rte_pktmbuf_pool_create_by_ops(const char *name, unsigned int n,
	unsigned int cache_size, uint16_t priv_size, uint16_t data_room_size,
	int socket_id, const char *ops_name)
{
    printf("mock_rte_pktmbuf_pool_create_by_ops\n");

    struct rte_mempool *rte_pool = calloc(1, sizeof(struct rte_mempool));

    return rte_pool;
}

void mock_rte_mempool_free(struct rte_mempool *mp)
{
    printf("%s\n", __func__);
    free(mp);
}

int mock_rte_eth_dev_info_get(uint16_t port_id, struct rte_eth_dev_info *dev_info)
{
    memset(dev_info, 0, sizeof(struct rte_eth_dev_info));
    dev_info->max_tx_queues = 2;
    dev_info->max_rx_queues = 2;
    dev_info->tx_desc_lim.nb_min = 256;
    dev_info->tx_desc_lim.nb_max = 65535;
    dev_info->rx_desc_lim.nb_min = 256;
    dev_info->rx_desc_lim.nb_max = 65535;
    dev_info->min_mtu = 0;
    dev_info->max_mtu = 65535;

    dev_info->tx_offload_capa =
        RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_UDP_CKSUM | RTE_ETH_TX_OFFLOAD_TCP_CKSUM | RTE_ETH_TX_OFFLOAD_MULTI_SEGS;
    dev_info->rx_offload_capa =
        RTE_ETH_RX_OFFLOAD_IPV4_CKSUM | RTE_ETH_RX_OFFLOAD_UDP_CKSUM | RTE_ETH_RX_OFFLOAD_TCP_CKSUM | RTE_ETH_RX_OFFLOAD_SCATTER;
    return 0;
}

void *mock_rte_mempool_lookup(const char *name)
{
    printf("%s\n", __func__);
    return calloc(1, sizeof(struct rte_mempool));
}
struct rte_mempool *mock_rte_mempool_create(const char *name, unsigned n, unsigned elt_size,
	unsigned cache_size, unsigned private_data_size,
	rte_mempool_ctor_t *mp_init, void *mp_init_arg,
	rte_mempool_obj_cb_t *obj_init, void *obj_init_arg,
	int socket_id, unsigned flags) {
    printf("mock_rte_mempool_create\n");
    printf("mock_rte_mempool_create\n");
    printf("mock_rte_mempool_create\n");
    printf("mock_rte_mempool_create\n");
    printf("mock_rte_mempool_create\n");
    static struct rte_mempool mp = {0};
    return &mp;
}
