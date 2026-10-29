#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <thread>
#include <atomic>
#include <cstdint>
#include <tuple>
#include <chrono>

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>

namespace fast
{
class FastMessageHandler;
}

namespace asio
{
using namespace boost::asio;

using error_code = boost::system::error_code;

constexpr size_t MAX_PACKET_SIZE = 1500UL;

// ------------------------------------------------------------------
// UdpMulticastReceiver.
// Handles a bucket of UDP sockets doing a similar job connecting
// to different ports in order receive messages from multiple sources.
// ------------------------------------------------------------------

class UdpMulticastReceiver : boost::noncopyable,
                             public std::enable_shared_from_this<UdpMulticastReceiver>
{
    friend class fast::FastMessageHandler;

    struct SocketInfo
    {
        SocketInfo (ip::udp::socket&& s, std::string mgroup, uint16_t p)
          : socket{std::move(s)},
            multicast_group{std::move(mgroup)},
            port{p}
        {
        }

        SocketInfo (SocketInfo const&)           = delete;
        SocketInfo& operator=(SocketInfo const&) = delete;

        SocketInfo (SocketInfo&& other)
          : socket{std::move(other.socket)},
            sender_endpoint{std::move(other.sender_endpoint)},
            time{std::move(other.time)},
            multicast_group{std::move(other.multicast_group)},
            address{std::move(other.address)},
            port{other.port},
            buffer{std::move(other.buffer)}
        {
        }

        SocketInfo& operator=(SocketInfo&& other)
        {
            if (&other != this) {
                socket          = std::move(other.socket);
                sender_endpoint = std::move(other.sender_endpoint);
                time            = std::move(other.time);
                multicast_group = std::move(other.multicast_group);
                address         = std::move(other.address);
                port            = other.port;
                buffer          = std::move(other.buffer);
            }
            return *this;
        }

        constexpr bool operator==(SocketInfo const& other) const
        { return multicast_group == other.multicast_group && port == other.port; }

        constexpr std::string const& to_string () noexcept
        {
            if (address.empty()) address = multicast_group + ':' + std::to_string(port);
            return address;
        }

        ip::udp::socket socket;
        ip::udp::endpoint sender_endpoint;
        std::chrono::sys_time<std::chrono::microseconds> time;
        std::string multicast_group;
        std::string address;
        uint16_t port;
        std::array<char, MAX_PACKET_SIZE> buffer;
    };

  public:
    using DataHandler =
      std::function<void(SocketInfo&, uint16_t feed_group, std::size_t length)>;

    UdpMulticastReceiver (io_context& io_service, DataHandler& handler, uint16_t feed_group,
                          size_t expected_sockets = 1)
      : io_service_{io_service},
        handler_{handler},
        receiving_{false},
        feed_group_(feed_group)
    {
        if (expected_sockets > 1) sockets_.reserve(expected_sockets);
    }

    UdpMulticastReceiver (UdpMulticastReceiver&& other)
      : io_service_{other.io_service_}, // transfer references
        handler_{other.handler_},
        sockets_{std::move(other.sockets_)},
        receiving_{other.receiving_.exchange(false, std::memory_order_relaxed)},
        feed_group_{other.feed_group_}
    {
    }

    // Start listening on a multicast group and port
    bool listen (std::string multicast_group, uint16_t port);

    constexpr bool already_listen (std::string const& multicast_group, uint16_t port) const
    {
        std::lock_guard lk{sockets_mutex_};
        return std::ranges::any_of(sockets_, [&multicast_group, port] (auto const& si) {
            return si.multicast_group == multicast_group && si.port == port;
        });
    }

    void start ();
    void stop ();

    constexpr size_t size () const noexcept
    {
        std::lock_guard lk{sockets_mutex_};
        return sockets_.size();
    }

  private:
    void do_receive (SocketInfo&);
    void handle_receive (SocketInfo& s, std::size_t bytes_transferred,
                         boost::system::error_code const& error);

    io_context& io_service_;
    DataHandler& handler_;
    std::vector<SocketInfo> sockets_;
    mutable std::mutex sockets_mutex_;
    std::atomic_bool receiving_;
    const uint16_t feed_group_;
};

// ------------------------------------------------------------------
// UdpMulticastManager.
// Manages a set of receivers (UdpMulticastReceiver), each set's element
// is a bunch of listeners that are bound to different mulicast groups
// and ports, but serving the similar data feeds (feed_group).
// ------------------------------------------------------------------

class UdpMulticastManager : boost::noncopyable
{
    enum class Running { True, False, Init };
    // receiver class: std::shared_ptr<UdpMulticastReceiver>
    // feed group: uint16_t
    // running: std::atomic<Running>
    using UdpMulticastReceiverPtr = std::shared_ptr<UdpMulticastReceiver>;
    using MulticastReceiverFeedGroup =
      std::tuple<uint16_t, UdpMulticastReceiverPtr, std::atomic<Running>>;

  public:
    using DataHandler = UdpMulticastReceiver::DataHandler;

    explicit UdpMulticastManager (DataHandler handler);

    ~UdpMulticastManager ();

    // Add a receiver of multicast group to listen on into feed_group
    bool add_receiver (uint16_t feed_group, std::string multicast_group, uint16_t port,
                       size_t expected_sockets = 1);

    // Start receivers
    void start (std::optional<uint16_t> feed_group = std::nullopt);

    // Stop receivers and shut down
    void stop (std::optional<uint16_t> feed_group = std::nullopt);

    // Get the io_context for external use (e.g. for posting work)
    constexpr io_context& io_service ()
    { return io_service_; }

    // Check if feed_group is running in manager
    constexpr bool is_group_running (uint16_t feed_group) const
    {
        auto pred = [feed_group] (std::shared_ptr<MulticastReceiverFeedGroup> const& rg) {
            using enum Running;
            if (!rg) return false;
            return std::get<uint16_t>(*rg) == feed_group &&
                   True == std::get<std::atomic<Running>>(*rg).load(std::memory_order_relaxed);
        };
        return std::ranges::find_if(receivers_, std::move(pred)) != receivers_.end();
    }

  private:
    using receivers_vector   = std::vector<std::shared_ptr<MulticastReceiverFeedGroup>>;
    using receivers_iterator = receivers_vector::iterator;

    constexpr bool has_feed_group (uint16_t feed_group)
    { return find_feed_group(feed_group) != receivers_.end(); }

    constexpr receivers_iterator find_feed_group (uint16_t feed_group)
    {
        auto pred = [feed_group] (std::shared_ptr<MulticastReceiverFeedGroup> const& rg) {
            if (!rg) return false;
            return std::get<uint16_t>(*rg) == feed_group;
        };
        return std::ranges::find_if(receivers_, std::move(pred));
    }

    DataHandler handler_;
    io_context io_service_;
    std::atomic<Running> io_running_;
    any_io_executor tracked_executor_;
    receivers_vector receivers_;
    std::vector<std::jthread> threads_;
};

} // end of namesapce asio
