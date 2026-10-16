#include <iostream>
#include <libxml++/libxml++.h>


int main() {
    try {
        // Создаем парсер документов
        xmlpp::DomParser parser;
        
        // Парсим XML из строки (или используем parser.parse_file("path.xml"))
        parser.parse_memory("<root><element status='working'>Hello from MacPorts!</element></root>");
        
        if (parser) {
            // Получаем корневой элемент
            const auto* root = parser.get_document()->get_root_node();
            
            // Находим дочерний узел
            for (const auto* node : root->get_children()) {
                const auto* element = dynamic_cast<const xmlpp::Element*>(node);
                if (element) {
                    std::cout << "Текст элемента: " << element->get_first_child_text()->get_content() << std::endl;
                    std::cout << "Атрибут status: " << element->get_attribute_value("status") << std::endl;
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Произошла ошибка: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
