#include <iostream>
#include <sstream>
#include <map>
#include <chrono>
#include "fast_message_handler.h"
#include "spin_wait.h"
#include "config.h"

namespace fast
{

// ============================================================================
// FastMessageHandler
// ============================================================================
FastMessageHandler::FastMessageHandler (std::filesystem::path const &template_filepath)
  : workers_{config::WORKERS_THREADS},
    decoder_{template_filepath, threads_stop_source_},
    packets_queue_{PACKET_BUFFERS_QUEUE_INITIAL_CAPACITY},
    tasks_futures_{FUTURE_QUEUE_INITIAL_CAPACITY}
{
    using namespace config;
    if (cfg->has_message_preamble) packet_sequencing_.emplace();

    // the use of stop_source is mandatory due jthread's static assertions,
    // if the threads_stop_source_ issues a stop request all the below threads
    // will be stopped.
    packet_serialiser_thread_ = std::jthread(&FastMessageHandler::packet_serialiser, this,
                                             threads_stop_source_.get_token());
    tasks_multiplex_thread_   = std::jthread{&FastMessageHandler::tasks_multiplexer, this,
                                             threads_stop_source_.get_token()};
    consumer_thread_          = std::jthread{&FastMessageHandler::messages_consumer, this,
                                             threads_stop_source_.get_token()};
}

FastMessageHandler::FastMessageHandler (std::filesystem::path const &template_filepath,
                                        MessageHandlerF message_handler,
                                        MessageErrorHandlerF error_handler)
  : FastMessageHandler(template_filepath)
{
    // callbacks are registered and templates are loaded.
    message_handler_ = std::make_shared<MessageHandlerF>(std::move(message_handler));
    error_handler_   = std::make_shared<MessageErrorHandlerF>(std::move(error_handler));
}

FastMessageHandler::~FastMessageHandler ()
{ threads_stop_source_.request_stop(); }

// --------------------------------------------------------------------------
// Callback registration
// --------------------------------------------------------------------------

void FastMessageHandler::on_message (MessageHandlerF handler, bool reset)
{
    if (!message_handler_) {
        message_handler_ = std::make_shared<MessageHandlerF>(std::move(handler));
    } else if (reset) {
        auto new_handler = std::make_shared<MessageHandlerF>(std::move(handler));
        std::atomic_store_explicit(&message_handler_, new_handler, std::memory_order_relaxed);
    }
}

void FastMessageHandler::on_error (MessageErrorHandlerF handler, bool reset)
{
    if (!error_handler_) {
        error_handler_ = std::make_shared<MessageErrorHandlerF>(std::move(handler));
    } else if (reset) {
        auto new_handler = std::make_shared<MessageErrorHandlerF>(std::move(handler));
        std::atomic_store_explicit(&error_handler_, new_handler, std::memory_order_relaxed);
    }
}

// --------------------------------------------------------------------------
// Data handler for UdpMulticastManager, signature:
//  std::function<void(SocketInfo&, uint16_t feed_group, std::size_t length)>.
// Called by UdpMultiCastManger from UDP receiving threads when a packet becomes
// available in asio async handler. No synchronization performed.
// --------------------------------------------------------------------------

FastMessageHandler::DataHandler FastMessageHandler::get_data_handler ()
{
    return [this] (SocketInfo &si, uint16_t feed_group, std::size_t length) {
        on_udp_packet(si, feed_group, length);
    };
}

// Serializes incoming packets in un-ordered queue to allow receiving threads
// not to wait until messages become processed.
void FastMessageHandler::on_udp_packet (SocketInfo &si, uint16_t feed_group, size_t length)
{
    PacketBuffer buffer{Buffer{si.buffer.data(), length},
                        local_namespace(si.multicast_group, si.port),
                        si.time.time_since_epoch().count(), feed_group};
    misc::spin_wait(
      [this, v = std::move(buffer)] mutable {
          return packets_queue_.push(std::make_unique<PacketBuffer>(std::move(v)).release());
      },
      threads_stop_source_, 10);
    packets_cv_.notify_all();
}

void FastMessageHandler::on_recovery_packet (PacketBuffer buffer)
{
    misc::spin_wait(
      [this, v = std::move(buffer)] mutable {
          return packets_queue_.push(std::make_unique<PacketBuffer>(std::move(v)).release());
      },
      threads_stop_source_, 10);
    packets_cv_.notify_all();
}

void FastMessageHandler::packet_serialiser (std::stop_token stoken)
{
    std::mutex mutex;
    std::unique_lock lk{mutex};
    while (!stoken.stop_requested()) {
        packets_cv_.wait(lk, stoken, [this] {
            return packets_queue_.consume_all([this] (auto *pbuf) {
                on_buffered_packet(std::move(*pbuf));
                delete pbuf;
            }) != 0;
        });
    }
    packets_queue_.consume_all([] (auto *pbuf) { delete pbuf; });
}

// --------------------------------------------------------------------------
// UDP packet processing runs by Asio worker thread.
// Arranges a decoder task, stores task's future to list and starts async task.
// --------------------------------------------------------------------------

void FastMessageHandler::on_buffered_packet (PacketBuffer packet_buffer)
{
    std::optional<PacketSequencing::BuffersMap> buffers;

    // Some exchanges provide a message preamble contained a look-ahead data
    // before a FAST message body. If it a case the packet_sequencing_ contains an
    // accepter / look-ahead handler.
    if (packet_sequencing_) {
        // Each of feed groups maintains it's own message acceptor
        // handling a sequence numbers only for that group.
        auto recovery_fn = [this] (auto &&pb) { on_recovery_packet(std::move(pb)); };
        buffers = packet_sequencing_->at(packet_buffer.feed_group_)(packet_buffer,
                                                                    std::move(recovery_fn));
    } else {
        buffers = {{0, std::move(packet_buffer.buffer_)}};
    }

    /**
     * TEST:
     * packet_buffer.buffer_ -= sizeof(uint32_t);
     * buffers = {{0, std::move(packet_buffer.buffer_)}};
     */

    if (!buffers) return;

    for (auto &[_, buffer] : *buffers) {
        if (!buffer) {
            // error: FAST part is missing in packet
            continue;
        }

        // Prepare the packet data buffer to submit to a decoder task arranged within a
        // threads pool.
        using clock = std::chrono::system_clock;
        auto future = workers_.enqueue(
          [this] (std::string local_namespace, clock::rep time_us, Buffer buffer) {
              std::string err;
              if (auto msg = decoder_.try_decode(buffer, config::cfg->dictionary_reset, err)) {
                  msg->local_namespace_name = std::move(local_namespace);
                  msg->time_us              = time_us;
                  return std::make_shared<FastMessage>(*std::move(msg));
              } else {
                  // Log decode error
                  if (!err.empty()) {
                      auto handler =
                        std::atomic_load_explicit(&error_handler_, std::memory_order_relaxed);
                      if (handler && *handler) { (*handler)(local_namespace, err); }
                  }
                  return FastMessagePtr{};
              }
          },
          packet_buffer.local_namespace_, packet_buffer.time_us_, std::move(buffer));

        if (future.valid()) {
            // Store a task return state in the tasks_futures_ if it possible,
            // wait a little if exhausted or return on stop.
            misc::spin_wait(
              [this, f = std::move(future)] mutable {
                  return tasks_futures_.push(std::move(f));
              },
              threads_stop_source_, 1);

            tasks_futures_cv_.notify_all();
        }
    }
}

// --------------------------------------------------------------------------
// Collects fast messages as a ready future result from the list of tasks futures.
// Transfer valid (not null) messages to the consumer query.
// --------------------------------------------------------------------------

void FastMessageHandler::tasks_multiplexer (std::stop_token stoken)
{
    std::mutex mutex;
    std::unique_lock lk{mutex};

    std::vector<std::future<FastMessagePtr>> futures;
    const size_t consume_count = workers_.size() * 2;
    futures.reserve(consume_count);

    auto enque_message = [this, stoken] (FastMessagePtr mp) {
        if (mp) {
            misc::spin_wait(
              [this, m = std::move(mp)] mutable { return messages_queue_.push(std::move(m)); },
              stoken, 10);
            messages_queue_cv_.notify_all();
        }
    };

    while (!stoken.stop_requested()) {
        using namespace std::chrono_literals;

        tasks_futures_cv_.wait(lk, stoken, [this, &futures] {
            return tasks_futures_.read_available() > 0 || !futures.empty();
        });

        for (auto front = futures.begin(); front != futures.end();) {
            if (front->wait_for(1ns) == std::future_status::ready) {
                if (front->valid()) enque_message(std::move(front->get()));
                front = futures.erase(front);
            } else {
                ++front;
            }
        }

        for (size_t i = 0; i < consume_count; ++i) {
            const bool consumed =
              tasks_futures_.consume_one([&futures, &enque_message] (auto &&f) {
                  if (f.wait_for(1ns) == std::future_status::ready) {
                      if (f.valid()) enque_message(std::move(f.get()));
                  } else {
                      futures.push_back(std::move(f));
                  }
              });

            if (!consumed) break;
        }
    }
}

// --------------------------------------------------------------------------
// Consumer thread (single thread reads from queue and dispatches)
// --------------------------------------------------------------------------

void FastMessageHandler::messages_consumer (std::stop_token stoken)
{
    while (!stoken.stop_requested()) {
        const bool consumed = messages_queue_.consume_one([this] (FastMessagePtr message) {
            // We have managed that the queue holds only valid pointers.
            // Dispatch to the user callback
            auto handler =
              std::atomic_load_explicit(&message_handler_, std::memory_order_relaxed);
            if (handler && *handler) {
                try {
                    (*handler)(message);
                }
                catch (std::exception const &e) {
                    auto handler =
                      std::atomic_load_explicit(&error_handler_, std::memory_order_relaxed);
                    // User callback threw — log and continue
                    if (handler && *handler) {
                        (*handler)(message->local_namespace_name,
                                   std::string{"Message handler error: "} + e.what());
                    }
                }
                catch (...) {
                    auto handler =
                      std::atomic_load_explicit(&error_handler_, std::memory_order_relaxed);
                    if (handler && *handler) {
                        (*handler)(message->local_namespace_name,
                                   "Message handler caught unknown exception");
                    }
                }
            };
        });

        if (!consumed) {
            std::mutex wait_mutex;
            std::unique_lock lk{wait_mutex};
            messages_queue_cv_.wait(lk, stoken,
                                    [this] { return messages_queue_.read_available() > 0; });
        }
    }

    // Drain remaining messages on shutdown
    messages_queue_.consume_all([this] (FastMessagePtr message) {
        if (message_handler_) {
            try {
                (*message_handler_)(message);
            }
            catch (...) {
                // Ignore exceptions during drain
            }
        }
    });
}

// --------------------------------------------------------------------------
// Utilities
// ============================================================================
std::string FastMessageHandler::local_namespace (std::string const &multicast_group,
                                                 uint16_t port)
{ return multicast_group + ':' + std::to_string(port); }

std::string FastMessageHandler::value_to_string (const FastFieldValue &value)
{
    std::ostringstream oss;
    boost::apply_visitor(
      [&] (auto &&v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, boost::blank>) {
              oss << "(absent)";
          } else if constexpr (std::is_same_v<T, int32_t>) {
              oss << v;
          } else if constexpr (std::is_same_v<T, int64_t>) {
              oss << v;
          } else if constexpr (std::is_same_v<T, uint32_t>) {
              oss << v;
          } else if constexpr (std::is_same_v<T, uint64_t>) {
              oss << v;
          } else if constexpr (std::is_same_v<T, double>) {
              oss << v;
          } else if constexpr (std::is_same_v<T, std::string>) {
              oss << '"' << v << '"';
          }
      },
      value);
    return oss.str();
}

} // namespace fast
