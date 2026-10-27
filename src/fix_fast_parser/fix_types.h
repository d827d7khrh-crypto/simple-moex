#pragma once

#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
    std::optional<std::string> constant_value; // Если задано — константа

    // for the late data binding
    bool is_constant () const
    { return constant_value.has_value(); }
};

// ============================================================================
// Nested Sequence Definition
// ============================================================================

struct NestedSequenceDefinition
{
    std::string name; // name of nested sequence. Example - Group
    int length_tag;   // FIX-tag of elements counter
    Presence presence = Presence::Required;
    std::vector<FieldDefinition> fields;
    std::vector<NestedSequenceDefinition> nested_sequences;

    // Quick search fields by name inside a nested sequence
    const FieldDefinition* find_field_by_name (const std::string& name) const
    {
        for (const auto& f : fields) {
            if (f.name == name) return &f;
        }
        return nullptr;
    }

    const FieldDefinition* find_field_by_id (int tag_id) const
    {
        for (const auto& f : fields) {
            if (f.id == tag_id) return &f;
        }
        return nullptr;
    }

    const NestedSequenceDefinition*
    find_nested_sequence_by_name (const std::string& name) const
    {
        for (const auto& ns : nested_sequences) {
            if (ns.name == name) return &ns;
        }
        return nullptr;
    }

    void print (std::ostream& os, int indent = 0) const
    {
        std::string indent_str(indent, ' ');
        os << indent_str << "NestedSequence: " << name << " (count_tag=" << length_tag << ", "
           << presence_to_string(presence) << ")\n";
        os << indent_str << "  Fields:\n";
        for (const auto& f : fields) {
            os << indent_str << "    [" << presence_to_string(f.presence) << "] " << f.name
               << " (tag=" << f.id << ", " << field_type_to_string(f.type) << ")\n";
        }
        for (const auto& ns : nested_sequences) {
            ns.print(os, indent + 4);
        }
    }
};

// ============================================================================
// Sequence Definition
// ============================================================================

struct SequenceDefinition
{
    std::string name; // sequence name (example, "MDEntries")
    int length_tag;   // FIX-tag of the elements counter (example, NoMDEntries = 268)
    Presence presence = Presence::Required;
    std::vector<FieldDefinition> fields;
    std::vector<NestedSequenceDefinition> nested_sequences;

    // Quick fields searching inside a sequence
    const FieldDefinition* find_field_by_name (const std::string& name) const
    {
        for (const auto& f : fields) {
            if (f.name == name) return &f;
        }
        return nullptr;
    }

    const FieldDefinition* find_field_by_id (int tag_id) const
    {
        for (const auto& f : fields) {
            if (f.id == tag_id) return &f;
        }
        return nullptr;
    }

    const NestedSequenceDefinition*
    find_nested_sequence_by_name (const std::string& name) const
    {
        for (const auto& ns : nested_sequences) {
            if (ns.name == name) return &ns;
        }
        return nullptr;
    }
};

// ============================================================================
// Template Definition
// ============================================================================

struct TemplateDefinition
{
    std::string name; // template name (example, "DefaultIncrementalRefreshMessage")
    int id;           // template ID FIX FAST (example, 31)
    std::vector<FieldDefinition> fields;
    std::vector<SequenceDefinition> sequences;

    // Quick fields searching in templates.
    const FieldDefinition* find_field_by_name (const std::string& name) const
    {
        for (const auto& f : fields) {
            if (f.name == name) return &f;
        }
        return nullptr;
    }

    const FieldDefinition* find_field_by_id (int tag_id) const
    {
        for (const auto& f : fields) {
            if (f.id == tag_id) return &f;
        }
        return nullptr;
    }

    const SequenceDefinition* find_sequence_by_name (const std::string& name) const
    {
        for (const auto& s : sequences) {
            if (s.name == name) return &s;
        }
        return nullptr;
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
            for (const auto& f : s.fields) {
                os << indent_str << "        [" << presence_to_string(f.presence) << "] "
                   << f.name << " (tag=" << f.id << ", " << field_type_to_string(f.type)
                   << ")\n";
            }
            for (const auto& ns : s.nested_sequences) {
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

    // Все шаблоны
    std::vector<TemplateDefinition> templates;

    // quick access indexes
    std::unordered_map<std::string, size_t> name_index; // name -> index
    std::unordered_map<int, size_t> id_index;           // template id -> index

    // Add a template
    void add_template (TemplateDefinition tpl)
    {
        size_t idx = templates.size();
        templates.push_back(std::move(tpl));
        name_index[templates[idx].name] = idx;
        id_index[templates[idx].id]     = idx;
    }

    // Lookup by Name
    const TemplateDefinition* find_by_name (const std::string& name) const
    {
        auto it = name_index.find(name);
        if (it != name_index.end()) return &templates[it->second];
        return nullptr;
    }

    // Lookup by Id
    const TemplateDefinition* find_by_id (int id) const
    {
        auto it = id_index.find(id);
        if (it != id_index.end()) return &templates[it->second];
        return nullptr;
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
