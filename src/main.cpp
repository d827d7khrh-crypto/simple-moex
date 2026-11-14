#include <iostream>
#include <string>
#include <unordered_map>
#include <csignal>
#include <format>
#include "udp_multicast_receiver.h"
#include "fast_message_handler.h"
#include "save_file_message_handler.h"
#include "do_nothing_message_handler.h"
#include "config.h"
#include "strings.h"

int main (int argc, char** argv)
{
    auto opt_result = program_options::try_get_options(argc, argv);
    if (!opt_result.second.empty()) {
        std::cerr << opt_result.second << std::endl;
        return 1;
    }

    auto vm = *std::move(opt_result.first);
    if (vm.count("help")) {
        std::cout << std::format("Usage: app [options]\n{}", program_options::to_string());
        return 0;
    }

    using namespace config;

    ConfigParser parser(vm);
    auto [cf, err] = parser.try_parse();
    if (!err.empty()) {
        std::cerr << "Config error: " << err << "\n";
        return 1;
    }

    if (!cf) {
        std::cout << "Not using any configuration file\n";
        cfg = std::make_unique<AppConfig>();
    } else {
        cfg = std::make_unique<AppConfig>(*std::move(cf));
    }

    std::cout << cfg->print();

    std::unique_ptr<fast::FastMessageHandler> message_handler;
    try {
        auto template_filepath = cfg->template_dir_path / cfg->template_file_name;
        message_handler        = std::make_unique<fast::FastMessageHandler>(template_filepath);

        // Messages handler to save all FIX structures to files
        // message_handler->on_message(SaveFileMessageHandler{});

        // Message hadler to just output timestamps
        message_handler->on_message(DoNothingMessageHandler{});

        // Simple group based messages handler
        // message_handler->on_message(GroupsMessageHandler{cfg->groups});

        message_handler->on_error([] (std::string const& ns, std::string const& err) {
            std::cerr << "[ERR] " << ns << ": " << err << '\n';
        });
    }
    catch (std::exception const& err) {
        std::cerr << "FastMessageHandler create error: " << err.what() << '\n';
        return 1;
    }

    asio::UdpMulticastManager network_manager{message_handler->get_data_handler()};

    // enable receiving on the groups basis
    for (size_t group_id = 0; group_id < cfg->groups.size(); ++group_id) {
        message_handler->add_feed_group(uint16_t(group_id));
        auto& feeds = cfg->groups[group_id].feeds;
        for (size_t feed_id = 0; feed_id < feeds.size(); ++feed_id)
            network_manager.add_receiver(group_id, feeds[feed_id].multicast_group,
                                         feeds[feed_id].multicast_port, feeds.size());
    }

    std::cout << std::format("Starting at: {:%F %T}\n",
                             std::chrono::time_point_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now()));

    network_manager.start();

    sigset_t set;
    int sig;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    sigwait(&set, &sig);

    std::cout << std::format("Finished at: {:%F %T}\n",
                             std::chrono::time_point_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now()));

    return 0;
}
