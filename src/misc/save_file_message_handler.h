#pragma once

#include <string>
#include <format>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <variant>
#include <flat_map>
#include "fast_message_handler.h"
#include "fast_message_parser.h"
#include "strings.h"

class SaveFileMessageHandler
{
  public:
    explicit SaveFileMessageHandler (std::ostream& os)
      : stream_{os}
    {
    }

    SaveFileMessageHandler ()
      : stream_{std::flat_map<std::string, std::shared_ptr<std::ofstream>>{}}
    {
    }

    void operator()(const fast::FastMessage& msg)
    {
        std::string local_namespace;
        std::ostream* os;
        if (stream_.index() == 0) {
            os              = &std::get<0>(stream_).get();
            local_namespace = msg.local_namespace_name + ": ";
        } else {
            if (std::get<1>(stream_).contains(msg.local_namespace_name)) {
                os = std::get<1>(stream_).at(msg.local_namespace_name).get();
            } else {
                std::filesystem::path filename{msg.local_namespace_name + "_feed.txt"};
                if (std::ofstream ofs{filename}) {
                    os = std::get<1>(stream_)
                           .try_emplace(msg.local_namespace_name,
                                        std::make_unique<std::ofstream>(std::move(ofs)))
                           .first->second.get();
                } else {
                    return;
                }
            }
        }

        auto str =
          std::format("{}{}::{}({}) -> {}, {}\n", local_namespace, msg.namespace_name,
                      msg.template_name, msg.template_id,
                      std::format("udp='{:%F %T}'",
                                  std::chrono::time_point_cast<std::chrono::microseconds>(
                                    std::chrono::system_clock::time_point(
                                      std::chrono::system_clock::duration(msg.time_us)))),
                      std::format("now='{:%F %T}'",
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
            if (!fields.empty()) { std::ranges::for_each(fields, pf); }
        };

        pl(msg.fields);
        if (!msg.sequences.empty()) {
            auto ps = [&] (auto& s) {
                str.append(" - ")
                  .append(s.name)
                  .append(": template elements=")
                  .append(std::to_string(s.elem_count))
                  .append(", length name=")
                  .append(s.length_name)
                  .append(", length tag=")
                  .append(std::to_string(s.length_tag_id))
                  .append(", groups=")
                  .append(std::to_string(s.groups.size()))
                  .append(1, '\n');
                for (auto& grp : s.groups) {
                    str.append("   -\n");
                    pl(grp.fields);
                };
            };
            std::ranges::for_each(msg.sequences, ps);
        }

        (*os) << str << std::endl;
    }

  private:
    std::variant<std::reference_wrapper<std::ostream>,
                 std::flat_map<std::string, std::shared_ptr<std::ofstream>>>
      stream_;
};
