#ifndef UDP_MULTICAST_RECEIVER_H
#define UDP_MULTICAST_RECEIVER_H

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/noncopyable.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <thread>
#include <atomic>

namespace net = boost::asio;
using tcp_acceptor = boost::asio::ip::tcp::acceptor;

class UdpMulticastReceiver : boost::noncopyable
{
public:
    using DataHandler = std::function<void(const std::string& multicast_group,
                                           uint16_t port,
                                           const char* data,
                                           std::size_t length)>;

    UdpMulticastReceiver(net::io_context& io_context,
                         DataHandler handler);

    ~UdpMulticastReceiver();

    // Start listening on a multicast group and port
    bool listen(const std::string& multicast_group, uint16_t port);

    // Stop all receivers
    void stop();

    // Check if the receiver is running
    bool is_running() const;

private:
    void do_receive(const std::string& multicast_group, uint16_t port);
    void handle_receive(const std::string& multicast_group,
                        uint16_t port,
                        const boost::system::error_code& error,
                        std::size_t bytes_transferred,
                        const std::vector<char>& buffer);

    net::io_context& io_context_;
    DataHandler handler_;

    struct SocketInfo
    {
        std::unique_ptr<net::ip::udp::socket> socket;
        std::string multicast_group;
        uint16_t port;
        std::vector<char> buffer;
    };

    std::vector<std::shared_ptr<SocketInfo>> sockets_;
    std::atomic<bool> running_;
};

class UdpMulticastManager : boost::noncopyable
{
public:
    using DataHandler = UdpMulticastReceiver::DataHandler;

    explicit UdpMulticastManager(DataHandler handler);

    ~UdpMulticastManager();

    // Add a multicast group to listen on
    bool add_receiver(const std::string& multicast_group, uint16_t port);

    // Start all receivers
    void start();

    // Stop all receivers and shut down
    void stop();

    // Get the io_context for external use (e.g. for posting work)
    net::io_context& io_context();

    // Check if manager is running
    bool is_running() const;

private:
    DataHandler handler_;
    std::vector<std::unique_ptr<UdpMulticastReceiver>> receivers_;
    std::unique_ptr<net::io_context> io_context_;
    std::vector<std::unique_ptr<std::thread>> threads_;
    std::atomic<bool> running_;
};

#endif // UDP_MULTICAST_RECEIVER_H
