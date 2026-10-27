#include <cstdint>
#include <string>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h> // Для getopt
#include <boost/program_options.hpp>
#include <boost/asio.hpp>
#include "config.h"

struct CommandArgs
{
    std::string input_file;
};

int get_args (int argc, char** argv, CommandArgs& args)
{
    constexpr const char* optstring = ":ab:h"; // -a, -b с аргументом, -h (помощь)
    int opt;
    // If optstring has a leading ‘:’ then a missing option argument causes ‘:’ to
    // be returned instead of ‘?’
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
        case 'a':
            std::cout << "Опция -a указана\n";
            break;
        case 'b':
            std::cout << "Опция -b с аргументом: " << optarg << "\n";
            break;
        case 'h':
            std::cout << "Помощь: ...\n";
            return 0;
        case ':':
            std::cerr << "У опции отсутствует аргумент\n";
            return 1;
        case '?':
            std::cerr << "Неверная опция\n";
            return 1;
        default:
            std::cerr << "Неизвестная опция\n";
            return 1;
        }
    }

    // optind points to a first non parsed arg in a command line
    for (int i = optind; i < argc; ++i) {
        std::cout << "Остаточный аргумент: " << argv[i] << "\n";
    }

    return 0;
}

// Using Boost Program_options to get command line args.
constexpr const char* DEFAULT_MULTICAST_GROUP = "239.255.0.1"; // multicast group (D class)
constexpr const unsigned short DEFAULT_MULTICAST_PORT = 15001; // port

// Pack a struct to avoid alignment
#pragma pack(push, 1)
struct DataPacket
{
    uint32_t sequence_number; // integer - sequence number
    uint8_t data[1400];       // unsigned char - binary data
};
#pragma pack(pop)

int main (int argc, char** argv)
{
    auto opt_vm = program_options::try_get_options(argc, argv);
    if (opt_vm.second.empty() || !opt_vm.first) return 1;

    decltype(auto) vm = *std::move(opt_vm);

    bool verbose = vm["verbose"].as<bool>();
    int count = vm["count"].as<int>();

    // ---- Network settings ----
    const std::string address = vm["address"].as<std::string>();      // multicast group (D class)
    const unsigned short port = vm["port"].as<unsigned short>();      // port
    const std::string input_filename = vm["input"].as<std::string>(); // data file
    std::optional<std::string> output_path;
    if (vm.count("output")) output_path.emplace(vm["output"].as<std::string>());
    // ------------------------

    std::cout << "[Инициализация] Чтение файла: " << input_filename << std::endl;
    std::ifstream infile(input_filename);
    if (!infile.is_open()) {
        std::cerr << "[Ошибка] Не удалось открыть файл " << input_filename << std::endl;
        std::cerr << "[Инфо] Пожалуйста, создайте файл '" << input_filename
                  << "' в рабочей директории и наполните его строками." << std::endl;
        return 1;
    }

    try {

        // Asio input/output context
        boost::asio::io_context io_context;
        // multicast group send to
        boost::asio::ip::address_v4 multicast_address = boost::asio::ip::make_address_v4(address);
        // network destination (Multicast group)
        boost::asio::ip::udp::endpoint multicast_endpoint{multicast_address, port};
        // create an UDP socket (IPv4)
        boost::asio::ip::udp::socket socket{io_context, multicast_endpoint.protocol()};
        // socket.bind(boost::asio::ip::udp::endpoint{boost::asio::ip::make_address_v4("192.168.0.19"),
        // port});

        // boost::asio::ip::address_v4 local_interface = boost::asio::ip::address_v4::any();
        // boost::asio::ip::address_v4 local_interface =
        // boost::asio::ip::make_address_v4("127.0.0.1");
        // boost::asio::ip::multicast::outbound_interface interface_option(local_interface);
        // socket.set_option(interface_option);

        // boost::asio::ip::multicast::enable_loopback loopback_option(true);
        // socket.set_option(loopback_option);

        // boost::asio::ip::multicast::hops hops_option(1);
        // socket.set_option(hops_option);

        std::cout << "[Сеть] UDP сокет открыт. Готов к отправке на " << multicast_address << ':'
                  << port << ':' << std::endl;

        std::string line;
        uint32_t seq_num = 1;

        // Построчно читаем файл
        while (std::getline(infile, line)) {
            if (line.empty()) continue;

            // make a data payload
            DataPacket packet{};
            packet.sequence_number = seq_num;

            // fillin an output buffer, unsigned char (uint8_t), cat a line if exceeds
            // the packet's data buffer length
            size_t copy_len = std::min(line.size(), sizeof(packet.data));
            std::memcpy(packet.data, line.c_str(), copy_len);

            // send the payload using Asio
            socket.send_to(boost::asio::buffer(&packet, sizeof(packet)), multicast_endpoint);

            std::cout << "[Отправлено] Пакет #" << packet.sequence_number << " | Текст: " << line
                      << " (" << sizeof(packet) << " байт)" << std::endl;

            seq_num++;

            // don't stress the network
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        std::cout << "[Готово] Все строки из файла успешно отправлены в сеть." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
