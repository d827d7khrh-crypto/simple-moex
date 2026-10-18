#include "fix_fast_parser.h"
#include "udp_multicast_receiver.h"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

std::atomic<bool> g_running{true};

void signal_handler(int)
{
    g_running = false;
}

void test_parse_xml_files(std::string filepath);

int main(int argc, char** argv)
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ================================================================
    // Пример 1: UDP Multicast Receiver (раскомментируйте для запуска)
    // ================================================================
#if 1
    UdpMulticastManager::DataHandler data_handler =
        [](const std::string& multicast_group,
           uint16_t port,
           const char* data,
           std::size_t length) {
            std::cout << "[" << multicast_group << ":" << port << "] "
                      << "Received " << length << " bytes: "
                      << std::string(data, std::min(length, std::size_t(256)))
                      << (length > 256 ? "..." : "") << "\n";
        };

    UdpMulticastManager manager(data_handler);

    // Добавляем несколько мультикаст-групп
    // Стандартные мультикаст-адреса: 224.0.0.0 – 239.255.255.255
    manager.add_receiver("239.255.1.1", 5555);
    manager.add_receiver("239.255.1.2", 5556);
    manager.add_receiver("239.255.1.3", 5557);

    // Запускаем менеджер (создаёт потоки и начинает приём)
    manager.start();

    // Ждём сигнал остановки
    std::cout << "Multicast receiver running. Press Ctrl+C to stop.\n";
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Stopping...\n";
    manager.stop();
    std::cout << "Stopped.\n";
#endif

    // ================================================================
    // Пример 2: Парсинг XML (раскомментируйте для запуска)
    // ================================================================
#if 0
    constexpr const char * spectra_filepath = "../../doc/spectra-templates.xml";
    constexpr const char * asts_filepath = "../../doc/FIX50SP2-2025-Oct.xml";

    std::string filepath = spectra_filepath;
    if (argc > 1) {
        filepath = argv[1];
    }
    test_parse_xml_files(filepath);
#endif

    return 0;
}
