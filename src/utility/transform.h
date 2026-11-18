#pragma once

#include <bit>
#include <ranges>
#include "fast_message_parser.h"

namespace trns
{

inline constexpr std::pair<uint32_t, uint8_t> pack_24 (uint32_t ui24)
{
    uint32_t out = 0;

    uint8_t *p = reinterpret_cast<uint8_t *>(&out);
    uint8_t *n = p++;

    *p++ = (uint8_t)(ui24 & 0xff);
    ui24 >>= 8;
    *p++ = (uint8_t)(ui24 & 0xff);
    ui24 >>= 8;
    *p = (uint8_t)(ui24 & 0xff);

    *n = *p ? 3 : *(p - 1) ? 2 : 1;
    return {out, *n + 1};
}

inline constexpr void pack_24 (uint32_t ui24, std::vector<uint8_t> &packed)
{
    auto p = pack_24(ui24);
    packed.append_range(
      std::span<uint8_t>(reinterpret_cast<uint8_t *>(&p.first), size_t(p.second)));
}

inline constexpr uint32_t unpack_24 (uint32_t ui24)
{
    uint8_t *p = reinterpret_cast<uint8_t *>(&ui24);
    uint8_t n  = *p++;
    uint32_t x = 0;
    uint8_t *o = reinterpret_cast<uint8_t *>(&x);
    while (n--)
        *o++ = *p++;
    return x;
}

inline constexpr std::vector<uint32_t> unpack_24 (std::span<const uint8_t> const &packed)
{
    std::vector<uint32_t> v;
    for (auto it = packed.begin(); it != packed.end();) {
        uint8_t u[4] = {*it++, 0, 0, 0};
        size_t i     = u[0];
        uint8_t *p   = &u[1];
        for (; i--;)
            *p++ = *it++;
        v.push_back(unpack_24(*reinterpret_cast<uint32_t *>(u)));
    }
    return v;
}

inline constexpr void pack (std::string &s, std::vector<uint8_t> &packed)
{
    const size_t size = s.size();
    uint16_t size16   = static_cast<uint16_t>(size);

    packed.resize(packed.size() + sizeof(uint16_t));
    *reinterpret_cast<uint16_t *>(&*std::prev(packed.end(), sizeof(uint16_t))) = htons(size16);
    packed.append_range(std::span<uint8_t>(reinterpret_cast<uint8_t *>(s.data()), size));
}

inline constexpr size_t unpack (std::string &s, std::span<const uint8_t> const &span)
{
    const uint16_t sz16 = *reinterpret_cast<const uint16_t *>(span.data());
    const size_t n      = ntohs(sz16);
    s.assign(reinterpret_cast<const char *>(span.data() + sizeof(uint16_t)), n);
    return n + sizeof(uint16_t);
}

template <std::integral I>
inline constexpr void pack (I num, std::vector<uint8_t> &packed)
{
    packed.resize(packed.size() + sizeof(uint8_t));
    if constexpr (std::is_same_v<I, bool>) {
        *std::prev(packed.end(), sizeof(uint8_t)) = 1;
        packed.push_back(num == true ? 1 : 0);
    } else {
        *std::prev(packed.end(), sizeof(uint8_t)) = static_cast<uint8_t>(sizeof(I));
        switch (sizeof(I)) {
        case sizeof(uint8_t):
            packed.push_back(num);
            break;
        case sizeof(uint16_t):
            packed.resize(packed.size() + sizeof(uint16_t));
            *reinterpret_cast<uint16_t *>(&*std::prev(packed.end(), sizeof(uint16_t))) =
              htons(num);
            break;
        case sizeof(uint32_t):
            packed.resize(packed.size() + sizeof(uint32_t));
            *reinterpret_cast<uint32_t *>(&*std::prev(packed.end(), sizeof(uint32_t))) =
              htonl(num);
            break;
        case sizeof(uint64_t):
            packed.resize(packed.size() + sizeof(uint64_t));
            *reinterpret_cast<uint64_t *>(&*std::prev(packed.end(), sizeof(uint64_t))) =
              htonll(num);
            break;
        default:
            break;
        }
    }
}

template <std::integral I>
inline constexpr size_t unpack (I &num, std::span<const uint8_t> const &span)
{
    size_t n = span[0];
    switch (sizeof(I)) {
    case sizeof(uint8_t):
        num = span[1];
        break;
    case sizeof(uint16_t):
        num = ntohs(*reinterpret_cast<const uint16_t *>(&span[1]));
        break;
    case sizeof(uint32_t):
        num = ntohl(*reinterpret_cast<const uint32_t *>(&span[1]));
        break;
    case sizeof(uint64_t):
        num = ntohll(*reinterpret_cast<const uint64_t *>(&span[1]));
        break;
    default:
        break;
    }
    return n + sizeof(uint8_t);
}

// Serialize chrono system_clock to send over network
inline constexpr uint64_t serialize_clock (std::chrono::system_clock::rep &val) noexcept
{ return htonll(std::bit_cast<uint64_t>(val)); }

// Deserialize chrono system_clock from 64 bit network order.
inline constexpr std::chrono::system_clock::rep deserialize_clock (uint64_t bits) noexcept
{ return std::bit_cast<std::chrono::system_clock::rep>(ntohll(bits)); }

inline constexpr void pack (std::chrono::system_clock::rep &rep, std::vector<uint8_t> &packed)
{
    constexpr const size_t ext = sizeof(uint64_t) + sizeof(uint8_t);
    packed.resize(packed.size() + ext);
    // Type length byte.
    auto first = std::prev(packed.end(), ext);
    *first++   = static_cast<uint8_t>(sizeof(uint64_t));
    // Data bytes.
    *reinterpret_cast<uint64_t *>(&*first) = serialize_clock(rep);
}

inline constexpr size_t unpack (std::chrono::system_clock::rep &rep,
                                std::span<const uint8_t> const &span)
{
    size_t n = span[0];
    rep      = deserialize_clock(*reinterpret_cast<const uint64_t *>(&span[1]));
    return n + sizeof(uint8_t);
};

// Serialize Decimal64 to send over network
inline constexpr uint64_t serialize_decimal (fast::Decimal64 &val) noexcept
{ return htonll(std::bit_cast<uint64_t>(val)); }

// Deserialize Decimal64 from 64 bit network order.
inline constexpr fast::Decimal64 deserialize_decimal (uint64_t bits) noexcept
{ return std::bit_cast<fast::Decimal64>(ntohll(bits)); }

inline constexpr void pack (fast::Decimal64 &d, std::vector<uint8_t> &packed)
{
    constexpr const size_t ext = sizeof(uint64_t) + sizeof(uint8_t);
    packed.resize(packed.size() + ext);
    // Type length byte.
    auto first = std::prev(packed.end(), ext);
    *first++   = static_cast<uint8_t>(sizeof(uint64_t));
    // Data bytes.
    *reinterpret_cast<uint64_t *>(&*first) = serialize_decimal(d);
}

inline constexpr size_t unpack (fast::Decimal64 &d, std::span<const uint8_t> const &span)
{
    size_t n = span[0];
    return n + sizeof(uint8_t);
}

} // namespace trns
