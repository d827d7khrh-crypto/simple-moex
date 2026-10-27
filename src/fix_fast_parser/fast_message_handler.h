#pragma once

#include <functional>
#include <filesystem>
#include <string>
#include <memory>
#include <atomic>
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <boost/lockfree/spsc_queue.hpp>

#include "udp_multicast_receiver.h"
#include "fast_message_parser.h"

namespace fast
{

// ============================================================================
// Callback invoked for each successfully decoded FAST message
// ============================================================================

/// Message processing callback.
/// msg             — decoded FAST message (valid only during callback execution)
using MessageHandlerF = std::function<void(const FastMessage& msg)>;
using MessageHandler  = std::shared_ptr<MessageHandlerF>;

/// Callback invoked when a message cannot be decoded
using MessageErrorHandlerF =
  std::function<void(std::string const& message_namespace, const std::string& error)>;
using MessageErrorHandler = std::shared_ptr<MessageErrorHandlerF>;

// ============================================================================
// FastMessageHandler — bridges UDP multicast receiver with FAST decoder.
// It is not CopyConstructible, MoveConstructible, CopyAssignable, or MoveAssignable.
// ============================================================================

class FastMessageHandler
{
    using DataHandler = asio::UdpMulticastReceiver::DataHandler;
    using SocketInfo  = asio::UdpMulticastReceiver::SocketInfo;

    constexpr static inline const size_t MAX_TASKS_FUTURES_SIZE  = config::MAX_TASKS_SIZE;
    constexpr static inline const size_t MESSAGES_QUEUE_CAPACITY = MAX_TASKS_FUTURES_SIZE * 2;

  public:
    explicit FastMessageHandler (std::filesystem::path const& template_filepath);

    FastMessageHandler (std::filesystem::path const& template_filepath,
                        MessageHandlerF message_handler, MessageErrorHandlerF error_handler);

    ~FastMessageHandler ();

    /// Get error message from the last failed template load
    constexpr std::string const& get_error () const
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

    // ------------------------------------------------------------------
    // Utilities
    // ------------------------------------------------------------------

    /// Message namespace
    static std::string local_namespace (std::string const& multicast_group, uint16_t port);

    /// Convert a FastFieldValue to string representation
    static std::string value_to_string (const FastFieldValue& value);

  private:
    // Packets decoder multiplexer thread entry point
    void tasks_multiplexer (std::stop_token stoken);
    // Consumer thread entry point
    void messages_consumer (std::stop_token stoken);

    // Called from the UDP receive thread for each packet
    void on_udp_packet (SocketInfo& si, size_t length);

    // Callbacks
    MessageHandler message_handler_;
    MessageErrorHandler error_handler_;

    // The only stop source for all threads / waits
    std::stop_source threads_stop_source_;

    // Decoder instance
    FastMessageDecoder decoder_;

    // The thread which handles future objects of decoding tasks
    std::condition_variable_any tasks_futures_cv_;
    // The thread which handles messages
    std::condition_variable_any messages_queue_cv_;

    mutable std::mutex tasks_futures_mutext_;
    using TaskFutureList = std::list<std::future<FastMessagePtr>>;
    TaskFutureList tasks_futures_;

    using MessagesQueue =
      boost::lockfree::spsc_queue<FastMessagePtr,
                                  boost::lockfree::capacity<MESSAGES_QUEUE_CAPACITY>>;
    MessagesQueue messages_queue_;

    std::jthread tasks_multiplex_thread_;
    std::jthread consumer_thread_;

    std::string last_error_;
};

} // namespace fast
