#pragma once

#include <functional>
#include <filesystem>
#include <string>
#include <memory>
#include <atomic>
#include <optional>
#include <list>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/lockfree/queue.hpp>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "udp_multicast_receiver.h"
#include "fast_message_parser.h"
#include "thread_pool.h"
#include "packet.h"

namespace fast
{

// ============================================================================
// Callback invoked for each successfully decoded FAST message
// ============================================================================

/// Message processing callback.
/// msg             — decoded FAST message (valid only during callback execution)
using MessageHandlerF = std::function<void(FastMessagePtr msg)>;
using MessageHandler  = std::shared_ptr<MessageHandlerF>;

/// Callback invoked when a message cannot be decoded
using MessageErrorHandlerF =
  std::function<void(std::string const &message_namespace, const std::string &error)>;
using MessageErrorHandler = std::shared_ptr<MessageErrorHandlerF>;

// ============================================================================
// FastMessageHandler — bridges UDP multicast receiver with FAST decoder.
// It is not CopyConstructible, MoveConstructible, CopyAssignable, or MoveAssignable.
// ============================================================================

class FastMessageHandler
{
    using DataHandler = asio::UdpMulticastReceiver::DataHandler;
    using SocketInfo  = asio::UdpMulticastReceiver::SocketInfo;

    using PacketBuffer = misc::PacketBuffer;
    using Buffer       = misc::BoundedBuffer;

  public:
    explicit FastMessageHandler (std::filesystem::path const &template_filepath);

    FastMessageHandler (std::filesystem::path const &template_filepath,
                        MessageHandlerF message_handler, MessageErrorHandlerF error_handler);

    ~FastMessageHandler ();

    /// Get error message from the last failed template load
    constexpr std::string const &get_error () const
    { return last_error_; }

    // ------------------------------------------------------------------
    // Callback registration
    // ------------------------------------------------------------------

    /// Register a callback for successfully decoded messages.
    /// Called from the consumer thread (not from the UDP receive thread).
    void on_message (MessageHandlerF handler, bool reset = false);

    /// Register a callback for decode errors.
    /// Called from the UDP receive thread.
    void on_error (MessageErrorHandlerF handler, bool reset = false);

    // ------------------------------------------------------------------
    // Data handler compatible with UdpMulticastManager
    // ------------------------------------------------------------------

    /// Return a DataHandler callable that can be passed to
    /// UdpMulticastManager. This is the main entry point for incoming
    /// UDP packets.
    /// Return a callable compatible with UdpMulticastManager::DataHandler
    DataHandler get_data_handler ();

    // Pre-allocates a slot in a packet_sequencing_ map for this feed_group
    constexpr void add_feed_group (uint16_t feed_group)
    {
        if (packet_sequencing_) packet_sequencing_->try_emplace(feed_group);
    }

    constexpr void add_historical_replay (uint16_t feed_group, std::string const &address,
                                          uint16_t port)
    {
        auto const &replay = historical_replay_
                               .try_emplace(feed_group,
                                            boost::asio::ip::tcp::endpoint{
                                              boost::asio::ip::make_address(address), port})
                               .first->second;
        if (packet_sequencing_)
            (*packet_sequencing_)[feed_group].add_historical_replay(std::cref(replay));
    }

    // ------------------------------------------------------------------
    // Recovery buffers
    // ------------------------------------------------------------------

    // Called by recovery subsystem to inject packets from replay responces.
    void on_recovery_packet (PacketBuffer buffer);

    // ------------------------------------------------------------------
    // Utilities
    // ------------------------------------------------------------------

    /// Message namespace
    static std::string local_namespace (std::string const &multicast_group, uint16_t port);

    /// Convert a FastFieldValue to string representation
    static std::string value_to_string (const FastFieldValue &value);

  private:
    // UDP packect serializer
    void packet_serialiser (std::stop_token stoken);
    // Packets decoder multiplexer thread entry point
    void tasks_multiplexer (std::stop_token stoken);
    // Consumer thread entry point
    void messages_consumer (std::stop_token stoken);

    // Called from the UDP receive thread for each packet
    void on_udp_packet (SocketInfo &si, uint16_t feed_group, size_t length);

    // Called whenever a buffer gets ready to process by packet_serialiser.
    void on_buffered_packet(PacketBuffer);

    // maps a feed_group to a packet preamble handler
    std::optional<std::unordered_map<uint16_t, PacketSequencing>> packet_sequencing_;

    // Callbacks
    MessageHandler message_handler_;
    MessageErrorHandler error_handler_;

    // The historical replay endpoints (if any)
    using HistoricalReplayMap = std::unordered_map<uint16_t, boost::asio::ip::tcp::endpoint>;
    HistoricalReplayMap historical_replay_;

    // The only stop source for all threads / waits
    std::stop_source threads_stop_source_;

    // Workers threads
    ThreadPool workers_;

    // Decoder instance
    FastMessageDecoder decoder_;

    // mutable std::mutex tasks_futures_mutext_;
    // using TaskFutureList = std::list<std::future<FastMessagePtr>>;
    // TaskFutureList tasks_futures_;

    // Roughly computed queue capacity.
    constexpr static inline size_t PACKET_BUFFERS_QUEUE_INITIAL_CAPACITY = 1000UL * 60UL;
    using PacketBuffersQueue = boost::lockfree::queue<PacketBuffer *>;

    std::condition_variable_any packets_cv_;
    PacketBuffersQueue packets_queue_;

    constexpr static inline size_t FUTURE_QUEUE_INITIAL_CAPACITY = 1024UL;
    using FuturesQueue = boost::lockfree::spsc_queue<std::future<FastMessagePtr>>;

    std::condition_variable_any tasks_futures_cv_;
    FuturesQueue tasks_futures_;

    using MESSAGES_QUEUE_CAPACITY = boost::lockfree::capacity<16384>;
    using MessagesQueue = boost::lockfree::spsc_queue<FastMessagePtr, MESSAGES_QUEUE_CAPACITY>;

    std::condition_variable_any messages_queue_cv_;
    MessagesQueue messages_queue_;

    // The packet serializer thread.
    std::jthread packet_serialiser_thread_;
    // The thread which handles future objects of decoding tasks
    std::jthread tasks_multiplex_thread_;
    // The thread which handles messages
    std::jthread consumer_thread_;

    std::string last_error_;
};

} // namespace fast
