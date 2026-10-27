#include <iostream>
#include <boost/asio.hpp>

#pragma pack(push, 1)
struct DataPacket {
    int32_t sequence_number;
    uint8_t data[256];
};
#pragma pack(pop)

constexpr const char* DEFAULT_MULTICAST_GROUP = "239.255.0.1"; // multicast group (D class)
constexpr const unsigned short DEFAULT_MULTICAST_PORT = 15001;    // port

int main() {
    const std::string listen_address_str = "0.0.0.0";

    try {
        boost::asio::io_context io_context;

        // 1. Создаем сокет для IPv4
        boost::asio::ip::udp::socket socket(io_context);

        const boost::asio::ip::address listen_address = boost::asio::ip::make_address(listen_address_str);
        boost::asio::ip::udp::endpoint listen_endpoint(listen_address, DEFAULT_MULTICAST_PORT);
        
        socket.open(listen_endpoint.protocol());
        socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        socket.bind(listen_endpoint);
        
        // multicast group send to
        boost::asio::ip::address_v4 multicast_address = boost::asio::ip::make_address_v4(DEFAULT_MULTICAST_GROUP);
        socket.set_option(boost::asio::ip::multicast::join_group(multicast_address));

        std::cout << "[Приемник] Слушаю мультикаст " << multicast_address << ":" << DEFAULT_MULTICAST_PORT << "..." << std::endl;

        while (true) {
            DataPacket packet{};
            boost::asio::ip::udp::endpoint sender_endpoint;

            // Блокирующий вызов чтения пакета из сети
            size_t reply_length = socket.receive_from(
                boost::asio::buffer(&packet, sizeof(packet)), sender_endpoint
            );

            std::cout << "[Принято] От: " << sender_endpoint.address().to_string() 
                      << " | Пакет #" << packet.sequence_number 
                      << " | Текст: " << reinterpret_cast<char*>(packet.data) 
                      << " (" << reply_length << " байт)" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "[Исключение Приемника] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
