#include "xml_test.h"

void test_parse_xml_files (std::string filepath)
{
    try {
        fix::fast::FastTemplatesParser parser;

        if (!parser.parse_file(filepath)) {
            std::cerr << "Failed to parse: " << parser.get_error() << "\n";
            return;
        }

        const auto& dict = parser.get_templates();

        std::cout << "=== MOEX FIX FAST Templates ===\n";
        std::cout << "Namespace: " << dict.namespace_name << "\n";
        std::cout << "Total templates: " << dict.templates.size() << "\n\n";

        // Вывод информации о каждом шаблоне
        for (const auto& tpl : dict.templates) {
            std::cout << "Template: " << tpl.name << " (id=" << tpl.id << ")\n";
            std::cout << "  Fields: " << tpl.fields.size()
                      << ", Sequences: " << tpl.sequences.size() << "\n";

            // Показываем поля с константами
            for (const auto& f : tpl.fields) {
                if (f.is_constant()) {
                    std::cout << "    [CONST] " << f.name
                              << " (tag=" << f.id << ") = "
                              << *f.constant_value << "\n";
                }
            }

            // Показываем последовательности
            for (const auto& s : tpl.sequences) {
                std::cout << "    Sequence: " << s.name
                          << " (count_tag=" << s.length_tag
                          << ", fields=" << s.fields.size()
                          << ", nested_sequences=" << s.nested_sequences.size() << ")\n";
                // Вывод вложенных последовательностей
                for (const auto& ns : s.nested_sequences) {
                    std::cout << "      NestedSequence: " << ns.name
                              << " (count_tag=" << ns.length_tag
                              << ", fields=" << ns.fields.size() << ")\n";
                }
            }
            std::cout << "\n";
        }

        // Пример: поиск конкретного шаблона
        auto* snapshot = dict.find_by_name("SecurityDefinition");
        if (snapshot) {
            std::cout << "=== SecurityDefinition ===\n";
            snapshot->print(std::cout, 0);
        }

        snapshot = dict.find_by_name("X-OLR-FOND");
        if (snapshot) {
            std::cout << "=== X-OLR-FOND ===\n";
            snapshot->print(std::cout, 0);
        }

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return;
    }
}
