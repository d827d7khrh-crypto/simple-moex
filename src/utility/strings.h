#pragma once

#include <string>
#include <cstring>
#include <cstdint>

namespace misc
{

// UTCTimestamp:
//  20260513212711210 to 2026-05-13T21:27:11:210
inline std::string to_date (uint64_t date_long)
{
    std::string_view sv{std::to_string(date_long)};
    std::string str = "XXXX-XX-XXTXX:XX:XX:XXX";
    str.replace(0, 4, sv.data(), 4);
    sv.remove_prefix(4);
    str.replace(5, 2, sv.data(), 2);
    sv.remove_prefix(2);
    str.replace(8, 2, sv.data(), 2);
    sv.remove_prefix(2);
    str.replace(11, 2, sv.data(), 2);
    sv.remove_prefix(2);
    str.replace(14, 2, sv.data(), 2);
    sv.remove_prefix(2);
    str.replace(17, 2, sv.data(), 2);
    sv.remove_prefix(2);
    str.replace(20, 3, sv.data(), 3);
    return str;
}

// UTCTimeOnly:
//  204859999889657 to 20:48:59:999889657
//   71213012345678 to 07:12:13.012345678
inline std::string to_time_ns (uint64_t time_long)
{
    std::string_view sv{std::to_string(time_long)};
    std::string str = "0X:XX:XX:XXXXXXXXX";
    if (sv.size() == 15) {
        str.replace(0, 2, sv.data(), 2);
        sv.remove_prefix(2);
    } else {
        str.replace(1, 1, sv.data(), 1);
        sv.remove_prefix(1);
    }
    str.replace(3, 2, sv.data(), 2);
    sv.remove_prefix(2);
    str.replace(6, 2, sv.data(), 2);
    sv.remove_prefix(2);
    str.replace(9, 9, sv.data(), 9);
    return str;
}

inline std::string to_time_us (uint64_t time_long)
{ return to_time_ns(time_long).erase(15, 3); }

} // namespace misc
