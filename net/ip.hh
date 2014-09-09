/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 */

#ifndef IP_HH_
#define IP_HH_

#include <arpa/inet.h>
#include <cstdint>
#include <array>
#include "core/array_map.hh"
#include "byteorder.hh"
#include "arp.hh"
#include "tcp.hh"
#include "ip_checksum.hh"

namespace net {

uint16_t ip_checksum(const void* data, size_t len);

class ipv4;
template <uint8_t ProtoNum>
class ipv4_l4;
struct ipv4_address;

struct ipv4_address {
    ipv4_address() : ip(0) {}
    explicit ipv4_address(uint32_t ip) : ip(ip) {}

    packed<uint32_t> ip;

    template <typename Adjuster>
    auto adjust_endianness(Adjuster a) { return a(ip); }

    friend bool operator==(ipv4_address x, ipv4_address y) {
        return x.ip == y.ip;
    }
    friend bool operator!=(ipv4_address x, ipv4_address y) {
        return x.ip != y.ip;
    }
} __attribute__((packed));

std::ostream& operator<<(std::ostream& os, ipv4_address a);

}

namespace std {

template <>
struct hash<net::ipv4_address> {
    size_t operator()(net::ipv4_address a) const { return a.ip; }
};

}

namespace net {

struct ipv4_traits {
    using address_type = ipv4_address;
    using inet_type = ipv4_l4<6>;
    struct pseudo_header {
        packed<address_type> src;
        packed<address_type> dst;
        uint8_t zeros = 0;
        uint8_t protocol = 6;
        packed<uint16_t> len;

        pseudo_header(ipv4_address src, ipv4_address dst, uint16_t len)
            : src(src), dst(dst), len(len) {}

        template <typename Adjuster>
        void adjust_endianness(Adjuster a) { a(src, dst, len); }
    };
};

template <uint8_t ProtoNum>
class ipv4_l4 {
    ipv4& _inet;
public:
    ipv4_l4(ipv4& inet) : _inet(inet) {}
    future<> send(ipv4_address from, ipv4_address to, packet p);
};

class ip_protocol {
public:
    virtual ~ip_protocol() {}
    virtual void received(packet p, ipv4_address from, ipv4_address to) = 0;
};

class ipv4_tcp final : public ip_protocol {
    ipv4_l4<6> _inet_l4;
    tcp<ipv4_traits> _tcp;
public:
    ipv4_tcp(ipv4& inet) : _inet_l4(inet), _tcp(_inet_l4) {}
    virtual void received(packet p, ipv4_address from, ipv4_address to) {
        _tcp.received(std::move(p), from, to);
    }
    friend class ipv4;
};

class ipv4 {
public:
    using address_type = ipv4_address;
    static address_type broadcast_address() { return ipv4_address(0xffffffff); }
    static uint16_t arp_protocol_type() { return 0x0800; }
private:
    interface* _netif;
    arp _global_arp;
    arp_for<ipv4> _arp;
    ipv4_address _host_address;
    ipv4_address _netmask = ipv4_address(0xffffff00);
    l3_protocol _l3;
    ipv4_tcp _tcp;
    array_map<ip_protocol*, 256> _l4;
    semaphore _send_sem;
private:
    void run();
    void handle_received_packet(packet p, ethernet_address from);
    bool in_my_netmask(ipv4_address a) const;
public:
    explicit ipv4(interface* netif);
    void set_host_address(ipv4_address ip);
    void send(ipv4_address to, uint8_t proto_num, packet p);
    tcp<ipv4_traits>& get_tcp() { return _tcp._tcp; }
};

template <uint8_t ProtoNum>
inline
future<> ipv4_l4<ProtoNum>::send(ipv4_address from, ipv4_address to, packet p) {
    _inet.send(/* from, */ to, ProtoNum, std::move(p));
    return make_ready_future<>(); // FIXME: ?
}

struct ip_hdr {
    uint8_t ihl : 4;
    uint8_t ver : 4;
    uint8_t dscp : 6;
    uint8_t ecn : 2;
    packed<uint16_t> len;
    packed<uint16_t> id;
    packed<uint16_t> frag;
    uint8_t ttl;
    uint8_t ip_proto;
    packed<uint16_t> csum;
    ipv4_address src_ip;
    ipv4_address dst_ip;
    uint8_t options[0];
    template <typename Adjuster>
    auto adjust_endianness(Adjuster a) {
        return a(len, id, frag, csum, src_ip, dst_ip);
    }
} __attribute__((packed));

struct icmp_hdr {
    uint8_t type;
    uint8_t code;
    packed<uint16_t> csum;
    packed<uint32_t> rest;
    template <typename Adjuster>
    auto adjust_endianness(Adjuster a) {
        return a(csum);
    }
} __attribute__((packed));

}

#endif /* IP_HH_ */