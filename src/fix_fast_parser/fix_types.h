#pragma once

#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <ranges>
#include <iostream>

namespace fast
{

// ============================================================================
// Fields types for FIX FAST
// ============================================================================

enum class FieldType { String, Int32, Int64, UInt32, UInt64, Decimal };

enum class Presence { Required, Optional };

enum class Charset { Default, Unicode };

// ============================================================================
// Auxilary functions
// ============================================================================

inline std::string field_type_to_string (FieldType type)
{
    switch (type) {
    case FieldType::String:
        return "String";
    case FieldType::Int32:
        return "Int32";
    case FieldType::Int64:
        return "Int64";
    case FieldType::UInt32:
        return "UInt32";
    case FieldType::UInt64:
        return "UInt64";
    case FieldType::Decimal:
        return "Decimal";
    }
    return "Unknown";
}

inline std::string presence_to_string (Presence p)
{ return (p == Presence::Required) ? "required" : "optional"; }

inline std::ostream& operator<<(std::ostream& os, FieldType type)
{ return os << field_type_to_string(type); }

inline std::ostream& operator<<(std::ostream& os, Presence p)
{ return os << presence_to_string(p); }

// ============================================================================
// Field Definition
// ============================================================================

struct FieldDefinition
{
    std::string name; // field name (example, "Symbol")
    int id;           // FIX-tag (example, 55)
    FieldType type;
    Presence presence = Presence::Required;
    Charset charset   = Charset::Default;
    // for constant operator, holds a value
    std::optional<std::string> constant_value;

    // for the late data binding
    constexpr bool is_constant () const
    { return constant_value.has_value(); }
};

// ============================================================================
// Sequence Definition
// ============================================================================

struct SequenceDefinition
{
    friend class FastTemplatesParser;

    std::string name; // sequence name (example, "MDEntries")
    int length_tag;   // FIX-tag of the elements counter (example, NoMDEntries = 268)
    Presence presence = Presence::Required;
    bool is_nested    = false;

  private:
    std::vector<FieldDefinition> fields_;
    std::vector<SequenceDefinition> nested_;

  public:
    constexpr decltype(fields_) const& fields () const
    { return fields_; }

    constexpr decltype(nested_) const& nested () const
    { return nested_; }

    // Quick fields searching inside a sequence
    constexpr const FieldDefinition* find_field (std::string const& name) const
    {
        auto it = std::ranges::find(fields_, name, &FieldDefinition::name);
        return it != fields_.cend() ? &*it : nullptr;
    }

    constexpr const FieldDefinition* find_field (int tag_id) const
    {
        auto it = std::ranges::find(fields_, tag_id, &FieldDefinition::id);
        return it != fields_.cend() ? &*it : nullptr;
    }

    constexpr const SequenceDefinition* find_nested (std::string const& name) const
    {
        auto it = std::ranges::find(nested_, name, &SequenceDefinition::name);
        return it != nested_.cend() ? &*it : nullptr;
    }

    constexpr const SequenceDefinition* find_nested (int length_tag) const
    {
        auto it = std::ranges::find(nested_, length_tag, &SequenceDefinition::length_tag);
        return it != nested_.cend() ? &*it : nullptr;
    }

    void print (std::ostream& os, int indent = 0) const
    {
        std::string indent_str(indent, ' ');
        os << indent_str << (is_nested ? "NestedSequence: " : "Sequence: ") << name
           << " (count_tag=" << length_tag << ", " << presence_to_string(presence) << ")\n";
        os << indent_str << "  Fields:\n";
        for (const auto& f : fields_) {
            os << indent_str << "    [" << presence_to_string(f.presence) << "] " << f.name
               << " (tag=" << f.id << ", " << field_type_to_string(f.type) << ")\n";
        }
        for (const auto& ns : nested_) {
            ns.print(os, indent + 4);
        }
    }
};

// ============================================================================
// Template Definition
// ============================================================================

struct TemplateDefinition
{
    friend class FastTemplatesParser;

    std::string name; // template name (example, "DefaultIncrementalRefreshMessage")
    int id;           // template ID FIX FAST (example, 31)

  private:
    std::vector<FieldDefinition> fields;
    std::vector<SequenceDefinition> sequences;

  public:
    // Quick fields searching in templates.
    constexpr const FieldDefinition* find_field (std::string const& name) const
    {
        auto it = std::ranges::find(fields, name, &FieldDefinition::name);
        return it != fields.cend() ? &*it : nullptr;
    }

    constexpr const FieldDefinition* find_field (int tag_id) const
    {
        auto it = std::ranges::find(fields, tag_id, &FieldDefinition::id);
        return it != fields.cend() ? &*it : nullptr;
    }

    constexpr const SequenceDefinition* find_sequence (std::string const& name) const
    {
        auto it = std::ranges::find(sequences, name, &SequenceDefinition::name);
        return it != sequences.cend() ? &*it : nullptr;
    }

    constexpr const SequenceDefinition* find_sequence (int length_tag) const
    {
        auto it = std::ranges::find(sequences, length_tag, &SequenceDefinition::length_tag);
        return it != sequences.cend() ? &*it : nullptr;
    }

    void print (std::ostream& os = std::cout, int indent = 0) const
    {
        std::string indent_str(indent, ' ');
        os << indent_str << "Template: " << name << " (id=" << id << ")\n";
        os << indent_str << "  Fields:\n";
        for (const auto& f : fields) {
            os << indent_str << "    [" << presence_to_string(f.presence) << "] " << f.name
               << " (tag=" << f.id << ", " << field_type_to_string(f.type);
            if (f.is_constant()) { os << " = " << *f.constant_value; }
            if (f.charset != Charset::Default) { os << ", unicode"; }
            os << ")\n";
        }
        os << indent_str << "  Sequences:\n";
        for (const auto& s : sequences) {
            os << indent_str << "    Sequence: " << s.name << " (count_tag=" << s.length_tag
               << ", " << presence_to_string(s.presence) << ")\n";
            os << indent_str << "      Fields:\n";
            for (const auto& f : s.fields()) {
                os << indent_str << "        [" << presence_to_string(f.presence) << "] "
                   << f.name << " (tag=" << f.id << ", " << field_type_to_string(f.type)
                   << ")\n";
            }
            for (const auto& ns : s.nested()) {
                ns.print(os, indent + 8);
            }
        }
    }
};

// ============================================================================
// Templates Dictionary
// ============================================================================

class TemplatesDictionary
{
  public:
    std::string namespace_name; // the namespace attribute of XML

  private:
    // Parsed templates.
    std::vector<TemplateDefinition> templates;

    // quick access indexes
    std::unordered_map<std::string, size_t> name_index; // name -> index
    std::unordered_map<int, size_t> id_index;           // template id -> index

  public:
    // Add a template
    void add_template (TemplateDefinition tpl)
    {
        size_t idx = templates.size();
        templates.push_back(std::move(tpl));
        name_index[templates[idx].name] = idx;
        id_index[templates[idx].id]     = idx;
    }

    // Lookup by Name
    constexpr const TemplateDefinition* find (std::string const& name) const
    {
        auto it = name_index.find(name);
        return it != name_index.end() ? &templates[it->second] : nullptr;
    }

    // Lookup by Id
    constexpr const TemplateDefinition* find (int id) const
    {
        auto it = id_index.find(id);
        return it != id_index.end() ? &templates[it->second] : nullptr;
    }

    void print (std::ostream& os = std::cout) const
    {
        os << "FAST Templates Dictionary:\n";
        for (const auto& tpl : templates) {
            tpl.print(os, 2);
            os << "\n";
        }
    }
};

} // namespace fast
