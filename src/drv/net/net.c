

#include "net.h"
#include "e1000.h"
#include "../../libk/core/mem.h"
#include "../../libk/string.h"
#include "../../libk/debug/log.h"

#define DNS_LOCAL_PORT 53000
static void udp_dns_handle(const uint8_t *payload, size_t len);

static uint8_t my_ip[4] = {10, 0, 2, 15};
static uint8_t my_gw[4] = {10, 0, 2, 2};
static uint8_t my_mac[6] = {0};
static uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void net_set_ip(const uint8_t ip[4], const uint8_t gw[4])
{
    memcpy(my_ip, ip, 4);
    memcpy(my_gw, gw, 4);
}

#define ARP_CACHE_SIZE 16
typedef struct
{
    uint8_t ip[4];
    uint8_t mac[6];
    bool valid;
} arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE_SIZE];

static void arp_cache_put(const uint8_t ip[4], const uint8_t mac[6])
{

    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (arp_cache[i].valid && memcmp(arp_cache[i].ip, ip, 4) == 0)
        {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }

    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (!arp_cache[i].valid)
        {
            memcpy(arp_cache[i].ip, ip, 4);
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = true;
            return;
        }
    }
}

static bool arp_cache_get(const uint8_t ip[4], uint8_t mac_out[6])
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (arp_cache[i].valid && memcmp(arp_cache[i].ip, ip, 4) == 0)
        {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return true;
        }
    }
    return false;
}

static uint8_t tx_scratch[2048];

static int eth_send(const uint8_t dst_mac[6], uint16_t ethertype,
                    const void *payload, size_t payload_len)
{
    if (payload_len + sizeof(eth_frame_t) > sizeof(tx_scratch))
        return -1;
    eth_frame_t *eth = (eth_frame_t *)tx_scratch;
    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, my_mac, 6);
    eth->type = htons(ethertype);
    memcpy(tx_scratch + sizeof(eth_frame_t), payload, payload_len);
    return e1000_send_packet(tx_scratch, sizeof(eth_frame_t) + payload_len);
}

static void arp_send_request(const uint8_t target_ip[4])
{
    uint8_t pkt[sizeof(arp_packet_t)];
    arp_packet_t *arp = (arp_packet_t *)pkt;
    arp->htype = htons(1);
    arp->ptype = htons(ETH_TYPE_IP);
    arp->hlen = 6;
    arp->plen = 4;
    arp->op = htons(ARP_OP_REQUEST);
    memcpy(arp->sha, my_mac, 6);
    memcpy(arp->spa, my_ip, 4);
    memset(arp->tha, 0, 6);
    memcpy(arp->tpa, target_ip, 4);
    eth_send(bcast_mac, ETH_TYPE_ARP, pkt, sizeof(pkt));
}

static void arp_send_reply(const uint8_t req_mac[6], const uint8_t req_ip[4])
{
    uint8_t pkt[sizeof(arp_packet_t)];
    arp_packet_t *arp = (arp_packet_t *)pkt;
    arp->htype = htons(1);
    arp->ptype = htons(ETH_TYPE_IP);
    arp->hlen = 6;
    arp->plen = 4;
    arp->op = htons(ARP_OP_REPLY);
    memcpy(arp->sha, my_mac, 6);
    memcpy(arp->spa, my_ip, 4);
    memcpy(arp->tha, req_mac, 6);
    memcpy(arp->tpa, req_ip, 4);
    eth_send(req_mac, ETH_TYPE_ARP, pkt, sizeof(pkt));
}

int net_arp_resolve(const uint8_t ip[4], uint8_t mac_out[6])
{
    if (arp_cache_get(ip, mac_out))
        return 0;

    for (int attempt = 0; attempt < 5; attempt++)
    {
        arp_send_request(ip);

        for (int i = 0; i < 10000; i++)
        {
            net_poll();
            if (arp_cache_get(ip, mac_out))
                return 0;

            for (volatile int d = 0; d < 500; d++)
                ;
        }
    }
    log("ARP: failed to resolve %d.%d.%d.%d", 2, 1,
        ip[0], ip[1], ip[2], ip[3]);
    return -1;
}

static uint16_t ip_checksum(const void *data, size_t len)
{
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1)
    {
        sum += *p++;
        len -= 2;
    }
    if (len)
        sum += *(const uint8_t *)p;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static uint16_t tcp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                             const void *tcp_seg, size_t tcp_len)
{

    uint8_t pseudo[12];
    memcpy(pseudo, src_ip, 4);
    memcpy(pseudo + 4, dst_ip, 4);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_TCP;
    pseudo[10] = (uint8_t)(tcp_len >> 8);
    pseudo[11] = (uint8_t)(tcp_len);

    uint32_t sum = 0;
    const uint16_t *p;
    size_t n;

    p = (const uint16_t *)pseudo;
    n = 12;
    while (n > 1)
    {
        sum += *p++;
        n -= 2;
    }

    p = (const uint16_t *)tcp_seg;
    n = tcp_len;
    while (n > 1)
    {
        sum += *p++;
        n -= 2;
    }
    if (n)
        sum += *(const uint8_t *)p;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static uint16_t ip_id_counter = 1;

static int ip_send(const uint8_t dst_ip[4], uint8_t proto,
                   const void *payload, size_t payload_len)
{

    uint8_t nexthop[4];

    if (dst_ip[0] == my_ip[0] && dst_ip[1] == my_ip[1] && dst_ip[2] == my_ip[2])
        memcpy(nexthop, dst_ip, 4);
    else
        memcpy(nexthop, my_gw, 4);

    uint8_t dst_mac[6];
    if (net_arp_resolve(nexthop, dst_mac) < 0)
        return -1;

    size_t total = sizeof(ip_header_t) + payload_len;
    if (total > 1500)
        return -1;

    ip_header_t hdr;
    hdr.ver_ihl = 0x45;
    hdr.tos = 0;
    hdr.total_len = htons((uint16_t)total);
    hdr.id = htons(ip_id_counter++);
    hdr.flags_frag = htons(0x4000);
    hdr.ttl = 64;
    hdr.proto = proto;
    hdr.checksum = 0;
    memcpy(hdr.src, my_ip, 4);
    memcpy(hdr.dst, dst_ip, 4);
    hdr.checksum = ip_checksum(&hdr, sizeof(hdr));

    uint8_t buf[1520];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), payload, payload_len);
    return eth_send(dst_mac, ETH_TYPE_IP, buf, total);
}

static tcp_conn_t conns[MAX_TCP_CONNS];

static tcp_conn_t *conn_get(int id)
{
    if (id < 0 || id >= MAX_TCP_CONNS)
        return NULL;
    return &conns[id];
}

static uint32_t simple_rand_seq(void)
{
    static uint32_t s = 0xDEADBEEF;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static int tcp_send_seg(tcp_conn_t *c, uint8_t flags,
                        const void *data, size_t data_len)
{
    uint8_t seg[sizeof(tcp_header_t) + 1460];
    if (data_len > 1460)
        data_len = 1460;

    tcp_header_t *tcp = (tcp_header_t *)seg;
    tcp->src_port = htons(c->local_port);
    tcp->dst_port = htons(c->remote_port);
    tcp->seq = htonl(c->seq);
    tcp->ack = htonl(c->ack);
    tcp->data_off = 0x50;
    tcp->flags = flags;
    tcp->window = htons(8192);
    tcp->checksum = 0;
    tcp->urg_ptr = 0;

    if (data && data_len)
        memcpy(seg + sizeof(tcp_header_t), data, data_len);

    size_t seg_len = sizeof(tcp_header_t) + data_len;
    tcp->checksum = tcp_checksum(my_ip, c->remote_ip, seg, seg_len);
    return ip_send(c->remote_ip, IP_PROTO_TCP, seg, seg_len);
}

static void tcp_handle(const uint8_t src_ip[4], const tcp_header_t *tcp,
                       size_t total_ip_payload_len)
{
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint16_t src_port = ntohs(tcp->src_port);
    uint32_t seq = ntohl(tcp->seq);
    uint32_t ack_num = ntohl(tcp->ack);
    uint8_t flags = tcp->flags;
    size_t hdr_len = (tcp->data_off >> 4) * 4;
    size_t data_len = total_ip_payload_len - hdr_len;
    const uint8_t *data = (const uint8_t *)tcp + hdr_len;

    tcp_conn_t *c = NULL;
    for (int i = 0; i < MAX_TCP_CONNS; i++)
    {
        if (conns[i].state != TCP_STATE_CLOSED &&
            conns[i].local_port == dst_port &&
            conns[i].remote_port == src_port &&
            memcmp(conns[i].remote_ip, src_ip, 4) == 0)
        {
            c = &conns[i];
            break;
        }
    }
    if (!c)
        return;

    switch (c->state)
    {
    case TCP_STATE_SYN_SENT:
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK))
        {
            c->ack = seq + 1;
            c->seq = ack_num;
            tcp_send_seg(c, TCP_FLAG_ACK, NULL, 0);
            c->state = TCP_STATE_ESTABLISHED;
            log("TCP: established port %d", 1, 0, (int)c->local_port);
        }
        break;

    case TCP_STATE_ESTABLISHED:
    case TCP_STATE_FIN_WAIT:

        if (flags & TCP_FLAG_ACK)
        {

            (void)ack_num;
        }

        if (data_len > 0)
        {
            uint32_t free_space = TCP_RX_BUF_SIZE - c->rx_avail;
            size_t to_copy = data_len < free_space ? data_len : free_space;
            for (size_t i = 0; i < to_copy; i++)
            {
                c->rx_buf[c->rx_tail] = data[i];
                c->rx_tail = (c->rx_tail + 1) % TCP_RX_BUF_SIZE;
            }
            c->rx_avail += to_copy;
            c->ack = seq + (uint32_t)data_len;

            tcp_send_seg(c, TCP_FLAG_ACK, NULL, 0);
        }
        if (flags & TCP_FLAG_FIN)
        {

            c->ack = seq + (uint32_t)data_len + 1;
            c->fin_received = true;
            tcp_send_seg(c, TCP_FLAG_ACK, NULL, 0);
            if (c->state == TCP_STATE_FIN_WAIT)
                c->state = TCP_STATE_TIME_WAIT;
            else
                c->state = TCP_STATE_CLOSE_WAIT;
        }
        break;

    default:
        break;
    }
}

static void ip_handle(const uint8_t *pkt, size_t len)
{
    if (len < sizeof(ip_header_t))
        return;
    const ip_header_t *ip = (const ip_header_t *)pkt;
    if ((ip->ver_ihl >> 4) != 4)
        return;
    if (memcmp(ip->dst, my_ip, 4) != 0)
        return;

    size_t ip_hdr_len = (ip->ver_ihl & 0xF) * 4;
    size_t total = ntohs(ip->total_len);
    if (total > len)
        return;

    const uint8_t *payload = pkt + ip_hdr_len;
    size_t payload_len = total - ip_hdr_len;

    if (ip->proto == IP_PROTO_TCP)
    {
        if (payload_len < sizeof(tcp_header_t))
            return;
        tcp_handle(ip->src, (const tcp_header_t *)payload, payload_len);
    }
    else if (ip->proto == IP_PROTO_UDP)
    {
        if (payload_len < sizeof(udp_header_t))
            return;
        const udp_header_t *udph = (const udp_header_t *)payload;
        uint16_t dst_port = ntohs(udph->dst_port);
        if (dst_port == DNS_LOCAL_PORT)
        {
            udp_dns_handle(payload + sizeof(udp_header_t),
                           payload_len - sizeof(udp_header_t));
        }
    }
}

static void arp_handle(const uint8_t *pkt, size_t len)
{
    if (len < sizeof(arp_packet_t))
        return;
    const arp_packet_t *arp = (const arp_packet_t *)pkt;
    if (ntohs(arp->ptype) != ETH_TYPE_IP)
        return;

    arp_cache_put(arp->spa, arp->sha);

    if (ntohs(arp->op) == ARP_OP_REQUEST)
    {
        if (memcmp(arp->tpa, my_ip, 4) == 0)
            arp_send_reply(arp->sha, arp->spa);
    }
}

static uint8_t rx_scratch[2048];

void net_poll(void)
{
    int got;
    while ((got = e1000_receive_packet(rx_scratch, sizeof(rx_scratch))) > 0)
    {
        if ((size_t)got < sizeof(eth_frame_t))
            continue;
        eth_frame_t *eth = (eth_frame_t *)rx_scratch;
        uint16_t et = ntohs(eth->type);
        const uint8_t *payload = rx_scratch + sizeof(eth_frame_t);
        size_t payload_len = (size_t)got - sizeof(eth_frame_t);

        if (et == ETH_TYPE_ARP)
            arp_handle(payload, payload_len);
        else if (et == ETH_TYPE_IP)
            ip_handle(payload, payload_len);
    }
}

static int udp_send(const uint8_t dst_ip[4], uint16_t src_port,
                    uint16_t dst_port, const void *data, size_t data_len)
{
    size_t total = sizeof(udp_header_t) + data_len;
    if (total > 1472)
        return -1;

    uint8_t buf[1480];
    udp_header_t *udp = (udp_header_t *)buf;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons((uint16_t)total);
    udp->checksum = 0;
    memcpy(buf + sizeof(udp_header_t), data, data_len);

    return ip_send(dst_ip, IP_PROTO_UDP, buf, total);
}

#define DNS_SERVER_IP {8, 8, 8, 8}
#define DNS_PORT 53
#define DNS_LOCAL_PORT 53000
#define DNS_BUF_SIZE 512

static uint8_t dns_rx_buf[DNS_BUF_SIZE];
static int dns_rx_len = 0;
static uint16_t dns_rx_txid = 0;
static bool dns_rx_ready = false;

static void udp_dns_handle(const uint8_t *payload, size_t len)
{
    if (len < 12 || len > DNS_BUF_SIZE)
        return;
    uint16_t txid = (uint16_t)((payload[0] << 8) | payload[1]);
    if (txid != dns_rx_txid)
        return;
    memcpy(dns_rx_buf, payload, len);
    dns_rx_len = (int)len;
    dns_rx_ready = true;
}

static int dns_build_query(uint8_t *buf, size_t bufsize,
                           uint16_t txid, const char *hostname)
{
    if (bufsize < 512)
        return -1;
    int pos = 0;

    buf[pos++] = (uint8_t)(txid >> 8);
    buf[pos++] = (uint8_t)(txid & 0xFF);
    buf[pos++] = 0x01;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    const char *p = hostname;
    while (*p)
    {
        const char *dot = p;
        while (*dot && *dot != '.')
            dot++;
        size_t label_len = (size_t)(dot - p);
        if (label_len == 0 || label_len > 63)
            return -1;
        buf[pos++] = (uint8_t)label_len;
        for (size_t i = 0; i < label_len; i++)
            buf[pos++] = (uint8_t)p[i];
        p = dot;
        if (*p == '.')
            p++;
    }
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;
    return pos;
}

static int dns_skip_name(const uint8_t *buf, int len, int pos)
{
    while (pos < len)
    {
        uint8_t c = buf[pos];
        if (c == 0)
        {
            return pos + 1;
        }
        if ((c & 0xC0) == 0xC0)
        {
            return pos + 2;
        }
        pos += 1 + c;
    }
    return -1;
}

static int dns_parse_response(const uint8_t *buf, int len, uint8_t ip_out[4])
{
    if (len < 12)
        return -1;

    uint16_t flags = (uint16_t)((buf[2] << 8) | buf[3]);
    uint16_t ancount = (uint16_t)((buf[6] << 8) | buf[7]);
    if ((flags & 0x8000) == 0)
        return -1;
    if ((flags & 0x000F) != 0)
        return -1;
    if (ancount == 0)
        return -1;

    int pos = 12;

    uint16_t qdcount = (uint16_t)((buf[4] << 8) | buf[5]);
    for (int q = 0; q < qdcount; q++)
    {
        pos = dns_skip_name(buf, len, pos);
        if (pos < 0)
            return -1;
        pos += 4;
    }

    for (int a = 0; a < ancount; a++)
    {
        int name_end = dns_skip_name(buf, len, pos);
        if (name_end < 0 || name_end + 10 > len)
            return -1;
        uint16_t rtype = (uint16_t)((buf[name_end + 0] << 8) | buf[name_end + 1]);
        uint16_t rdlen = (uint16_t)((buf[name_end + 8] << 8) | buf[name_end + 9]);
        int rdata = name_end + 10;
        log("DNS: answer rtype=%d rdlen=%d", 1, 0, (int)rtype, (int)rdlen);
        if (rdata + rdlen > len)
            return -1;
        if (rtype == 1 && rdlen == 4)
        {
            memcpy(ip_out, buf + rdata, 4);
            return 0;
        }
        pos = rdata + rdlen;
    }
    return -1;
}

int dns_resolve(const char *hostname, uint8_t ip_out[4])
{
    static const uint8_t dns_ip[] = DNS_SERVER_IP;
    static uint16_t txid_counter = 0x1337;

    int is_ip = 1;
    for (const char *p = hostname; *p; p++)
    {
        if ((*p < '0' || *p > '9') && *p != '.')
        {
            is_ip = 0;
            break;
        }
    }
    if (is_ip)
    {
        unsigned int a = 0, b = 0, c = 0, d = 0;

        const char *p = hostname;
        while (*p >= '0' && *p <= '9')
        {
            a = a * 10 + (*p - '0');
            p++;
        }
        if (*p++ != '.')
            return -1;
        while (*p >= '0' && *p <= '9')
        {
            b = b * 10 + (*p - '0');
            p++;
        }
        if (*p++ != '.')
            return -1;
        while (*p >= '0' && *p <= '9')
        {
            c = c * 10 + (*p - '0');
            p++;
        }
        if (*p++ != '.')
            return -1;
        while (*p >= '0' && *p <= '9')
        {
            d = d * 10 + (*p - '0');
            p++;
        }
        ip_out[0] = (uint8_t)a;
        ip_out[1] = (uint8_t)b;
        ip_out[2] = (uint8_t)c;
        ip_out[3] = (uint8_t)d;
        return 0;
    }

    uint16_t txid = txid_counter++;
    dns_rx_ready = false;
    dns_rx_txid = txid;

    uint8_t qbuf[DNS_BUF_SIZE];
    int qlen = dns_build_query(qbuf, sizeof(qbuf), txid, hostname);
    if (qlen < 0)
        return -1;

    for (int attempt = 0; attempt < 3; attempt++)
    {
        udp_send(dns_ip, DNS_LOCAL_PORT, DNS_PORT, qbuf, (size_t)qlen);

        for (int i = 0; i < 3000000 && !dns_rx_ready; i++)
        {
            net_poll();
            for (volatile int d = 0; d < 100; d++)
                ;
        }
        if (dns_rx_ready)
            break;
        log("DNS: timeout attempt %d for %s", 1, 0, attempt + 1, hostname);
    }

    if (!dns_rx_ready)
    {
        log("DNS: failed to resolve %s", 2, 1, hostname);
        return -1;
    }

    if (dns_parse_response(dns_rx_buf, dns_rx_len, ip_out) < 0)
    {
        log("DNS: no A record for %s", 2, 1, hostname);
        return -1;
    }

    log("DNS: %s -> %d.%d.%d.%d", 1, 0,
        hostname, ip_out[0], ip_out[1], ip_out[2], ip_out[3]);
    return 0;
}

void net_init(void)
{
    memset(conns, 0, sizeof(conns));
    memset(arp_cache, 0, sizeof(arp_cache));
    e1000_get_mac_address(my_mac);
    log("net: MAC %02x:%02x:%02x:%02x:%02x:%02x  IP %d.%d.%d.%d",
        1, 0,
        my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5],
        my_ip[0], my_ip[1], my_ip[2], my_ip[3]);

    static const uint8_t qemu_gw_mac[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
    arp_cache_put(my_gw, qemu_gw_mac);
    log("net: gateway %d.%d.%d.%d seeded in ARP cache", 1, 0,
        my_gw[0], my_gw[1], my_gw[2], my_gw[3]);
}

int tcp_connect(const uint8_t ip[4], uint16_t port)
{

    int id = -1;
    for (int i = 0; i < MAX_TCP_CONNS; i++)
    {
        if (conns[i].state == TCP_STATE_CLOSED)
        {
            id = i;
            break;
        }
    }
    if (id < 0)
        return -1;

    tcp_conn_t *c = &conns[id];
    memset(c, 0, sizeof(*c));
    memcpy(c->remote_ip, ip, 4);
    c->remote_port = port;
    c->local_port = (uint16_t)(49152 + id * 100 + (simple_rand_seq() & 0xFF));
    c->seq = simple_rand_seq();
    c->state = TCP_STATE_SYN_SENT;
    c->fin_received = false;

    tcp_send_seg(c, TCP_FLAG_SYN, NULL, 0);
    c->seq++;

    for (int i = 0; i < 500000; i++)
    {
        net_poll();
        if (c->state == TCP_STATE_ESTABLISHED)
            return id;
        if (c->state == TCP_STATE_CLOSED)
            return -1;
        if (i > 0 && (i % 50000) == 0)
        {
            log("TCP: retransmit SYN (attempt %d)", 1, 0, i / 50000);
            tcp_send_seg(c, TCP_FLAG_SYN, NULL, 0);
        }
        for (volatile int d = 0; d < 2000; d++)
            ;
    }
    log("TCP: connect timeout", 2, 1);
    c->state = TCP_STATE_CLOSED;
    return -1;
}

int tcp_send(int id, const void *data, size_t len)
{
    tcp_conn_t *c = conn_get(id);
    if (!c || c->state != TCP_STATE_ESTABLISHED)
        return -1;

    const uint8_t *ptr = (const uint8_t *)data;
    size_t sent = 0;
    while (sent < len)
    {
        size_t chunk = len - sent;
        if (chunk > 1460)
            chunk = 1460;
        if (tcp_send_seg(c, TCP_FLAG_PSH | TCP_FLAG_ACK, ptr + sent, chunk) < 0)
            return -1;
        c->seq += (uint32_t)chunk;
        sent += chunk;
    }
    return (int)sent;
}

int tcp_recv(int id, void *buf, size_t max_len)
{
    tcp_conn_t *c = conn_get(id);
    if (!c)
        return -1;

    for (int i = 0; i < 2000000; i++)
    {
        net_poll();

        if (c->rx_avail > 0)
            break;

        if (c->fin_received)
        {

            for (int j = 0; j < 1000; j++)
            {
                net_poll();
                if (c->rx_avail > 0)
                    goto has_data;
                for (volatile int d = 0; d < 100; d++)
                    ;
            }
            log("tcp_recv: EOF avail=%u fin=%d", 1, 0,
                c->rx_avail, (int)c->fin_received);
            return 0;
        }
        if (c->state == TCP_STATE_CLOSED)
            return -1;
        for (volatile int d = 0; d < 200; d++)
            ;
    }
has_data:
    if (c->rx_avail == 0)
        return 0;

    size_t to_read = c->rx_avail < max_len ? c->rx_avail : max_len;
    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < to_read; i++)
    {
        dst[i] = c->rx_buf[c->rx_head];
        c->rx_head = (c->rx_head + 1) % TCP_RX_BUF_SIZE;
    }
    c->rx_avail -= (uint32_t)to_read;
    return (int)to_read;
}

void tcp_close(int id)
{
    tcp_conn_t *c = conn_get(id);
    if (!c || c->state == TCP_STATE_CLOSED)
        return;
    if (c->state == TCP_STATE_ESTABLISHED)
    {
        tcp_send_seg(c, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        c->seq++;
        c->state = TCP_STATE_FIN_WAIT;
    }
    c->state = TCP_STATE_CLOSED;
}

bool tcp_is_connected(int id)
{
    tcp_conn_t *c = conn_get(id);
    return c && c->state == TCP_STATE_ESTABLISHED;
}
