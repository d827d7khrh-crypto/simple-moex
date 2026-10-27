# Создаем отдельный исполняемый файл для тестов
add_executable(xml_test main.cpp)

# Линкуем библиотеку libxml++, которую корневой CMake уже нашел
target_link_libraries(xml_test PRIVATE PkgConfig::LIBXMLXX)

