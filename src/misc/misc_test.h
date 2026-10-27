#include <string>
#include <atomic>

extern std::atomic<bool> g_running;

void test_parse_xml_files (std::string filepath);
void test_udp_multicast ();
