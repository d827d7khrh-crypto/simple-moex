#include <iostream>
#include <string>
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

    config::ConfigParser parser(vm);
    auto [cfg, err] = parser.try_parse();
    if (!err.empty()) {
        std::cerr << "Config error: " << err << "\n";
        return 1;
    }

    if (!cfg) {
        std::cout << "Not using any configuration file\n";
        config::cfg = std::make_unique<config::AppConfig>();
    } else {
        config::cfg = std::make_unique<config::AppConfig>(*std::move(cfg));
    }

    auto template_filepath = config::cfg->template_dir_path / config::cfg->template_file_name;

    std::cerr << template_filepath << '\n' << config::cfg->exchange << '\n';

    std::unique_ptr<fast::FastMessageHandler> message_handler;
    try {
        message_handler = std::make_unique<fast::FastMessageHandler>(template_filepath);

        // Messages handler to save all FIX structures to files
        // message_handler->on_message(SaveFileMessageHandler{});

        // Message hadler to just output timestamps
        message_handler->on_message(DoNothingMessageHandler{});

        message_handler->on_error([] (std::string const& ns, std::string const& err) {
            std::cerr << "[ERR] " << ns << ": " << err << '\n';
        });
    }
    catch (std::exception const& err) {
        std::cerr << "FastMessageHandler create error: " << err.what() << '\n';
        return 1;
    }

    asio::UdpMulticastManager network_manager{message_handler->get_data_handler()};

    network_manager.add_receiver(config::GROUP_A, "239.195.5.65", 36065, 2);
    network_manager.add_receiver(config::GROUP_A, "239.195.133.65", 37065);

    network_manager.add_receiver(config::GROUP_B, "239.195.5.73", 36073, 2);
    network_manager.add_receiver(config::GROUP_B, "239.195.133.73", 37073);

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
