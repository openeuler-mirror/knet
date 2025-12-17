/* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved. */

#ifndef DP_IF_PACKET_H
#define DP_IF_PACKET_H

#ifdef __cplusplus
extern "C" {
#endif

struct DP_SockaddrLl { // link layer
    unsigned short sll_family; /* Always AF_PACKET */
    unsigned short sll_protocol; /* Physical-layer protocol */
    int            sll_ifindex; /* Interface number */
    unsigned short sll_hatype; /* ARP hardware type */
    unsigned char  sll_pkttype; /* Packet type */
    unsigned char  sll_halen; /* Length of address */
    unsigned char  sll_addr[8]; /* Physical-layer address */
};

#define DP_PACKET_HOST      0 /* To us                */
#define DP_PACKET_BROADCAST 1 /* To all               */
#define DP_PACKET_MULTICAST 2 /* To group             */
#define DP_PACKET_OTHERHOST 3 /* To someone else      */
#define DP_PACKET_OUTGOING  4 /* Outgoing of any type */
#define DP_PACKET_LOOPBACK  5 /* MC/BRD frame looped back */

#ifdef __cplusplus
}
#endif
#endif
