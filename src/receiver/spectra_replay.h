#pragma once

#include <format>
#include <iostream>

#include "replay.h"

namespace recov::spectra
{

class TCPReplay : public Replay
{
    constexpr static inline const char name_[] = "TCPReplay SPECTRA";

  public:
    BackfillRes backfill (uint32_t first, uint32_t last) override
    {
        std::cout << std::format("[Recovery {:s}] Backfill the gap [{:d},{:d})\n", name_,
                                 first, last);
        if (login()) {
            buffers_.reserve(last - first);
            if (!buffers_.empty()) return std::move(buffers_);
        }
        return std::nullopt;
    }

  private:
    bool login ()
    { return true; }

    void on_message (uint32_t packet_number, void* payload, size_t size)
    { buffers_.push_back(make_buffer(packet_number, payload, size)); }

    std::vector<Buffer> buffers_;
};

} // namespace recov::spectra
