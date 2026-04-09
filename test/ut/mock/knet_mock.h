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

#ifndef KNET_TEST_MOCK_H
#define KNET_TEST_MOCK_H

#ifdef __cplusplus
extern "C" {
#endif

void *mock_rte_zmalloc(const char *type, size_t size, unsigned align);
void *mock_rte_malloc(const char *type, size_t size, unsigned align);
void mock_rte_free(void *addr);
void mock_rte_mempool_obj_iter(struct rte_mempool *mp, rte_mempool_mem_cb_t *mem_cb, void *mem_cb_arg);
struct rte_mempool *mock_rte_pktmbuf_pool_create_by_ops(const char *name, unsigned int n,
	unsigned int cache_size, uint16_t priv_size, uint16_t data_room_size,
	int socket_id, const char *ops_name);
void mock_rte_mempool_free(struct rte_mempool *mp);
int mock_rte_eth_dev_info_get(uint16_t port_id, struct rte_eth_dev_info *dev_info);
void *mock_rte_mempool_lookup(const char *name);
struct rte_mempool *mock_rte_mempool_create(const char *name, unsigned n, unsigned elt_size,
	unsigned cache_size, unsigned private_data_size,
	rte_mempool_ctor_t *mp_init, void *mp_init_arg,
	rte_mempool_obj_cb_t *obj_init, void *obj_init_arg,
	int socket_id, unsigned flags);

#ifdef __cplusplus
}
#endif

#endif // KNET_TEST_MOCK_H