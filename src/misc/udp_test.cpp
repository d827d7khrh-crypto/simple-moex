#include <string>
#include <iostream>
#include <chrono>
#include "udp_multicast_receiver.h"
#include "fast_message_handler.h"
#include "misc_test.h"

std::atomic<bool> g_running{false};

void test_udp_multicast ()
{
    // ==================================================================
    // 1. Create the FAST message handler and load templates
    // ==================================================================
    fast::FastMessageHandler handler;

    constexpr const char* spectra_filepath = "doc/spectra-templates.xml";
    if (!handler.load_templates_from_file(spectra_filepath)) {
        std::cerr << "Failed to load FAST templates: " << handler.get_error() << "\n";
        return;
    }

    std::cout << "FAST templates loaded successfully.\n";

    // ==================================================================
    // 2. Register message processing callback
    // ==================================================================
    handler.on_message([] (const std::string& multicast_group, uint16_t port,
                           const fast::FastMessage& msg) {
        std::cout << "[" << multicast_group << ":" << port << "] "
                  << "Template: " << msg.template_name << " (id=" << msg.template_id << ")\n";

        // Print top-level fields
        for (const auto& field : msg.fields) {
            std::cout << "    Field #" << field.tag_id << " " << field.name << " = "
                      << fast::FastMessageHandler::value_to_string(field.value) << "\n";
        }

        // Print sequences
        for (const auto& seq : msg.sequences) {
            std::cout << "    Sequence: " << seq.name << " (elements=" << seq.fields.size()
                      << ")\n";
            for (const auto& field : seq.fields) {
                std::cout << "      Field #" << field.tag_id << " " << field.name << " = "
                          << fast::FastMessageHandler::value_to_string(field.value) << "\n";
            }
        }
    });

    // Register error callback
    handler.on_error(
      [] (const std::string& multicast_group, uint16_t port, const std::string& error) {
          std::cerr << "[" << multicast_group << ":" << port << "] "
                    << "Decode error: " << error << "\n";
      });

    // ==================================================================
    // 3. Start the handler (spawns consumer thread)
    // ==================================================================
    handler.start();

    // ==================================================================
    // 4. Create UDP multicast manager and wire the FAST handler
    // ==================================================================
    UdpMulticastManager manager(handler.get_data_handler());

    // Add multicast groups to listen on
    // Standard multicast addresses: 224.0.0.0 – 239.255.255.255
    manager.add_receiver("239.255.1.1", 5555);
    manager.add_receiver("239.255.1.2", 5556);
    manager.add_receiver("239.255.1.3", 5557);

    manager.start();

    // ==================================================================
    // 5. Wait for stop signal
    // ==================================================================
    std::cout << "FAST multicast receiver running. Press Ctrl+C to stop.\n";
    g_running.store(true, std::memory_order_relaxed);
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Stopping...\n";
    manager.stop();
    handler.stop();
    std::cout << "Stopped.\n";
}
