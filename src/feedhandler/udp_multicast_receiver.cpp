#include "udp_multicast_receiver.h"

#include <iostream>
#include <thread>

namespace {

constexpr std::size_t receive_buffer_size = 65535;

} // namespace

// ------------------------------------------------------------------
// UdpMulticastReceiver
// ------------------------------------------------------------------

UdpMulticastReceiver::UdpMulticastReceiver(net::io_context& io_context,
                                           DataHandler handler)
    : io_context_(io_context)
    , handler_(std::move(handler))
    , running_(false)
{
}

UdpMulticastReceiver::~UdpMulticastReceiver()
{
    stop();
}

bool UdpMulticastReceiver::listen(const std::string& multicast_group,
                                  uint16_t port)
{
    if (running_) {
        std::cerr << "[Receiver] Already running\n";
        return false;
    }

    try {
        // Create UDP socket
        auto socket_info = std::make_shared<SocketInfo>();
        socket_info->socket =
            std::make_unique<net::ip::udp::socket>(io_context_);
        socket_info->multicast_group = multicast_group;
        socket_info->port = port;
        socket_info->buffer.resize(receive_buffer_size);

        // Bind to the multicast port on any interface
        net::ip::udp::endpoint endpoint(
            net::ip::address_v4::any(), port);

        socket_info->socket->open(endpoint.protocol());
        socket_info->socket->set_option(
            net::ip::udp::socket::reuse_address(true));
        socket_info->socket->bind(endpoint);

        // Join the multicast group
        net::ip::address multicast_address = net::ip::make_address(multicast_group);
        net::ip::multicast::join_group join_option(multicast_address);
        socket_info->socket->set_option(join_option);

        sockets_.push_back(std::move(socket_info));

        running_ = true;

        // Start asynchronous receive
        for (auto& info : sockets_) {
            do_receive(info->multicast_group, info->port);
        }

        std::cout << "[Receiver] Listening on multicast "
                  << multicast_group << ":" << port << "\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[Receiver] Error: " << e.what() << "\n";
        return false;
    }
}

void UdpMulticastReceiver::stop()
{
    running_ = false;

    for (auto& info : sockets_) {
        try {
            if (info->socket && info->socket->is_open()) {
                info->socket->cancel();
                info->socket->close();
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[Receiver] Stop error: " << e.what() << "\n";
        }
    }
    sockets_.clear();
}

bool UdpMulticastReceiver::is_running() const
{
    return running_;
}

void UdpMulticastReceiver::do_receive(const std::string& multicast_group,
                                      uint16_t port)
{
    for (auto& info : sockets_) {
        if (info->socket && info->socket->is_open()) {
            net::ip::udp::endpoint sender_endpoint;
            info->socket->async_receive_from(
                net::buffer(info->buffer),
                sender_endpoint,
                [this, info, multicast_group, port,
                 sender_endpoint](
                    const boost::system::error_code& error,
                    std::size_t bytes_transferred) {
                    handle_receive(multicast_group, port, error,
                                   bytes_transferred, info->buffer);
                });
        }
    }
}

void UdpMulticastReceiver::handle_receive(
    const std::string& multicast_group,
    uint16_t port,
    const boost::system::error_code& error,
    std::size_t bytes_transferred,
    const std::vector<char>& buffer)
{
    if (error) {
        if (error != net::error::operation_aborted) {
            std::cerr << "[Receiver] Receive error: "
                      << error.message() << "\n";
        }
        return;
    }

    if (handler_) {
        handler_(multicast_group, port,
                 buffer.data(), bytes_transferred);
    }

    // Post the next receive operation
    do_receive(multicast_group, port);
}

// ------------------------------------------------------------------
// UdpMulticastManager
// ------------------------------------------------------------------

UdpMulticastManager::UdpMulticastManager(DataHandler handler)
    : handler_(std::move(handler))
    , io_context_(std::make_unique<net::io_context>())
    , running_(false)
{
}

UdpMulticastManager::~UdpMulticastManager()
{
    stop();
}

bool UdpMulticastManager::add_receiver(const std::string& multicast_group,
                                       uint16_t port)
{
    if (running_) {
        std::cerr << "[Manager] Cannot add receiver while running\n";
        return false;
    }

    auto receiver = std::make_unique<UdpMulticastReceiver>(
        *io_context_, handler_);

    if (!receiver->listen(multicast_group, port)) {
        return false;
    }

    receivers_.push_back(std::move(receiver));
    return true;
}

void UdpMulticastManager::start()
{
    if (running_) {
        std::cerr << "[Manager] Already running\n";
        return;
    }

    running_ = true;

    // Determine number of threads
    unsigned int num_threads =
        std::max(1u, std::thread::hardware_concurrency());

    // Create worker threads
    for (unsigned int i = 0; i < num_threads; ++i) {
        threads_.push_back(std::make_unique<std::thread>(
            [this]() {
                try {
                    io_context_->run();
                }
                catch (const std::exception& e) {
                    std::cerr << "[Manager] Thread error: "
                              << e.what() << "\n";
                }
            }));
    }

    std::cout << "[Manager] Started with " << num_threads
              << " thread(s)\n";
}

void UdpMulticastManager::stop()
{
    if (!running_) return;

    running_ = false;

    // Stop all receivers
    for (auto& receiver : receivers_) {
        receiver->stop();
    }

    // Stop the io_context
    try {
        io_context_->stop();
    }
    catch (...) {
    }

    // Join all threads
    for (auto& thread : threads_) {
        if (thread->joinable()) {
            thread->join();
        }
    }

    threads_.clear();
    receivers_.clear();
}

net::io_context& UdpMulticastManager::io_context()
{
    return *io_context_;
}

bool UdpMulticastManager::is_running() const
{
    return running_;
}
