#include "udp_multicast_receiver.h"
#include <iostream>
#include <format>

namespace asio
{

// ------------------------------------------------------------------
// UdpMulticastReceiver
// ------------------------------------------------------------------

bool UdpMulticastReceiver::listen (std::string multicast_group, uint16_t port)
{
    SocketInfo* P_si = nullptr;
    try {
        // Bind to the multicast port on any interface
        ip::udp::endpoint endpoint{ip::address_v4::any(), port};
        {
            // Create UDP socket
            std::lock_guard lk{sockets_mutex_};
            P_si = &sockets_.emplace_back(ip::udp::socket{io_service_, endpoint.protocol()},
                                          std::move(multicast_group), port);
        }
        P_si->socket.set_option(ip::udp::socket::reuse_address(true));
        P_si->socket.bind(endpoint);

        // Join the multicast group
        P_si->socket.set_option(
          ip::multicast::join_group(ip::make_address(P_si->multicast_group)));

        std::cout << "[Receiver] Listening on multicast " << P_si->to_string() << "\n";
        return true;
    }
    catch (const std::exception& e) {
        std::string err = ": ";
        if (P_si) {
            err += P_si->to_string() + ": ";
            std::lock_guard lk{sockets_mutex_};
            std::erase(sockets_, *P_si);
        }
        std::cerr << std::format("[Receiver] Error{:s}{:s}\n", err, e.what());
        return false;
    }
}

void UdpMulticastReceiver::start ()
{
    if (!receiving_.exchange(true, std::memory_order_relaxed))
        std::ranges::for_each(sockets_, [this] (auto& s) {
            do_receive(s);
            std::cout << std::format("[Receiver] Started receiver {:s}, Group #{:d}\n",
                                     s.to_string(), feed_group_);
        });
}

void UdpMulticastReceiver::stop ()
{
    receiving_.store(false, std::memory_order_relaxed);
    std::ranges::for_each(sockets_, [this] (auto& s) {
        std::cout << std::format("[Receiver] Stopping receiver {:s}, Group #{:d}\n",
                                 s.to_string(), feed_group_);
        do_receive(s);
    });
}

void UdpMulticastReceiver::do_receive (SocketInfo& s)
{
    if (receiving_.load(std::memory_order_relaxed))
        s.socket.async_receive_from(
          buffer(s.buffer, s.buffer.size()), s.sender_endpoint,
          [self = shared_from_this(), &s] (error_code const& error, size_t bytes_transferred) {
              self->handle_receive(s, bytes_transferred, error);
          });
    else
        std::cout << std::format("[Receiver] Stopped receiver {:s}, Group #{:d}\n",
                                 s.to_string(), feed_group_);
}

void UdpMulticastReceiver::handle_receive (SocketInfo& s, std::size_t bytes_transferred,
                                           error_code const& error)
{
    if (!error) {
        s.time = std::chrono::system_clock::now();
        handler_(s, feed_group_, bytes_transferred);
        // Post the next receive operation
        do_receive(s);
    } else {
        if (error == error::operation_aborted) {
            std::cout << "[Receiver] Socket read complete (shutdown).\n";
            // don't call do_receive again
        } else if (error == error::connection_refused || error == error::connection_reset) {
            // Ignoring non critical network level errors
            std::cerr << "[Receiver] A non critical network error: " << error.message()
                      << ". Continuing...\n";
            do_receive(s);
        } else {
            std::cerr << "[Receiver] Receive error: " << error.message() << "\n";
            // don't call do_receive again in case of critical errors, receiving will be
            // interrupted
        }
    }
}

// ------------------------------------------------------------------
// UdpMulticastManager.
// Manages a set of receivers (UdpMulticastReceiver), each set's element
// is a bunch of listeners that are bound to different mulicast groups
// and ports, but serving the similar data feeds.
// ------------------------------------------------------------------

UdpMulticastManager::UdpMulticastManager (DataHandler handler)
  : handler_{std::move(handler)},
    io_service_{},
    io_running_{Running::False},
    tracked_executor_{prefer(io_service_.get_executor(), execution::outstanding_work.tracked)}
{
}

UdpMulticastManager::~UdpMulticastManager ()
{ stop(); }

bool UdpMulticastManager::add_receiver (uint16_t feed_group, std::string multicast_group,
                                        uint16_t port, size_t expected_sockets)
{
    if (auto group = find_feed_group(feed_group); group != receivers_.end()) {
        auto& receiver = std::get<UdpMulticastReceiverPtr>(**group);
        // Check that the multicast-group/port combination does not exist
        if (receiver->already_listen(multicast_group, port)) {
            std::cerr << std::format(
              "[Manager] Cannot add receiver on {:s}:{:d}, Group #{:d}: Already exists\n",
              multicast_group, port, feed_group);
            return false;
        }
        // Add a listener to UdpMulticastReceiver
        if (!receiver->listen(multicast_group, port)) {
            std::cerr << std::format(
              "[Manager] Cannot add receiver on {:s}:{:d}, Group #{:d}\n", multicast_group,
              port, feed_group);
            return false;
        } else
            std::cout << std::format(
              "[Manager] Added new receiver on {:s}:{:d}, Group #{:d}\n", multicast_group,
              port, feed_group);
    } else {
        // Add a UdpMulticastReceiver configured for this manager and setup a first listener.
        using enum Running;
        if (!std::get<UdpMulticastReceiverPtr>(
               *receivers_.emplace_back(std::make_shared<MulticastReceiverFeedGroup>(
                 std::make_tuple(feed_group,
                                 std::make_shared<UdpMulticastReceiver>(
                                   io_service_, handler_, feed_group, expected_sockets),
                                 False))))
               ->listen(multicast_group, port)) {
            receivers_.pop_back();
            std::cerr << std::format(
              "[Manager] Cannot add receiver on {:s}:{:d}, Group #{:d}\n", multicast_group,
              port, feed_group);
            return false;
        } else {
            std::cout << std::format("[Manager] Added receiver on {:s}:{:d}, Group #{:d}\n",
                                     multicast_group, port, feed_group);
        }
    }
    return true;
}

void UdpMulticastManager::start (std::optional<uint16_t> feed_group)
{
    using enum Running;
    auto running = False;
    while (!io_running_.compare_exchange_strong(running, Init, std::memory_order_relaxed)) {
        if (running != True) io_running_.wait(running);
        if (True == io_running_.load(std::memory_order_relaxed)) {
            std::cout << "[Manager] Already running\n";
            break;
        }
        running = False;
    }

    if (receivers_.empty()) {
        std::cerr << "[Manager] Has no registered UDP Multicat receivers\n";
        io_running_.store(False, std::memory_order_relaxed);
        io_running_.notify_one();
        return;
    }

    if (Init == io_running_.load(std::memory_order_relaxed) && threads_.empty()) {
        // Determine number of threads
        struct Size
        {
            size_t sz{0};
            void operator()(receivers_vector::value_type const& rg)
            { sz += std::get<UdpMulticastReceiverPtr>(*rg)->size(); }
        };
        auto [_, num_threads] = std::ranges::for_each(receivers_, Size{});
        num_threads.sz =
          std::min(size_t(std::thread::hardware_concurrency()), num_threads.sz + 1);
        threads_.reserve(num_threads.sz);

        io_running_.store(True, std::memory_order_relaxed);
        io_running_.notify_all();

        // Create worker threads
        for (size_t i = 0; i < num_threads.sz; ++i)
            threads_.emplace_back([this] () { io_service_.run(); });

        std::cout << "[Manager] Started with " << num_threads.sz << " io_service thread(s)\n";
    }

    auto start = [] (MulticastReceiverFeedGroup& group) {
        std::get<std::atomic<Running>>(group).store(True, std::memory_order_relaxed);
        std::get<UdpMulticastReceiverPtr>(group)->start();
    };

    if (feed_group) {
        if (auto group = find_feed_group(*feed_group); group != receivers_.end())
            start(**group);
        else
            return;
    } else {
        std::ranges::for_each(receivers_, [&start] (auto& rg) { start(*rg); });
    }
}

void UdpMulticastManager::stop (std::optional<uint16_t> feed_group)
{
    using enum Running;
    auto stop = [] (MulticastReceiverFeedGroup& group) {
        std::get<UdpMulticastReceiverPtr>(group)->stop();
        std::get<std::atomic<Running>>(group).store(False, std::memory_order_relaxed);
    };

    if (feed_group) {
        if (auto group = find_feed_group(*feed_group); group != receivers_.end())
            stop(**group);
        else
            return;
    } else {
        if (io_running_.exchange(False, std::memory_order_relaxed) == True) {
            std::ranges::for_each(receivers_, [&stop] (auto& rg) { stop(*rg); });
            // Stop all receivers
            if (!io_service_.stopped()) io_service_.stop();

            threads_.clear();
            receivers_.clear();

            std::cout << "[Manager] Stopped io_service thread(s)\n";
        }
    }
}

} // end namespace asio
