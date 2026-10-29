#include <iostream>
#include <string>
#include <csignal>
#include <format>
#include "udp_multicast_receiver.h"
#include "fast_message_handler.h"
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

    std::cout << template_filepath << '\n' << config::cfg->exchange << '\n';

    std::unique_ptr<fast::FastMessageHandler> message_handler;
    try {
        message_handler = std::make_unique<fast::FastMessageHandler>(template_filepath);

        message_handler->on_message([] (const fast::FastMessage& msg) {
            auto str =
              std::format("{}: '{}' -> '{}' -> '{}: {}, {}'\n", msg.local_namespace_name,
                          msg.namespace_name, msg.template_id, msg.template_name,
                          std::format("udp={:%F %T}",
                                      std::chrono::time_point_cast<std::chrono::microseconds>(
                                        std::chrono::system_clock::time_point(
                                          std::chrono::system_clock::duration(msg.time_us)))),
                          std::format("now={:%F %T}",
                                      std::chrono::time_point_cast<std::chrono::microseconds>(
                                        std::chrono::system_clock::now())));
            auto pf = [&str] (auto& f) {
                if (f.has_value()) {
                    str.append(1, '\t')
                      .append(f.name)
                      .append(1, '[')
                      .append(std::to_string(f.tag_id))
                      .append("] ");
                    if (f.tag_id == 52)
                        str.append(misc::to_date(*f.template to<uint64_t>()));
                    else if (f.tag_id == 273) {
                        str.append("ns=")
                          .append(misc::to_time_ns(*f.template to<uint64_t>()))
                          .append(", us=")
                          .append(misc::to_time_us(*f.template to<uint64_t>()));
                    } else {
                        if (auto s = f.to_string())
                            str.append(*s);
                        else if (auto i = f.to_int32())
                            str.append(std::to_string(*i));
                        else if (auto i = f.to_uint32())
                            str.append(std::to_string(*i));
                        else if (auto i = f.to_int64())
                            str.append(std::to_string(*i));
                        else if (auto i = f.to_uint64())
                            str.append(std::to_string(*i));
                        else if (auto d = f.to_double())
                            str.append(std::format("{:.6f}", *d));
                    }
                    str.append(1, '\n');
                }
            };

            auto pl = [&] (auto& fields) {
                if (!fields.empty()) {
                    std::ranges::for_each(fields, pf);
                    str.append(" --\n");
                }
            };

            pl(msg.fields);
            if (!msg.sequences.empty()) {
                str.append(" -- ").append(msg.sequences.front().name).append(1, '\n');
                std::ranges::for_each(msg.sequences, pl, &fast::FastSequence::fields);
            }
            std::cout << str << std::endl;
        });

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

    network_manager.start();

    sigset_t set;
    int sig;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    sigwait(&set, &sig);

    return 0;
}
