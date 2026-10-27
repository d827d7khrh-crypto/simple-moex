#pragma once

#include "fix_types.h"
#include <libxml++/libxml++.h>
#include <string>
#include <memory>

namespace fast
{

// ============================================================================
// Парсер XML-файла шаблонов FIX FAST (spectra-templates.xml)
// ============================================================================

class FastTemplatesParser
{
  public:
    FastTemplatesParser ();
    ~FastTemplatesParser ();

    // parse file
    bool parse_file (const std::string& filepath);

    // parse from XML string
    bool parse_string (const std::string& xml_content);

    // get templates dictionary
    const TemplatesDictionary& get_templates () const;
    TemplatesDictionary& get_templates ();

    // get last error
    const std::string& get_error () const;

  private:
    TemplatesDictionary dictionary_;
    std::string error_;

    // libxml++ parser
    xmlpp::DomParser parser_;

    // auxiliary parsing methods
    bool parse_templates_element (xmlpp::Element* root);
    bool parse_template_element (xmlpp::Element* template_elem);
    bool parse_field_element (xmlpp::Element* field_elem, FieldDefinition& field_def);
    bool parse_sequence_element (xmlpp::Element* seq_elem, SequenceDefinition& seq_def);
    bool parse_nested_sequence_element (xmlpp::Element* seq_elem,
                                        NestedSequenceDefinition& seq_def);

    // XML utility
    static std::string get_attribute (xmlpp::Element* elem, const std::string& attr_name);
    static std::string get_text (xmlpp::Element* elem);
    static FieldType parse_field_type (const std::string& type_name);
    static Presence parse_presence (const std::string& presence);
    static Charset parse_charset (const std::string& charset);

    void set_error (const std::string& msg);
};

} // namespace fast
