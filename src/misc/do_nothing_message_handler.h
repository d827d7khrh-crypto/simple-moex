#pragma once

#include <iostream>
#include "fast_message_parser.h"

class DoNothingMessageHandler
{
  public:
    constexpr void operator()(const fast::FastMessage& msg)
    {
        std::cout << std::format("{}: {:%F %T} {:%F %T}\n", msg.local_namespace_name,
                                 std::chrono::time_point_cast<std::chrono::microseconds>(
                                   std::chrono::system_clock::time_point(
                                     std::chrono::system_clock::duration(msg.time_us))),
                                 std::chrono::time_point_cast<std::chrono::microseconds>(
                                   std::chrono::system_clock::now()));
    }
};
