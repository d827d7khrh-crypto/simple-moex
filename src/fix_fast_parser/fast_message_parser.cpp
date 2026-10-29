#include <sstream>
#include <cstring>
#include "fast_message_parser.h"
#include "config.h"

namespace fast
{

// ============================================================================
// FastMessage
// ============================================================================

const FastField* FastMessage::find_field_by_name (const std::string& name) const
{
    for (const auto& f : fields) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

const FastField* FastMessage::find_field_by_id (int tag_id) const
{
    for (const auto& f : fields) {
        if (f.tag_id == tag_id) return &f;
    }
    return nullptr;
}

// ============================================================================
// FastMessageDecoder
// ============================================================================

// --------------------------------------------------------------------------
// Decoding
// --------------------------------------------------------------------------

FastMessage FastMessageDecoder::decode (const char* data, size_t size, bool dictionary_reset)
{
    std::string err;
    auto msg = try_decode(data, size, dictionary_reset, err);
    if (!msg) {
        if (!err.empty()) throw std::runtime_error(err);
        return {};
    }
    return *msg;
}

// MOEX: All dictionary entries are reset to the initial values specified after each UDP
// packet.
//       Currently, MOEX sends one message per UDP packet.
// - dictionary_reset: see above

std::optional<FastMessage> FastMessageDecoder::try_decode (const char* data, size_t size,
                                                           bool dictionary_reset,
                                                           std::string& err)
{
    Decoder* P_decoder = nullptr;
    try {
        if (!has_front_decoder()) emplace_front_decoder();

        P_decoder = pop_front_decoder();
        if (!P_decoder) { return std::nullopt; }

        const char* first = data;
        const char* last  = data + size;

        mfast::message_cref msg_ref = P_decoder->decode(first, last, dictionary_reset);

        if (msg_ref.absent()) {
            emplace_front_decoder(P_decoder);
            err = "Decoded message is absent";
            return std::nullopt;
        }

        FastMessage result;
        result.template_id    = static_cast<int>(msg_ref.id());
        result.template_name  = msg_ref.name();
        result.namespace_name = msg_ref.ns();

        // Convert message_cref to aggregate_cref for field iteration
        mfast::aggregate_cref agg(msg_ref);

        for (auto it = agg.begin(); it != agg.end(); ++it) {
            mfast::field_cref field_ref = *it;

            // Dispatch by field type and extract the appropriate structure
            switch (field_ref.field_type()) {
            case mfast::field_type_group: {
                // Group is a nested aggregate; extract as FastField
                // (its sub-fields are handled via the group itself)
                FastField ff = extract_field(field_ref);
                result.fields.push_back(std::move(ff));
                break;
            }

            case mfast::field_type_sequence: {
                // Sequence is extracted as FastSequence
                mfast::sequence_cref seq_ref(field_ref);
                FastSequence seq = extract_sequence(seq_ref);
                result.sequences.push_back(std::move(seq));
                break;
            }

            case mfast::field_type_templateref: {
                // TemplateRef is a nested message; extract as a field
                FastField ff = extract_field(field_ref);
                result.fields.push_back(std::move(ff));
                break;
            }

            default: {
                // Primitive types: int, string, decimal, byte_vector, enum
                FastField ff = extract_field(field_ref);
                result.fields.push_back(std::move(ff));
                break;
            }
            }
        }

        emplace_front_decoder(P_decoder);
        return result;
    }
    catch (const std::exception& e) {
        if (P_decoder) emplace_front_decoder(P_decoder);
        err = std::format("Decode error: {}", e.what());
        return std::nullopt;
    }
}

// --------------------------------------------------------------------------
// Field value extraction
// --------------------------------------------------------------------------

FastFieldValue FastMessageDecoder::extract_value (const mfast::field_cref& field_ref)
{
    if (field_ref.absent()) {
        return FastFieldValue{}; // boost::blank
    }

    mfast::field_type_enum_t type = field_ref.field_type();

    switch (type) {
    case mfast::field_type_int32: {
        mfast::int32_cref ref(field_ref);
        return static_cast<int32_t>(ref.value());
    }

    case mfast::field_type_int64: {
        mfast::int64_cref ref(field_ref);
        return static_cast<int64_t>(ref.value());
    }

    case mfast::field_type_uint32: {
        mfast::uint32_cref ref(field_ref);
        return static_cast<uint32_t>(ref.value());
    }

    case mfast::field_type_uint64: {
        mfast::uint64_cref ref(field_ref);
        return static_cast<uint64_t>(ref.value());
    }

    case mfast::field_type_decimal: {
        mfast::decimal_cref ref(field_ref);
        return static_cast<double>(ref);
    }

    case mfast::field_type_ascii_string: {
        mfast::ascii_string_cref ref(field_ref);
        return std::string(ref.c_str(), ref.size());
    }

    case mfast::field_type_unicode_string: {
        mfast::unicode_string_cref ref(field_ref);
        return std::string(ref.c_str(), ref.size());
    }

    case mfast::field_type_byte_vector: {
        mfast::byte_vector_cref ref(field_ref);
        // Convert byte vector to hex string
        std::string hex;
        hex.reserve(ref.size() * 2);
        const unsigned char* data = ref.data();
        for (mfast::array_view<unsigned char>::size_type i = 0; i < ref.size(); ++i) {
            unsigned char c               = data[i];
            static const char hex_chars[] = "0123456789abcdef";
            hex.push_back(hex_chars[(c >> 4) & 0x0f]);
            hex.push_back(hex_chars[c & 0x0f]);
        }
        return hex;
    }

    case mfast::field_type_enum: {
        mfast::enum_cref ref(field_ref);
        return static_cast<int32_t>(ref.value());
    }

    default:
        return FastFieldValue{};
    }
}

FastField FastMessageDecoder::extract_field (const mfast::field_cref& field_ref)
{
    FastField ff;
    ff.name   = field_ref.name() ? field_ref.name() : "";
    ff.tag_id = static_cast<int>(field_ref.id());
    ff.value  = extract_value(field_ref);
    return ff;
}

FastSequence FastMessageDecoder::extract_sequence (const mfast::sequence_cref& seq_ref)
{
    FastSequence seq;

    if (!seq_ref.present() || seq_ref.size() == 0) { return seq; }

    // Extract sequence name from instruction
    const auto* inst = seq_ref.instruction();
    if (inst) { seq.name = inst->name() ? inst->name() : ""; }

    // Iterate over sequence elements
    for (auto elem_it = seq_ref.begin(); elem_it != seq_ref.end(); ++elem_it) {
        const mfast::sequence_element_cref& elem = *elem_it;

        // Each sequence element is an aggregate
        for (size_t i = 0; i < elem.num_fields(); ++i) {
            mfast::field_cref field_ref = elem[i];
            FastField ff                = extract_field(field_ref);
            seq.fields.push_back(std::move(ff));
        }
    }

    return seq;
}

} // namespace fast
