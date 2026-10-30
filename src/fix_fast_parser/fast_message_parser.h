#pragma once

// mFAST 1.3 headers may not include <algorithm> in some translation units
#include <algorithm>
#include <filesystem>
#include <mfast/xml_parser/dynamic_templates_description.h>
#include <mfast/coder/fast_decoder.h>
#include <mfast/sequence_ref.h>
#include <mfast/group_ref.h>
#include <mfast/field_visitor.h>
#include <boost/variant.hpp>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <mutex>
#include <optional>
#include <stop_token>
#include <condition_variable>
#include <forward_list>
#include <chrono>
#include <stdexcept>
#include "fix_types.h"
#include "config.h"

namespace fast
{

// ============================================================================
// FAST message field value types (boost::variant)
// ============================================================================

/// Value of a single FAST message field
using FastFieldValue =
  boost::variant<boost::blank, int32_t, int64_t, uint32_t, uint64_t, double, std::string>;

/// Parsed message field
struct FastField
{
    std::string name;     // Field name (e.g., "Symbol")
    int tag_id = -1;      // FIX tag ID
    FastFieldValue value; // Field value

    constexpr bool has_value () const
    { return value.which() != 0; }

    constexpr std::optional<std::string> to_string () const
    {
        if (auto* str_ptr = boost::get<std::string>(&value))
            return *str_ptr;
        else
            return std::nullopt;
    }
    constexpr std::optional<int32_t> to_int32 () const
    {
        if (auto* int_ptr = boost::get<int32_t>(&value))
            return *int_ptr;
        else
            return std::nullopt;
    }
    constexpr std::optional<uint32_t> to_uint32 () const
    {
        if (auto* int_ptr = boost::get<uint32_t>(&value))
            return *int_ptr;
        else
            return std::nullopt;
    }
    constexpr std::optional<int64_t> to_int64 () const
    {
        if (auto* int_ptr = boost::get<int64_t>(&value))
            return *int_ptr;
        else
            return std::nullopt;
    }
    constexpr std::optional<uint64_t> to_uint64 () const
    {
        if (auto* int_ptr = boost::get<uint64_t>(&value))
            return *int_ptr;
        else
            return std::nullopt;
    }
    constexpr std::optional<double> to_double () const
    {
        if (auto* dbl_ptr = boost::get<double>(&value))
            return *dbl_ptr;
        else
            return std::nullopt;
    }
    template <typename T> constexpr std::optional<T> to () const
    {
        if (auto* ptr = boost::get<T>(&value))
            return *ptr;
        else
            return std::nullopt;
    }
};

/// Parsed message sequence
struct FastSequence
{
    std::string name;           // Sequence name
    size_t elem_count = 0;      // Full amount of elements in a sequence group
    std::string length_name;    // length field name
    uint64_t length_tag_id = 0; // FIX tag for element count
    // Each sequence consists of `group_count' field groups
    struct FieldGroup
    {
        using fields_t = std::vector<FastField>;
        fields_t fields;
    };
    std::vector<FieldGroup> groups;   // Fields in this sequence
    std::vector<FastSequence> nested; // Nested sequences
};

// ============================================================================
// Parsed FAST message
// ============================================================================

struct FastMessage
{
    int template_id = -1;                   // FAST template ID
    std::chrono::system_clock::rep time_us; // UDP packet receive time
    std::string template_name;              // Template name
    std::string namespace_name;             // Namespace name
    std::string local_namespace_name;       // Local Namespace (maddr:port)
    std::vector<FastField> fields;          // Top-level fields
    std::vector<FastSequence> sequences;    // Sequences

    /// Find a field by name (top-level only)
    const FastField* find_field_by_name (const std::string& name) const;

    /// Find a field by FIX tag ID (top-level only)
    const FastField* find_field_by_id (int tag_id) const;
};

using FastMessagePtr = std::shared_ptr<FastMessage>;

// ============================================================================
// Binary FAST message decoder
// ============================================================================

class FastMessageDecoder
{
    using Decoder = mfast::fast_decoder;

  public:
    // ------------------------------------------------------------------
    // Initialization
    // ------------------------------------------------------------------

    FastMessageDecoder (std::filesystem::path const& filepath, std::stop_source& stop_source)
      : stop_source_{stop_source}
    {
        if (std::ifstream file{filepath, std::ios::binary}) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            /// Templates are loaded from an XML file
            auto [ok, err] = load_templates(buffer.str());
            if (ok)
                emplace_front_decoder();
            else
                throw std::runtime_error(err);
        } else {
            throw std::runtime_error(
              std::format("Cannot open templates file: {:s}", filepath.c_str()));
        };
    }

    ~FastMessageDecoder ()
    {
        std::cout << std::format("[FastMessageDecoder] Used {} decoders\n",
                                 std::distance(decoders_.begin(), decoders_.end()));
    }

    // ------------------------------------------------------------------
    // Binary message decoding
    // ------------------------------------------------------------------

    /// Decode binary data into a FastMessage.
    /// Throws std::runtime_error on failure.
    FastMessage decode (const char* data, size_t size, bool dictionary_reset);

    /// Attempt to decode; returns {message, error}.
    /// If error is empty, decoding succeeded.
    std::optional<FastMessage> try_decode (const char* data, size_t size,
                                           bool dictionary_reset, std::string& err);

  private:
    // ------------------------------------------------------------------
    // Template loading from XML
    // ------------------------------------------------------------------
    std::pair<bool, std::string> load_templates (std::string const& xml_content)
    {
        // Won't overload templates
        if (templates_) return {false, std::string("The templates were alredy loaded")};

        // parse XML string into templates description
        try {
            templates_ = std::make_unique<mfast::dynamic_templates_description>(xml_content);
            return {true, {}};
        }
        catch (const std::exception& e) {
            return {false, std::string("Failed to load templates: ") + e.what()};
        }
    }
    // ------------------------------------------------------------------
    // Increase / reuse decoders
    // ------------------------------------------------------------------
    void emplace_front_decoder (Decoder* P_decoder = nullptr)
    {
        std::stop_token stoken = stop_source_.get_token();

        do {
            std::unique_lock lk{decoders_mutex_};
            if (decoders_full_cv_.wait(lk, stoken, [this] {
                    return std::distance(decoders_.begin(), decoders_.end()) <
                           config::WORKERS_THREADS * 2;
                })) {
                if (!P_decoder) {
                    P_decoder = ::new Decoder;
                    // fast_decoder takes templates via include()
                    P_decoder->include({templates_.get()});
                }
                decoders_.push_front(P_decoder);
                return;
            }
        } while (!stoken.stop_requested());
    }

    Decoder* pop_front_decoder ()
    {
        std::stop_token stoken = stop_source_.get_token();
        do {
            std::unique_lock lk{decoders_mutex_};
            if (decoders_full_cv_.wait(lk, stoken, [this] { return !decoders_.empty(); })) {
                auto* front = decoders_.front();
                decoders_.pop_front();
                return front;
            }
        } while (!stoken.stop_requested());
        return nullptr;
    }

    constexpr bool has_front_decoder ()
    {
        std::lock_guard lk{decoders_mutex_};
        return !decoders_.empty();
    }

    // Extract a field value based on its mFAST type
    FastFieldValue extract_value (const mfast::field_cref& field_ref);

    // Extract a single field from field_cref
    FastField extract_field (const mfast::field_cref& field_ref);

    // Extract a sequence from sequence_cref
    FastSequence extract_sequence (const mfast::sequence_cref& seq_ref);

    std::unique_ptr<mfast::dynamic_templates_description> templates_;

    std::stop_source& stop_source_;

    std::mutex decoders_mutex_;
    std::condition_variable_any decoders_full_cv_;
    std::forward_list<Decoder*> decoders_;
};

} // namespace fast
