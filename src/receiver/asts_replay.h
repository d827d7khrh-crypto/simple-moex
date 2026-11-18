#pragma once

#include <format>
#include <iostream>

#include "replay.h"

namespace recov::asts
{

class TCPReplay : public Replay
{
    constexpr static inline const char name_[] = "TCPReplay ASTS";

  public:
    explicit TCPReplay (std::vector<EndpointRef> const& endpoints)
      : Replay(endpoints)
    {
    }

    BackfillRes backfill (uint32_t first, uint32_t last) override
    {
        std::cout << std::format("[Recovery {:s}] Backfill the gap [{:d},{:d})\n", name_,
                                 first, last);
        if (login()) {
            std::vector<Buffer> buffers;
            buffers.reserve(last - first);
            if (!buffers.empty()) return buffers;
        }
        return std::nullopt;
    }

  private:
    bool login ()
    { return true; }
};

} // namespace recov::asts
