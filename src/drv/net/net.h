#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define ETH_ALEN 6
#define ETH_TYPE_IP 0x0800
#define ETH_TYPE_ARP 0x0806

typedef struct
{
    uint8_t dst[ETH_ALEN];
    uint8_t src[ETH_ALEN];
    uint16_t type;
} __attribute__((packed)) eth_frame_t;

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

typedef struct
{
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t op;
    uint8_t sha[ETH_ALEN];
    uint8_t spa[4];
    uint8_t tha[ETH_ALEN];
    uint8_t tpa[4];
} __attribute__((packed)) arp_packet_t;

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

typedef struct
{
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t proto;
    uint16_t checksum;
    uint8_t src[4];
    uint8_t dst[4];
} __attribute__((packed)) ip_header_t;

#define TCP_FLAG_FIN (1 << 0)
#define TCP_FLAG_SYN (1 << 1)
#define TCP_FLAG_RST (1 << 2)
#define TCP_FLAG_PSH (1 << 3)
#define TCP_FLAG_ACK (1 << 4)

typedef struct
{
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_off;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urg_ptr;
} __attribute__((packed)) tcp_header_t;

typedef enum
{
    TCP_STATE_CLOSED = 0,
    TCP_STATE_SYN_SENT,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_TIME_WAIT,
} tcp_state_t;

#define TCP_RX_BUF_SIZE (256 * 1024)
#define TCP_TX_BUF_SIZE (16 * 1024)
#define MAX_TCP_CONNS 8

typedef struct
{
    tcp_state_t state;
    uint8_t remote_ip[4];
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t seq;
    uint32_t ack;
    uint32_t remote_window;

    uint8_t rx_buf[TCP_RX_BUF_SIZE];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_avail;
    bool fin_received;
} tcp_conn_t;

void net_init(void);

void net_set_ip(const uint8_t ip[4], const uint8_t gw[4]);

void net_poll(void);

int net_arp_resolve(const uint8_t ip[4], uint8_t mac_out[ETH_ALEN]);

int tcp_connect(const uint8_t ip[4], uint16_t port);
int tcp_send(int id, const void *data, size_t len);
int tcp_recv(int id, void *buf, size_t max_len);
void tcp_close(int id);
bool tcp_is_connected(int id);

static inline uint16_t htons(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}
static inline uint32_t htonl(uint32_t v)
{
    return ((v >> 24) & 0xFF) | (((v >> 16) & 0xFF) << 8) |
           (((v >> 8) & 0xFF) << 16) | ((v & 0xFF) << 24);
}
#define ntohs htons
#define ntohl htonl

#endif

typedef struct
{
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

int dns_resolve(const char *hostname, uint8_t ip_out[4]);
