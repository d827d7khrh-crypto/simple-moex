#include "fix_fast_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace fix::fast {

// ============================================================================
// Конструктор / Деструктор
// ============================================================================

FastTemplatesParser::FastTemplatesParser() = default;
FastTemplatesParser::~FastTemplatesParser() = default;

// ============================================================================
// Утилиты
// ============================================================================

std::string FastTemplatesParser::get_attribute(xmlpp::Element* elem, const std::string& attr_name) {
    if (!elem) return {};
    return elem->get_attribute_value(attr_name);
}

FieldType FastTemplatesParser::parse_field_type(const std::string& type_name) {
    // libxml++ возвращает имя тега элемента, который определяет тип:
    // <string>, <uInt32>, <uInt64>, <int32>, <int64>, <decimal>
    if (type_name == "string")   return FieldType::String;
    if (type_name == "uInt32")   return FieldType::UInt32;
    if (type_name == "uInt64")   return FieldType::UInt64;
    if (type_name == "int32")    return FieldType::Int32;
    if (type_name == "int64")    return FieldType::Int64;
    if (type_name == "decimal")  return FieldType::Decimal;
    return FieldType::String; // fallback
}

Presence FastTemplatesParser::parse_presence(const std::string& presence) {
    if (presence == "optional") return Presence::Optional;
    return Presence::Required;
}

Charset FastTemplatesParser::parse_charset(const std::string& charset) {
    if (charset == "unicode") return Charset::Unicode;
    return Charset::Default;
}

void FastTemplatesParser::set_error(const std::string& msg) {
    error_ = msg;
    std::cerr << "[FAST Parser ERROR] " << msg << "\n";
}

// ============================================================================
// Парсинг из файла
// ============================================================================

bool FastTemplatesParser::parse_file(const std::string& filepath) {
    error_.clear();

    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            set_error("Cannot open file: " + filepath);
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string xml_content = buffer.str();

        return parse_string(xml_content);
    }
    catch (const std::exception& e) {
        set_error(std::string("Exception while reading file: ") + e.what());
        return false;
    }
}

// ============================================================================
// Парсинг из строки
// ============================================================================

bool FastTemplatesParser::parse_string(const std::string& xml_content) {
    error_.clear();
    dictionary_ = TemplatesDictionary{};

    try {
        parser_.parse_memory(xml_content);

        if (!parser_.get_document()) {
            set_error("Failed to parse XML document");
            return false;
        }

        xmlpp::Element* root = dynamic_cast<xmlpp::Element*>(parser_.get_document()->get_root_node());
        if (!root) {
            set_error("XML document has no root element");
            return false;
        }

        // Получаем имя namespace (например, "templates")
        dictionary_.namespace_name = root->get_name();

        return parse_templates_element(root);
    }
    catch (const xmlpp::exception& e) {
        set_error(std::string("XML parse error: ") + e.what());
        return false;
    }
    catch (const std::exception& e) {
        set_error(std::string("Exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Получение результатов
// ============================================================================

const TemplatesDictionary& FastTemplatesParser::get_templates() const {
    return dictionary_;
}

TemplatesDictionary& FastTemplatesParser::get_templates() {
    return dictionary_;
}

const std::string& FastTemplatesParser::get_error() const {
    return error_;
}

// ============================================================================
// Разбор элемента <templates>
// ============================================================================

bool FastTemplatesParser::parse_templates_element(xmlpp::Element* root) {
    // Итерируем все дочерние элементы <template>
    auto children = root->get_children();
    for (auto* child : children) {
        if (auto* template_elem = dynamic_cast<xmlpp::Element*>(child)) {
            if (template_elem->get_name() == "template") {
                if (!parse_template_element(template_elem)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// ============================================================================
// Разбор элемента <template>
// ============================================================================

bool FastTemplatesParser::parse_template_element(xmlpp::Element* template_elem) {
    TemplateDefinition tpl;

    // Основные атрибуты шаблона
    tpl.name = get_attribute(template_elem, "name");
    auto id_attr = get_attribute(template_elem, "id");
    if (!id_attr.empty()) {
        try {
            tpl.id = std::stoi(id_attr);
        }
        catch (...) {
            set_error("Invalid template id: " + id_attr);
            return false;
        }
    }

    if (tpl.name.empty()) {
        set_error("Template element has no name attribute");
        return false;
    }

    // Итерируем дочерние элементы
    auto children = template_elem->get_children();
    for (auto* child : children) {
        if (auto* child_elem = dynamic_cast<xmlpp::Element*>(child)) {
            std::string tag_name = child_elem->get_name();

            if (tag_name == "sequence") {
                // Это вложенная последовательность
                SequenceDefinition seq_def;
                if (!parse_sequence_element(child_elem, seq_def)) {
                    return false;
                }
                tpl.sequences.push_back(std::move(seq_def));
            }
            else {
                // Это поле (string, uInt32, int64, decimal, и т.д.)
                FieldDefinition field_def;
                if (!parse_field_element(child_elem, field_def)) {
                    return false;
                }
                tpl.fields.push_back(std::move(field_def));
            }
        }
    }

    dictionary_.add_template(std::move(tpl));
    return true;
}

// ============================================================================
// Разбор элемента поля (string, uInt32, int64, decimal, и т.д.)
// ============================================================================

bool FastTemplatesParser::parse_field_element(xmlpp::Element* field_elem, FieldDefinition& field_def) {
    // Имя тега элемента определяет тип (string, uInt32, int64, decimal, и т.д.)
    field_def.type = parse_field_type(field_elem->get_name());

    // Извлекаем атрибуты
    field_def.name = get_attribute(field_elem, "name");
    auto id_attr = get_attribute(field_elem, "id");

    if (!id_attr.empty()) {
        try {
            field_def.id = std::stoi(id_attr);
        }
        catch (...) {
            set_error("Invalid field id: " + id_attr);
            return false;
        }
    }

    auto presence_attr = get_attribute(field_elem, "presence");
    if (!presence_attr.empty()) {
        field_def.presence = parse_presence(presence_attr);
    }

    auto charset_attr = get_attribute(field_elem, "charset");
    if (!charset_attr.empty()) {
        field_def.charset = parse_charset(charset_attr);
    }

    // Проверяем наличие константного значения (<constant value="..."/>)
    for (auto* child : field_elem->get_children()) {
        auto* constant_elem = dynamic_cast<xmlpp::Element*>(child);
        if (constant_elem && constant_elem->get_name() == "constant") {
            field_def.constant_value = get_attribute(constant_elem, "value");
            break;
        }
    }

    if (field_def.name.empty()) {
        set_error("Field element has no name attribute (type=" + field_elem->get_name() + ")");
        return false;
    }

    return true;
}

// ============================================================================
// Разбор элемента последовательности <sequence>
// ============================================================================

bool FastTemplatesParser::parse_sequence_element(xmlpp::Element* seq_elem, SequenceDefinition& seq_def) {
    seq_def.name = get_attribute(seq_elem, "name");

    auto presence_attr = get_attribute(seq_elem, "presence");
    if (!presence_attr.empty()) {
        seq_def.presence = parse_presence(presence_attr);
    }

    // Итерируем дочерние элементы последовательности
    auto children = seq_elem->get_children();
    for (auto* child : children) {
        if (auto* child_elem = dynamic_cast<xmlpp::Element*>(child)) {
            std::string tag_name = child_elem->get_name();

            // Первый элемент с length (например, <length name="NoMDEntries" id="268"/>)
            // определяет тег счётчика элементов последовательности
            if (tag_name == "length") {
                seq_def.length_tag = std::stoi(get_attribute(child_elem, "id"));
            }
            else if (tag_name == "sequence") {
                // Это вложенная последовательность рекурсивно
                NestedSequenceDefinition nested_seq_def;
                if (!parse_nested_sequence_element(child_elem, nested_seq_def)) {
                    return false;
                }
                seq_def.nested_sequences.push_back(std::move(nested_seq_def));
            }
            else {
                // Остальные — поля последовательности
                FieldDefinition field_def;
                if (!parse_field_element(child_elem, field_def)) {
                    return false;
                }
                seq_def.fields.push_back(std::move(field_def));
            }
        }
    }

    if (seq_def.name.empty()) {
        set_error("Sequence element has no name attribute");
        return false;
    }

    if (seq_def.length_tag == 0) {
        set_error("Sequence '" + seq_def.name + "' has no length (count) element");
        return false;
    }

    return true;
}

// ============================================================================
// Разбор элемента вложенной последовательности <sequence>
// ============================================================================

bool FastTemplatesParser::parse_nested_sequence_element(xmlpp::Element* seq_elem, NestedSequenceDefinition& seq_def) {
    seq_def.name = get_attribute(seq_elem, "name");

    auto presence_attr = get_attribute(seq_elem, "presence");
    if (!presence_attr.empty()) {
        seq_def.presence = parse_presence(presence_attr);
    }

    // Итерируем дочерние элементы последовательности
    auto children = seq_elem->get_children();
    for (auto* child : children) {
        if (auto* child_elem = dynamic_cast<xmlpp::Element*>(child)) {
            std::string tag_name = child_elem->get_name();

            // Первый элемент с length определяет тег счётчика элементов последовательности
            if (tag_name == "length") {
                seq_def.length_tag = std::stoi(get_attribute(child_elem, "id"));
            }
            else if (tag_name == "sequence") {
                // Это вложенная последовательность рекурсивно
                NestedSequenceDefinition nested_seq_def;
                if (!parse_nested_sequence_element(child_elem, nested_seq_def)) {
                    return false;
                }
                seq_def.nested_sequences.push_back(std::move(nested_seq_def));
            }
            else {
                // Остальные — поля последовательности
                FieldDefinition field_def;
                if (!parse_field_element(child_elem, field_def)) {
                    return false;
                }
                seq_def.fields.push_back(std::move(field_def));
            }
        }
    }

    if (seq_def.name.empty()) {
        set_error("Nested sequence element has no name attribute");
        return false;
    }

    if (seq_def.length_tag == 0) {
        set_error("Nested sequence '" + seq_def.name + "' has no length (count) element");
        return false;
    }

    return true;
}

} // namespace fix::fast
