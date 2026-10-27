#include "config.h"
#include <toml11/value.hpp>
#include <toml11/parser.hpp>
#include <toml11/find.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>

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

// ============================================================================
// AppConfig
// ============================================================================

const FeedGroup* AppConfig::find_group (const std::string& name) const
{
    for (const auto& g : groups) {
        if (g.name == name) return &g;
    }
    return nullptr;
}

std::vector<MulticastSubscription> AppConfig::all_subscriptions () const
{
    std::vector<MulticastSubscription> result;
    for (const auto& g : groups) {
        result.insert(result.end(), g.subgroups.begin(), g.subgroups.end());
    }
    return result;
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
        cfg.template_dictionary_reset = ::strcasecmp(cfg.exchange.data(), MOEX_EXCHANGE) == 0;
    }

    // Groups array — toml11 v4.x: iterate via as_array()
    if (root.contains("groups")) {
        const auto& arr = toml::find<toml::value>(root, "groups");
        for (const auto& elem : arr.as_array()) {
            cfg.groups.push_back(parse_feed_group(elem));
        }
    }

    return cfg;
}

FeedGroup ConfigParser::parse_feed_group (const toml::value& tbl)
{
    FeedGroup group;
    group.name = toml::find<std::string>(tbl, "name");

    // Subgroups array — toml11 v4.x: iterate via as_array()
    if (tbl.contains("subgroups")) {
        const auto& arr = toml::find<toml::value>(tbl, "subgroups");
        for (const auto& elem : arr.as_array()) {
            group.subgroups.push_back(parse_subscription(elem));
        }
    }

    return group;
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
