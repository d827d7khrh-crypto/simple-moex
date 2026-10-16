
# Добавляем наш исполняемый файл и исходный код
add_executable(app main.cpp)

# Линкуем библиотеку libxml++, которую корневой CMake уже нашел
target_link_libraries(app PRIVATE PkgConfig::LIBXMLXX)
