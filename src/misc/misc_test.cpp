#include <csignal>
#include "misc_test.h"

void signal_handler (int) { g_running = false; }

// ================================================================
// Пример: Парсинг XML
// ================================================================
void XMLParserTest (int argc, char **argv)
{
    constexpr const char *spectra_filepath = "doc/spectra-templates.xml";
    constexpr const char *asts_filepath = "doc/FIX50SP2-2025-Oct.xml";

    std::string filepath = spectra_filepath;
    if (argc > 1) {
        filepath = argv[1];
    }
    test_parse_xml_files(filepath);
}

// ================================================================
// Пример: UDP Multicast Receiver
// ================================================================
void test_udp_receiving ()
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    test_udp_multicast();
}
