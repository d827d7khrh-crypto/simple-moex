#include <iostream>
#include <sstream>
#include <chrono>
#include "fast_message_handler.h"
#include "config.h"

namespace fast
{

// ============================================================================
// FastMessageHandler
// ============================================================================
FastMessageHandler::FastMessageHandler (std::filesystem::path const& template_filepath)
  : workers_{config::WORKERS_THREADS},
    decoder_{template_filepath, threads_stop_source_}
{
    // the use of stop_source is mandatory due jthread's static assertions,
    // if the threads_stop_source_ issues a stop request all the below threads
    // will be stopped.
    tasks_multiplex_thread_ = std::jthread{&FastMessageHandler::tasks_multiplexer, this,
                                           threads_stop_source_.get_token()};
    consumer_thread_        = std::jthread{&FastMessageHandler::messages_consumer, this,
                                           threads_stop_source_.get_token()};
}

FastMessageHandler::FastMessageHandler (std::filesystem::path const& template_filepath,
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
// Data handler for UdpMulticastManager
//  - std::function<void(SocketInfo&, uint16_t feed_group, std::size_t length)>
// --------------------------------------------------------------------------

FastMessageHandler::DataHandler FastMessageHandler::get_data_handler ()
{
    return [this] (SocketInfo& si, uint16_t feed_group, std::size_t length) {
        on_udp_packet(si, length);
    };
}

// --------------------------------------------------------------------------
// UDP packet processing runs by Asio worker thread.
// Arranges a decoder task, stores task's future to list and starts async task.
// --------------------------------------------------------------------------

void FastMessageHandler::on_udp_packet (SocketInfo& si, size_t length)
{
    std::cout << std::format("[on_udp_packet] {:s}, length={:d}\n", si.to_string(), length);

    const char* P_data = si.buffer.data();
    uint32_t seq_no    = 0;
    if (config::cfg->exchange == config::MOEX_EXCHANGE) {
        // MOEX: Packet seq_no
        if (length >= sizeof(uint32_t)) {
            seq_no = *((uint32_t*)P_data);
            P_data += sizeof(uint32_t);
            length -= sizeof(uint32_t);
        }
    }

    if (length == 0) return;

    // Submit the packet data buffer to a decoder task.
    std::shared_ptr<char[]> data = std::make_shared_for_overwrite<char[]>(length);
    std::memcpy(data.get(), P_data, length);
    // Submit an async task to thread pool.
    auto future = workers_.enqueue(
      [this] (std::string local_namespace, std::chrono::system_clock::rep time_us,
              std::shared_ptr<char[]> data, size_t length) {
          std::string err;
          if (auto msg = decoder_.try_decode(data.get(), length,
                                             config::cfg->template_dictionary_reset, err)) {
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
      local_namespace(si.multicast_group, si.port), si.time.time_since_epoch().count(), data,
      length);

    if (future.valid()) {
        do {
            // Store a task return state in the tasks_futures_ if it possible,
            // wait a little if exhausted or return on stop.
            std::lock_guard lk{tasks_futures_mutext_};
            if (tasks_futures_.size() < config::WORKERS_THREADS * 2) {
                tasks_futures_.push_back(std::move(future));
                break;
            }

            if (threads_stop_source_.stop_requested()) {
                tasks_futures_cv_.notify_all();
                return;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1us);
        } while (true);

        tasks_futures_cv_.notify_all();
    }
}

// --------------------------------------------------------------------------
// Collects fast messages as a ready future result from the list of tasks futures.
// Transfer valid (not null) messages to the consumer query.
// --------------------------------------------------------------------------

void FastMessageHandler::tasks_multiplexer (std::stop_token stoken)
{
    using namespace std::chrono_literals;
    std::unique_lock lk{tasks_futures_mutext_, std::defer_lock};

    while (!stoken.stop_requested()) {
        std::optional<std::future<FastMessagePtr>> fut;
        lk.lock();
        if (!tasks_futures_.empty()) {
            auto front = std::move(tasks_futures_.front());
            tasks_futures_.pop_front();
            if (front.wait_for(1us) == std::future_status::ready)
                fut.emplace(std::move(front));
            else
                tasks_futures_.push_back(std::move(front));
        } else {
            std::cout << std::format("[tasks_multiplexer] start waiting for tasks_futures_\n");
            tasks_futures_cv_.wait(lk, stoken, [this] { return !tasks_futures_.empty(); });
        }
        lk.unlock();

        if (fut) {
            // ensure we put messages in queue are only valid (not null) pointers
            if (FastMessagePtr message{std::move(fut->get())}) {
                while (!messages_queue_.push(message)) {
                    if (stoken.stop_requested()) break;
                    std::this_thread::sleep_for(10us);
                }
                messages_queue_cv_.notify_all();
            }
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
                    (*handler)(*message);
                }
                catch (std::exception const& e) {
                    auto handler =
                      std::atomic_load_explicit(&error_handler_, std::memory_order_relaxed);
                    // User callback threw — log and continue
                    if (handler && *handler) {
                        (*handler)(message->namespace_name,
                                   std::string{"Message handler error: "} + e.what());
                    }
                }
                catch (...) {
                    auto handler =
                      std::atomic_load_explicit(&error_handler_, std::memory_order_relaxed);
                    if (handler && *handler) {
                        (*handler)(message->namespace_name,
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
                (*message_handler_)(*message);
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
std::string FastMessageHandler::local_namespace (std::string const& multicast_group,
                                                 uint16_t port)
{ return multicast_group + ':' + std::to_string(port); }

std::string FastMessageHandler::value_to_string (const FastFieldValue& value)
{
    std::ostringstream oss;
    boost::apply_visitor(
      [&] (auto&& v) {
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
