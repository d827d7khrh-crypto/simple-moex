#include "config.h"
#include <toml11/value.hpp>
#include <toml11/parser.hpp>
#include <toml11/find.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <format>

namespace program_options
{

static options_description description ()
{
    // Options description: имя, описание, тип значения (если есть)
    options_description desc("Allowed options");
    desc.add_options()                   // use comment to prevent break
      ("help,h", "produce help message") //
      ("config,c", value<std::string>(), "configuration file path")      //
      ("template,t", value<std::string>(), "configuration file path")    //
      ("feed,f", value<std::string>(), "feed name from template")        //
      ("address_a,a", value<std::string>(), "IPV4 mulicast address 'A'") //
      ("port_a,p", value<unsigned short>(), "multicast port 'A'")        //
      ("address_b,b", value<std::string>(), "IPV4 mulicast address 'B'") //
      ("port_b,q", value<unsigned short>(), "multicast port 'B'")        //
      ("verbose,v", bool_switch()->default_value(false), "enable verbose mode");
    return desc;
}

std::pair<std::optional<variables_map>, std::string> try_get_options (int argc, char** argv)
{
    try {
        return std::make_pair(get_options(argc, argv), std::string{});
    }
    catch (std::exception const& err) {
        return std::make_pair(std::nullopt, err.what());
    }
}

std::optional<variables_map> get_options (int argc, char** argv)
{
    auto desc = description();
    variables_map vm;
    try {
        // parse a command line
        store(parse_command_line(argc, argv, desc), vm);
        notify(vm); // fillin vm and validate
    }
    catch (const error& e) {
        std::ostringstream os;
        os << "Error parsing options: " << e.what() << '\n' << desc << "\n";
        throw std::invalid_argument(os.str());
    }

    return vm;
}

std::string to_string ()
{ return (std::ostringstream{} << description()).str(); }

}

namespace config
{

std::unique_ptr<AppConfig> cfg;

std::string AppConfig::print () const
{
    std::ostringstream os;
    std::ranges::for_each(groups, [&os] (auto const& g) { os << g << '\n'; });
    return std::format("Exchange name: {}\nTemplate path: {}\nUsing message preamble: {}\n"
                       "FAST dictionary reset: {}\nMulticast Groups:\n{}\n",
                       exchange,
                       (std::filesystem::path{template_dir_path} / template_file_name).c_str(),
                       has_message_preamble, dictionary_reset, os.str());
}

// ============================================================================
// ConfigParser
// ============================================================================

ConfigParser::ConfigParser (boost::program_options::variables_map const& vm)
  : path_{vm.count("config") ? vm["config"].as<std::string>() : std::string{}}
{
}

// --------------------------------------------------------------------------
// Loading
// --------------------------------------------------------------------------

AppConfig ConfigParser::parse (const std::string& filepath)
{
    auto [cfg, err] = try_parse(filepath);
    if (!cfg) { throw std::runtime_error(!err.empty() ? err : get_error()); }
    return *cfg;
}

std::pair<std::optional<AppConfig>, std::string> ConfigParser::try_parse ()
{
    if (!path_.empty()) {
        auto [cfg, err] = try_parse(path_);
        if (cfg)
            return std::make_pair(std::move(cfg), std::string{});
        else
            return std::make_pair(std::nullopt, get_error());
    }

    return std::make_pair(std::nullopt, std::string{});
}

std::pair<std::optional<AppConfig>, std::string>
ConfigParser::try_parse (const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open()) {
        set_error(std::string("Cannot open config file: ") + filepath);
        return {};
    }

    try {
        toml::value cfg = toml::parse(file);
        config_         = parse_feed_group_from_root(cfg);
        error_.clear();
        return {config_, {}};
    }
    catch (const toml::syntax_error& e) {
        set_error(std::string("TOML syntax error in ") + filepath + ": " + e.what());
        return {};
    }
    catch (const toml::type_error& e) {
        set_error(std::string("TOML type error in ") + filepath + ": " + e.what());
        return {};
    }
    catch (const std::exception& e) {
        set_error(std::string("Failed to parse config ") + filepath + ": " + e.what());
        return {};
    }
}

const std::string& ConfigParser::get_error () const
{ return error_; }

const AppConfig& ConfigParser::get_config () const
{ return config_; }

// --------------------------------------------------------------------------
// Parsing helpers
// --------------------------------------------------------------------------

AppConfig ConfigParser::parse_feed_group_from_root (const toml::value& root)
{
    AppConfig cfg;

    // General section — toml11 v4.x: contains() is a member, find<T>() is free
    if (root.contains("general")) {
        const toml::value& gen = toml::find<toml::value>(root, "general");
        cfg.exchange           = toml::find_or(gen, "exchange", std::string{MOEX_EXCHANGE});
        cfg.template_file_name = toml::find<std::string>(gen, "template_file_name");
        cfg.template_dir_path  = toml::find<std::string>(gen, "template_dir_path");

        // MOEX has known preset
        if (cfg.exchange == MOEX_EXCHANGE) {
            // default is ASTS
            cfg.exchange_branch      = MOEX_EXCHANGE_ASTS;
            cfg.has_message_preamble = true;
            cfg.dictionary_reset     = true;
        }

        if (gen.contains("exchange_branch")) {
            cfg.exchange_branch = toml::find<std::string>(gen, "exchange_branch");
            if (cfg.exchange == MOEX_EXCHANGE) {
                if (cfg.exchange_branch != MOEX_EXCHANGE_ASTS &&
                    cfg.exchange_branch != MOEX_EXCHANGE_SPECTRA)
                    throw std::invalid_argument(std::format(
                      "{}: unknown feed branch: {}", MOEX_EXCHANGE, cfg.exchange_branch));
            }
        }
        if (gen.contains("has_message_preamble"))
            cfg.has_message_preamble = toml::find<bool>(gen, "has_message_preamble");
        if (gen.contains("dictionary_reset"))
            cfg.dictionary_reset = toml::find<bool>(gen, "dictionary_reset");
    }

    // Groups array — toml11 v4.x: iterate via as_array()
    if (root.contains("groups")) {
        const auto& arr = toml::find<toml::value>(root, "groups");
        for (const auto& elem : arr.as_array())
            if (auto group = parse_feed_group(elem)) cfg.groups.push_back(*std::move(group));
    }

    return cfg;
}

std::optional<FeedGroup> ConfigParser::parse_feed_group (const toml::value& tbl)
{
    // Subgroups array — toml11 v4.x: iterate via as_array()
    if (tbl.contains("feeds")) {
        FeedGroup g;
        g.name = toml::find<std::string>(tbl, "name");

        const auto& arr = toml::find<toml::value>(tbl, "feeds");
        for (auto const& elem : arr.as_array())
            g.feeds.push_back(parse_subscription(elem));
        return g;
    }

    return std::nullopt;
}

MulticastSubscription ConfigParser::parse_subscription (const toml::value& tbl)
{
    MulticastSubscription sub;
    sub.name            = toml::find<std::string>(tbl, "name");
    sub.multicast_group = toml::find<std::string>(tbl, "multicast_group");
    sub.multicast_port  = static_cast<uint16_t>(toml::find<int64_t>(tbl, "multicast_port"));
    return sub;
}

void ConfigParser::set_error (const std::string& msg)
{ error_ = msg; }

} // namespace config
