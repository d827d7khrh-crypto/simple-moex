#pragma once

#include <ranges>
#include <list>
#include <hffix.hpp>
#include "fast_message_parser.h"
#include "fix_fast_parser.h"
#include "transform.h"

namespace hndl
{

class StreamMessageHandler
{
  public:
    constexpr void operator()(fast::FastMessagePtr msg)
    {
        using namespace fast;
        using namespace trns;

        if (!msg) return;

        std::vector<uint8_t> packed_buffer, packed_values;
        packed_buffer.reserve(2048);

        // Initial offset for buffer's lenghts.
        size_t buffer_head_size = sizeof(uint32_t) * 2;

        // Reserve space for length headers.
        packed_buffer.resize(buffer_head_size); // 0 - 3: 32 bit buffer length
                                                // 4 - 7: 32 bit tags pack size

        // Template header.
        // Local Namespace (maddr:port) - string
        pack(msg->local_namespace_name, packed_buffer);
        // UDP packet receive time - chrono::system_clock::rep
        pack(msg->time_us, packed_buffer);
        // FAST template ID - int
        pack(msg->template_id, packed_buffer);

        // Reset buffer_size.
        buffer_head_size = packed_buffer.size();

        // Pack FAST message.
        const auto pack_field = [&packed_buffer, &packed_values] (FastField& field) {
            // Pack field's tag and value if field's operator is not a constant.
            // The constant value would be extracted from a template.
            if (field.has_value() && !field.is_constant) {
                // pack tag
                pack_24((uint32_t)field.tag_id, packed_buffer);
                // pack value
                switch (field.type) {
                    using enum mfast::field_type_enum_t;
                case field_type_int32:
                    pack(boost::get<int32_t>(field.value), packed_values);
                    break;
                case field_type_uint32:
                    pack(boost::get<uint32_t>(field.value), packed_values);
                    break;
                case field_type_int64:
                    pack(boost::get<int64_t>(field.value), packed_values);
                    break;
                case field_type_uint64:
                    pack(boost::get<uint64_t>(field.value), packed_values);
                    break;
                case field_type_decimal:
                    pack(boost::get<Decimal64>(field.value), packed_values);
                    break;
                case field_type_ascii_string:
                case field_type_unicode_string:
                    pack(boost::get<std::string>(field.value), packed_values);
                    break;
                case field_type_byte_vector:
                default:
                    break;
                }
            }
        };

        const auto pack_fields = [&pack_field] (FieldGroup& fields) {
            std::ranges::for_each(fields, pack_field);
        };

        // Pack global fields.
        pack_fields(msg->fields);

        auto pack_sequence = [&packed_buffer, &pack_fields] (this auto&& self,
                                                             FastSequence& sequence) -> void {
            // Sequence has a length_tag before repeated groups.
            pack_24(sequence.length_tag_id, packed_buffer);
            // Pack all the groups.
            for (FieldGroup& field_group : sequence.groups)
                pack_fields(field_group);

            // Nested sequences are handled recursively.
            std::ranges::for_each(sequence.nested, self);
        };

        // Pack sequence groups.
        std::ranges::for_each(msg->sequences, pack_sequence);

        // Size of packed tags vector.
        uint32_t packed_tags_size =
          static_cast<uint32_t>(packed_buffer.size() - buffer_head_size);

        // Append packed values vector to packed buffer.
        packed_buffer.append_range(std::move(packed_values));
        uint32_t buffer_size = static_cast<uint32_t>(packed_buffer.size());
        auto length          = packed_buffer.begin();

        // Fill in the the lenght of the full buffer.
        *reinterpret_cast<uint32_t*>(&*length) = htonl(buffer_size);

        // Fill in the tags vector lenght.
        length += sizeof(uint32_t);
        *reinterpret_cast<uint32_t*>(&*length) = htonl(packed_tags_size);

        transmit(std::move(packed_buffer));
    }

  private:
    void transmit (std::vector<uint8_t> buffer);
    void print_packed (std::vector<uint8_t> const& buffer);
};

void StreamMessageHandler::transmit (std::vector<uint8_t> buffer)
{ print_packed(buffer); }

struct Value
{
    struct field_t
    {
        fast::FastFieldValue val;
        fast::FieldType type;
        uint32_t tag;
    };
    using type_t = std::variant<field_t, std::vector<Value>>;
    type_t var;
};

void StreamMessageHandler::print_packed (std::vector<uint8_t> const& buffer)
{
    using namespace trns;

    auto first      = buffer.cbegin();
    uint32_t length = *reinterpret_cast<const uint32_t*>(&*first);
    length          = ntohl(length);

    first += sizeof(uint32_t);
    uint32_t tags_length = *reinterpret_cast<const uint32_t*>(&*first);
    tags_length          = ntohl(tags_length);

    first += sizeof(uint32_t);
    std::span<const uint8_t> data_span{first, buffer.end()};

    std::cout << "1: packed_buffer size = " << buffer.size() << " (" << length << ")\n";

    // Unpack template headers.
    // Local Namespace (maddr:port) - string
    std::string local_namespace_name;
    auto taken = unpack(local_namespace_name, data_span);
    std::cout << "local_namespace_name=" << local_namespace_name << '\n';

    // UDP packet receive time - chrono::system_clock::rep
    std::chrono::system_clock::rep time_us;
    auto view = data_span | std::views::drop(taken);
    taken += unpack(time_us, view);
    std::cout << "time_us="
              << std::chrono::time_point_cast<std::chrono::microseconds>(
                   std::chrono::system_clock::time_point(
                     std::chrono::system_clock::duration(time_us)))
              << '\n';
    // FAST template ID - int
    int template_id;
    view = data_span | std::views::drop(taken);
    taken += unpack(template_id, view);
    std::cout << "template_id=" << template_id << '\n';

    // Unpack tags pack.
    auto old_flags = std::cout.flags();
    std::cout << std::hex << std::showbase;

    view = data_span | std::views::drop(taken) | std::views::take(tags_length);
    taken += tags_length;

    for (auto c : view) {
        std::cout << (int)c;
    }

    std::cout.flags(old_flags);
    std::cout << '\n';

    auto v1 = unpack_24(view);
    for (auto c : v1)
        std::cout << c << ' ';
    std::cout << '\n';

    // Unpack values pack.
    using namespace config;
    fast::FastTemplatesParser parser;
    auto template_filepath = cfg->template_dir_path / cfg->template_file_name;
    if (!parser.parse_file(template_filepath)) {
        std::cerr << "Failed to parse: " << parser.get_error() << "\n";
        return;
    }

    auto* P_tmpl = parser.get_templates().find(template_id);
    if (!P_tmpl) {
        std::cerr << std::format("Unknown template id = {:d}\n", template_id);
        return;
    }

    auto pf = [&taken, &data_span] (fast::FieldType type, uint32_t tag) -> Value {
        auto view = data_span | std::views::drop(taken);
        switch (type) {
            using enum fast::FieldType;
        case Int32: {
            int32_t n;
            taken += unpack(n, view);
            return {Value::field_t{n, type, tag}};
        }
        case UInt32: {
            uint32_t n;
            taken += unpack(n, view);
            return {Value::field_t{n, type, tag}};
        }
        case Int64: {
            int64_t n;
            taken += unpack(n, view);
            return {Value::field_t{n, type, tag}};
        }
        case UInt64: {
            uint64_t n;
            taken += unpack(n, view);
            return {Value::field_t{n, type, tag}};
        }
        case Decimal: {
            fast::Decimal64 d;
            taken += unpack(d, view);
            return {Value::field_t{d, type, tag}};
        }
        case String: {
            std::string s;
            taken += unpack(s, view);
            return {Value::field_t{s, type, tag}};
        }
        default:
            return {Value::field_t{{}, type, 0}};
        }
    };

    // auto ps = [&pf] (fast::SequenceDefinition const& seq) {
    //     const fast::SequenceDefinition* P_nested = seq.find_nested(tag);
    //     if (P_nested)
    // };

    std::list<Value> gf;

    const fast::FieldDefinition* P_filed;
    const fast::SequenceDefinition* P_seq = nullptr;
    int seq_first_tag;

    for (auto tag_it = v1.begin(); tag_it != v1.end(); ++tag_it) {
        if (P_seq) {
            P_filed = P_seq->find_field(*tag_it);
            if (P_filed) {
                if (seq_first_tag == *tag_it) { gf.emplace_back(std::vector<Value>{}); }
                std::get<1>(gf.back().var).push_back(pf(P_filed->type, *tag_it));
            }
        } else {
            P_filed = P_tmpl->find_field(*tag_it);
            if (P_filed) {
                gf.push_back(pf(P_filed->type, *tag_it));
                P_seq = nullptr;
            } else {
                P_seq = P_tmpl->find_sequence(*tag_it);
                if (P_seq) { seq_first_tag = *(tag_it + 1); }
            }
        }
    }

    auto boost_get_str = [] (Value::field_t& field) -> std::string {
        switch (field.type) {
            using enum fast::FieldType;
        case Int32:
            return std::to_string(boost::get<int32_t>(field.val));
        case UInt32:
            return std::to_string(boost::get<uint32_t>(field.val));
        case Int64:
            return std::to_string(boost::get<int64_t>(field.val));
        case UInt64:
            return std::to_string(boost::get<uint64_t>(field.val));
        case Decimal:
            return std::to_string((double)boost::get<fast::Decimal64>(field.val));
        case String:
            return boost::get<std::string>(field.val);
        default:
            break;
        }
    };

    for (auto& x : gf) {
        if (x.var.index() == 0) {
            auto& var = std::get<0>(x.var);
            std::cout << var.tag << ": " << fast::field_type_to_string(var.type) << ": "
                      << boost_get_str(var) << '\n';
        } else {
            std::cout << "v=" << std::get<1>(x.var).size() << '\n';
            for (auto& y : std::get<1>(x.var)) {
                auto& var = std::get<0>(y.var);
                std::cout << var.tag << ": " << fast::field_type_to_string(var.type) << ": "
                          << boost_get_str(var) << '\n';
            }
        }
    }
}

} // namespace hndl
