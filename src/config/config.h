#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <utility>
#include <filesystem>
#include <boost/program_options.hpp>
#include <toml11/value.hpp>
#include <toml11/types.hpp>

namespace program_options
{

using namespace boost::program_options;

std::optional<variables_map> get_options (int argc, char** argv);

std::pair<std::optional<variables_map>, std::string> try_get_options (int argc, char** argv);

std::string to_string ();

}

namespace config
{

constexpr static inline const size_t WORKERS_THREADS       = 8;
constexpr static inline const char MOEX_EXCHANGE[]         = "MOEX";
constexpr static inline const char MOEX_EXCHANGE_ASTS[]    = "ASTS";
constexpr static inline const char MOEX_EXCHANGE_SPECTRA[] = "SPECTRA";

// ============================================================================
// Single multicast subscription (one subgroup within a feed group)
// ============================================================================

struct MulticastSubscription
{
    std::string name; // Subgroup name (e.g., "A", "B")
    std::string multicast_group;
    std::string source_ip;
    uint16_t multicast_port = 0;
};

struct HistoricalReplay
{
    std::string address;
    uint16_t port = 0;
};

inline std::ostream& operator<<(std::ostream& os, MulticastSubscription const& ms)
{
    os << "name: " << ms.name << ", multicast_group: " << ms.multicast_group
       << ", port: " << ms.multicast_port;
    return os;
}

// ============================================================================
// Feed group — a collection of multicast subscriptions
// ============================================================================

struct FeedGroup
{
    std::string name; // Group name (e.g., "FEED_1")
    std::vector<MulticastSubscription> feeds;
    std::vector<HistoricalReplay> replay;
};

inline std::ostream& operator<<(std::ostream& os, FeedGroup const& g)
{
    os << "Group name: " << g.name;
    std::ranges::for_each(g.feeds,
                          [&os] (auto const& ms) { os << "\n Subscription: " << ms; });
    return os;
}

// ============================================================================
// Top-level application configuration
// ============================================================================

struct AppConfig
{
    constexpr static inline const size_t INVALID_ID = std::numeric_limits<size_t>::max();

    std::string exchange;
    std::string exchange_branch;
    std::filesystem::path template_file_name; // FIX FAST template filename
    std::filesystem::path template_dir_path;  // Path to templates directory
    bool has_message_preamble{false};
    bool dictionary_reset{false};

    std::vector<FeedGroup> groups; // Feed groups

    std::string print () const;

    // Returns the names of all the groups.
    constexpr std::vector<std::reference_wrapper<const std::string>> groups_names () const
    {
        std::vector<std::reference_wrapper<const std::string>> names;
        for (auto& g : groups)
            names.push_back(g.name);
        return names;
    }

    /// Find a feed group by name (case-sensitive)
    constexpr std::optional<std::reference_wrapper<const FeedGroup>>
    find_group (const std::string& name) const
    {
        for (const auto& g : groups) {
            if (g.name == name) return std::optional{g};
        }
        return std::nullopt;
    }

    // Lookup a feed group by group index.
    // The index is a group position inside the vector.
    constexpr std::optional<std::reference_wrapper<const FeedGroup>>
    find_group (size_t index) const
    {
        if (groups.size() > index) return groups.at(index);
        return std::nullopt;
    }

    // Lookup an index of a group by group's name.
    // Returns index of a group or unsigned MAX value if the group with name `name'
    // doesn't exist.
    constexpr size_t group_index (std::string const& name) const
    {
        if (auto search = std::ranges::find(groups, name, &FeedGroup::name);
            search != groups.end())
            return std::distance(groups.begin(), search);
        return INVALID_ID;
    };
};

extern std::unique_ptr<AppConfig> cfg;

// ============================================================================
// Config parser — reads configuration from a TOML file
// ============================================================================

class ConfigParser
{
  public:
    explicit ConfigParser (boost::program_options::variables_map const& var);
    ~ConfigParser () = default;

    // Non-copyable, movable
    ConfigParser (const ConfigParser&)           = delete;
    ConfigParser& operator=(const ConfigParser&) = delete;

    ConfigParser (ConfigParser&& other) noexcept
      : config_{std::move(other.config_)},
        path_{std::move(other.path_)},
        error_{std::move(other.error_)}
    {
    }

    ConfigParser& operator=(ConfigParser&& other) noexcept
    {
        if (this != &other) {
            config_ = std::move(other.config_);
            path_   = std::move(other.path_);
            error_  = std::move(other.error_);
        }
        return *this;
    }

    // ------------------------------------------------------------------
    // Loading
    // ------------------------------------------------------------------

    /// Parse a TOML file and return the configuration.
    /// Throws std::runtime_error on failure.
    AppConfig parse (const std::string& filepath);

    /// Attempt to parse; returns {config, error}.
    /// If error is empty, parsing succeeded.
    std::pair<std::optional<AppConfig>, std::string> try_parse (const std::string& filepath);

    std::pair<std::optional<AppConfig>, std::string> try_parse ();

    /// Get error message from the last failed parse attempt
    const std::string& get_error () const;

    // ------------------------------------------------------------------
    // Accessor
    // ------------------------------------------------------------------

    /// Get the last successfully parsed configuration
    const AppConfig& get_config () const;

  private:
    AppConfig parse_feed_group_from_root (const toml::value& root);

    // Parse a single feed group from TOML table
    std::optional<FeedGroup> parse_feed_group (const toml::value& tbl);

    // Parse a single multicast subscription from TOML table
    MulticastSubscription parse_subscription (const toml::value& tbl);

    // Set error message
    void set_error (const std::string& msg);

    // Last parsed configuration
    AppConfig config_;
    std::filesystem::path path_;
    std::string error_;
};

} // namespace config
