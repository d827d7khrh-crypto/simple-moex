#pragma once

#include <cstdint>
#include <memory>

#include "replay.h"

namespace recov
{

class RecoveryFeed
{
  public:
    explicit RecoveryFeed (std::unique_ptr<Replay> replay)
      : replay_{std::move(replay)}
    {
    }

    BackfillRes operator()(uint32_t first, uint32_t last)
    { return replay_ ? replay_->backfill(first, last) : std::nullopt; }

  private:
    std::unique_ptr<Replay> replay_;
};

} // namespace recov
