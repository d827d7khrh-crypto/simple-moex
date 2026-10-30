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

constexpr static inline const size_t WORKERS_THREADS = 32;
constexpr static inline const char MOEX_EXCHANGE[]   = "MOEX";

enum : uint16_t { GROUP_A, GROUP_B };

// ============================================================================
// Single multicast subscription (one subgroup within a feed group)
// ============================================================================

struct MulticastSubscription
{
    std::string name; // Subgroup name (e.g., "A", "B")
    std::string multicast_group;
    uint16_t multicast_port = 0;
};

// ============================================================================
// Feed group — a collection of multicast subscriptions
// ============================================================================

struct FeedGroup
{
    std::string name; // Group name (e.g., "FEED_1")
    std::vector<MulticastSubscription> subgroups;
};

// ============================================================================
// Top-level application configuration
// ============================================================================

struct AppConfig
{
    std::string exchange;
    std::filesystem::path template_file_name; // FIX FAST template filename
    std::filesystem::path template_dir_path;  // Path to templates directory
    std::vector<FeedGroup> groups;            // Feed groups
    bool template_dictionary_reset;

    /// Find a feed group by name (case-sensitive)
    const FeedGroup* find_group (const std::string& name) const;

    /// Collect all multicast subscriptions across all groups
    std::vector<MulticastSubscription> all_subscriptions () const;
#if 0
    constexpr AppConfig& set_empty ()
    {
        empty_ = true;
        return *this;
    }

    constexpr bool is_empty ()
    { return empty_; }
#endif
  private:
    // bool empty_{false};
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
    FeedGroup parse_feed_group (const toml::value& tbl);

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
