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
#include <stop_token>
#include <condition_variable>
#include <forward_list>
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
};

/// Parsed message sequence
struct FastSequence
{
    std::string name;                 // Sequence name
    int count_tag_id = -1;            // FIX tag for element count
    std::vector<FastField> fields;    // Fields in this sequence
    std::vector<FastSequence> nested; // Nested sequences
};

// ============================================================================
// Parsed FAST message
// ============================================================================

struct FastMessage
{
    int template_id = -1;                // FAST template ID
    std::string template_name;           // Template name
    std::string namespace_name;          // Namespace name
    std::string local_namespace_name;    // Local Namespace (maddr:port)
    std::vector<FastField> fields;       // Top-level fields
    std::vector<FastSequence> sequences; // Sequences

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
    constexpr static inline const unsigned int MAX_DECODERS_COUNT = config::MAX_TASKS_SIZE;
    struct Decoder
    {
        // Store an error message
        template <typename S>
            requires std::is_convertible<S, std::string>::value
        void set_error (S&& msg)
        { err = std::forward<S>(msg); }
        /// Get error message from the last failed decode attempt
        constexpr std::string const& get_error () const
        { return err; }

        mfast::fast_decoder decoder;
        std::string err;
    };

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
    using TryDecodeResult = std::pair<FastMessage, std::string>;
    TryDecodeResult try_decode (const char* data, size_t size, bool dictionary_reset);

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
                           MAX_DECODERS_COUNT;
                })) {
                if (!P_decoder) {
                    P_decoder = ::new Decoder;
                    // fast_decoder takes templates via include()
                    P_decoder->decoder.include({templates_.get()});
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

    std::stop_source& stop_source_;

    std::unique_ptr<mfast::dynamic_templates_description> templates_;

    std::mutex decoders_mutex_;
    std::condition_variable_any decoders_full_cv_;
    std::forward_list<Decoder*> decoders_;
};

} // namespace fast
