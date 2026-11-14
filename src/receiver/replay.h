#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include "buffer.h"

namespace recov
{

using Buffer      = misc::BoundedBuffer;
using BackfillRes = std::optional<std::vector<Buffer>>;

class Replay
{
  public:
    virtual ~Replay () = default;

    virtual BackfillRes backfill (uint32_t first, uint32_t last) = 0;

  protected:
    Buffer make_buffer (uint32_t packet_number, void* payload, size_t size)
    {
        const size_t len = size + sizeof(uint32_t);
        void* mem        = ::new char[len];

        *static_cast<uint32_t*>(mem) = packet_number;
        std::memcpy((char*)mem + sizeof(uint32_t), payload, size);
        return Buffer{}.reset(mem, len);
    }
};

} // namespace recov
