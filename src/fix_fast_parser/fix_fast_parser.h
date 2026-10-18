#pragma once

#include "fix_types.h"
#include <libxml++/libxml++.h>
#include <string>
#include <memory>

namespace fix::fast {

// ============================================================================
// Парсер XML-файла шаблонов FIX FAST (spectra-templates.xml)
// ============================================================================

class FastTemplatesParser {
public:
    FastTemplatesParser();
    ~FastTemplatesParser();

    // Парсинг из файла
    bool parse_file(const std::string& filepath);

    // Парсинг из строки (XML-контент)
    bool parse_string(const std::string& xml_content);

    // Получить распаршенные шаблоны
    const TemplatesDictionary& get_templates() const;
    TemplatesDictionary& get_templates();

    // Получить последнюю ошибку
    const std::string& get_error() const;

private:
    TemplatesDictionary dictionary_;
    std::string error_;

    // libxml++ парсер
    xmlpp::DomParser parser_;

    // Вспомогательные методы парсинга
    bool parse_templates_element(xmlpp::Element* root);
    bool parse_template_element(xmlpp::Element* template_elem);
    bool parse_field_element(xmlpp::Element* field_elem, FieldDefinition& field_def);
    bool parse_sequence_element(xmlpp::Element* seq_elem, SequenceDefinition& seq_def);
    bool parse_nested_sequence_element(xmlpp::Element* seq_elem, NestedSequenceDefinition& seq_def);

    // Утилиты для работы с XML
    static std::string get_attribute(xmlpp::Element* elem, const std::string& attr_name);
    static std::string get_text(xmlpp::Element* elem);
    static FieldType parse_field_type(const std::string& type_name);
    static Presence parse_presence(const std::string& presence);
    static Charset parse_charset(const std::string& charset);

    void set_error(const std::string& msg);
};

} // namespace fix::fast
