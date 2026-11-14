#pragma once

#include <iostream>
#include "fast_message_parser.h"

class DoNothingMessageHandler
{
  public:
    constexpr void operator()(const fast::FastMessage &msg)
    {
        uint32_t seq_num = 0;
        if (auto *field = msg.find_field_by_id(34)) {
            if (auto val = field->to_uint32()) seq_num = *val;
        }
        std::cout << std::format("{}: {:10d} {:%F %T} {:%F %T}\n", msg.local_namespace_name,
                                 seq_num,
                                 std::chrono::time_point_cast<std::chrono::microseconds>(
                                   std::chrono::system_clock::time_point(
                                     std::chrono::system_clock::duration(msg.time_us))),
                                 std::chrono::time_point_cast<std::chrono::microseconds>(
                                   std::chrono::system_clock::now()));
    }
};
