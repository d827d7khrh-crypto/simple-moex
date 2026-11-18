#pragma once

#include <string>
#include <array>
#include <map>
#include <forward_list>
#include <vector>
#include <algorithm>
#include <ranges>
#include <mutex>
#include <atomic>
#include <ostream>
#include <iomanip>
#include <optional>
#include <future>
#include <chrono>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "recovery.h"
#include "spectra_replay.h"
#include "asts_replay.h"
#include "buffer.h"
#include "lists.h"

namespace fast
{

///
struct SequencingBuffer
{
    using Buffer = misc::BoundedBuffer;

    friend std::ostream& operator<<(std::ostream& os, SequencingBuffer const& s)
    {
        os << '[' << s.sequence_number_ << ',' << s.buffer_ << ']';
        return os;
    }

    constexpr operator Buffer&&()
    { return std::move(buffer_); }

    Buffer buffer_;
    uint32_t sequence_number_;
};

///
class PacketSequencing
{
    using Buffer        = misc::BoundedBuffer;
    using PacketBuffer  = misc::PacketBuffer;
    using DifferedList  = misc::SortedForwardList<SequencingBuffer>;
    using LookahedArray = std::array<Buffer, 10>;

    friend std::ostream& operator<<(std::ostream& os, PacketSequencing::LookahedArray const& a)
    {
        for (size_t i = 0; i < a.size(); ++i) {
            os << a[i];
            if (i != a.size() - 1) os << ',';
        }
        return os;
    }

    struct iterator
    {
        std::reference_wrapper<LookahedArray> array_ref_;
        size_t sequence_number_;

        using difference_type = ssize_t;
        using reference       = typename LookahedArray::value_type&;
        using pointer         = typename LookahedArray::value_type*;

        iterator (std::reference_wrapper<LookahedArray> array_ref, uint32_t sequence_number)
          : array_ref_{std::move(array_ref)},
            sequence_number_{sequence_number + 1}
        {
        }

        constexpr iterator& operator++()
        {
            ++sequence_number_;
            return *this;
        }

        constexpr iterator operator++(int)
        {
            iterator tmp(*this);
            ++(*this);
            return tmp;
        }

        constexpr reference operator*()
        {
            LookahedArray& a = array_ref_;
            return a[sequence_number_ % a.size()];
        }
    };

  public:
    using BuffersMap = std::map<uint32_t, Buffer>;

    // Requires lookahead_array to have a reasonable size.
    static_assert(std::tuple_size<LookahedArray>::value > 1);

    using recovery_task_result = std::optional<uint32_t>;

    template <typename F>
    std::optional<BuffersMap> operator()(PacketBuffer& buffer, F&& recovery_fn)
    {
        constexpr static const uint32_t INVALID_PACKET_NUMBER =
          std::numeric_limits<uint32_t>::max();

        // Extract a sequence number form the front of packet.
        const uint32_t packet_number = buffer.buffer_.size() >= sizeof(uint32_t)
                                         ? *reinterpret_cast<uint32_t*>(buffer.buffer_.begin())
                                         : INVALID_PACKET_NUMBER;
        if (packet_number == INVALID_PACKET_NUMBER) {
            // error: invalid packet length
            return std::nullopt;
        }

        // Amend a data length.
        buffer.buffer_ -= sizeof(uint32_t);
        if (!buffer.buffer_) {
            // error: FAST is missing in packet
            return std::nullopt;
        }

        uint32_t current_number;

        // Compares the value representation of sequence_number_ with that of n - 1. If those
        // are equal, replaces the former with n. Otherwise, after the operation is completed,
        // copy the value of seqence_nubmber_ to current_number.
        auto compare_exchange = [this, &current_number] (const uint32_t n) -> bool {
            current_number = n - 1;
            if (sequence_number_ != current_number) {
                current_number = sequence_number_;
                return current_number == 0;
            }
            sequence_number_ = n;
            return true;
        };

        if (compare_exchange(packet_number)) {
            // 1. The `sequence_number_' equals to that we're expected.
            // 1.1 Lookup at look-ahead array to search sequential outstanding buffers.
            uint32_t search_number = packet_number + 1;
            if (auto buffs = look_ahead(packet_number, search_number)) {
                // The look-ahead lookup has satisfied. The packet number of the last processed
                // buffer becomes a current value of `sequence_number_'.
                sequence_number_ = search_number;
                // Insert the current packet.
                buffs->try_emplace(packet_number, std::move(buffer.buffer_));
                // Accept the buffers sequence.
                return buffs;
            }
            // 1.2 If this is the very first packet. The packet number could not be equal to
            // one. In this case will use that number.
            if (current_number == 0) sequence_number_ = packet_number;
            // Accept the packet.
            return {{{packet_number, std::move(buffer.buffer_)}}};
        }

        // 2. Drop duplicates.
        if (packet_number == current_number) return std::nullopt;

        int32_t distance = packet_number - current_number;
        // 3. The early arrived packet would go into look after lists.
        if (distance > 0) {
            // The index in the array where `current_number' would reside is always reserved.
            if (distance < lookahead_.size()) {
                // If look-ahead array doesn't contain a value at the corresponding cell, put
                // the buffer into look-ahead.
                auto& cell = lookahead_[packet_number % lookahead_.size()];
                if (!cell) cell = std::move(buffer.buffer_);
            } else {
                // If index of buffer's sequence number is behaind a look-ahed array, transfer
                // the buffer to deferred list.
                differed_.insert({std::move(buffer.buffer_), packet_number},
                                 &SequencingBuffer::sequence_number_);

                // Given: `sequence_number_' value - first;
                //        the smallest existing value found by looking up in join
                //        of`lookahead_' and `differed_' - last.
                // Assuming a gap in range [first, last). Starting an async recovery
                // task to try to backfill a gap.
                if (!recovery_result_.valid() && !buffer.recovery_flag_) {
                    const uint32_t search = current_number + 1;
                    auto last             = get_equal_or_next(search);
                    recovery_result_      = std::async(
                      std::launch::async,
                      [this]<typename FT, typename... Args>(
                        FT&& f, Args&&... args) -> recovery_task_result {
                          return recovery_task(std::forward<FT>(f),
                                               std::forward<Args>(args)...);
                      },
                      std::forward<F>(recovery_fn), buffer.local_namespace_,
                      buffer.feed_group_, search, last ? last->at(0) : search);
                    return std::nullopt;
                };
            }
        } else {
            // A potential reference point for checking whether the number of the message
            // in the sequence has been reset or changed.
            reset();
            return {{{packet_number, std::move(buffer.buffer_)}}};
        }

        // 4. Consume async recovery state. Should be called only by packet_serialiser thread.
        if (recovery_result_.valid() && !buffer.recovery_flag_) {
            using namespace std::chrono_literals;
            if (recovery_result_.wait_for(1ns) == std::future_status::ready) {
                if (recovery_task_result result = recovery_result_.get()) {
                    // Recovery process failed, `result' contains a sequence number of a packet
                    // which had a valid buffer before call the recovery_task async method.
                    if (current_number < *result) {
                        const uint32_t next_number = current_number + 1;
                        std::cerr
                          << std::format("[PacketSequencing]: Backfill [{:d},{:d}) failed.\n",
                                         next_number, *result);
                        if (auto search_next = get_equal_or_next(next_number)) {
                            // If next was found in the differed list, shift it to lookahead_.
                            uint32_t search_number = search_next->at(0);
                            if (search_next->at(1))
                                differed_.extract_some(*this, search_number - 1,
                                                       &SequencingBuffer::sequence_number_);

                            if (auto buffs = look_ahead(current_number, search_number)) {
                                std::cerr << std::format(
                                  "[PacketSequencing]: Resuming "
                                  "packets [{:d},{:d}] count={:d}. Gap remains [{:d},{:d}]. "
                                  "Setting the sequence number to {:d}.\n",
                                  buffs->begin()->first, std::prev(buffs->end())->first,
                                  buffs->size(), next_number, search_next->at(0) - 1,
                                  search_number);
                                sequence_number_ = search_number;
                                return buffs;
                            } else {
                                std::cerr
                                  << std::format("[PacketSequencing]: Gap [{:d},{:d}].\n",
                                                 current_number, search_next->at(0) - 1);
                            }
                        }
                    }
                }
            }
        }

        return std::nullopt;
    }

    constexpr size_t size () const
    { return lookahead_.size(); }

    constexpr iterator insert_iterator (uint32_t sequence_number)
    { return iterator(std::ref(lookahead_), sequence_number); }

    constexpr void add_historical_replay (recov::Replay::EndpointRef ep)
    { historical_replay_.push_back(std::move(ep)); }

  private:
    // Fills `look_ahead_result.first' buffers with sequentially ordered values extracted
    // from look-ahead array and possibly deferred list.
    // Returns a pair of `buffers' map and packet number of last extracted buffer.
    std::optional<BuffersMap> look_ahead (uint32_t base, uint32_t& search)
    {
        BuffersMap buffers;
        while (auto& cell = lookahead_[search % lookahead_.size()]) {
            // 1.2 Use a value from look-ahead and continue further lookups to form a serie
            // of adjacent buffers.
            buffers.try_emplace(search, std::move(cell));
            // Decision point. Lookup for next cell's value if
            // still within a look-ahead boundaries.
            if (++search - base < lookahead_.size()) continue;

            // If reached here, a whole looka-head was traversed.
            // In case the look-ahead array was exhausted, the deffered list
            // could also have stored sequential data.
            base = search - 1;
            differed_.extract_some(*this, base, &SequencingBuffer::sequence_number_);
        }

        if (buffers.empty()) return std::nullopt;

        --search;
        return buffers;
    }

    void reset (uint32_t old_number = 0, uint32_t new_number = 0)
    {
        if (new_number == 0) {
            std::swap(sequence_number_, new_number);
            if (new_number != 0) lookahead_.fill({});
        } else {
            if (sequence_number_ == old_number) sequence_number_ = new_number;
        }
    }

    // Searches for a packet number of the first non empty buffer which equals to or greater
    // then `first', which could be found either in the look-ahead array or differed list.
    constexpr std::optional<std::array<uint32_t, 2>> get_equal_or_next (uint32_t base)
    {
        for (uint32_t search = base, end = search + lookahead_.size() - 1; search < end;
             ++search) {
            if (lookahead_[search % lookahead_.size()]) return {{search, 0}};
        }

        if (differed_.begin() != differed_.end())
            return {{differed_.begin()->sequence_number_, 1}};

        return std::nullopt;
    }

    // Async task to try to backfill missing packets in range [first, last).
    template <typename F>
    recovery_task_result recovery_task (F&& recovery_fn, std::string local_namespace,
                                        uint16_t feed_group, uint32_t first, uint32_t last)
    {
        using namespace recov;
        std::unique_ptr<Replay> replay;

        if (config::cfg->exchange_branch == config::MOEX_EXCHANGE_ASTS)
            replay = std::make_unique<asts::TCPReplay>(historical_replay_);
        else if (config::cfg->exchange_branch == config::MOEX_EXCHANGE_SPECTRA)
            replay = std::make_unique<spectra::TCPReplay>(historical_replay_);

        if (replay) {
            if (BackfillRes res = RecoveryFeed{std::move(replay)}(first, last)) {
                for (auto&& buf : *std::move(res)) {
                    using namespace std::chrono;
                    sys_time<microseconds> time = system_clock::now();
                    PacketBuffer packet_buffer{std::move(buf), local_namespace,
                                               time.time_since_epoch().count(), feed_group,
                                               true};
                    std::forward<F>(recovery_fn)(std::move(packet_buffer));
                }
                return std::nullopt;
            }
        }

        return last;
    }

    std::vector<recov::Replay::EndpointRef> historical_replay_;

    std::future<recovery_task_result> recovery_result_;

    LookahedArray lookahead_;
    DifferedList differed_;

    uint32_t sequence_number_{0};
};

}
