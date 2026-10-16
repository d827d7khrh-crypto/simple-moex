cmake_minimum_required(VERSION 3.20)
project(x-moex LANGUAGES C CXX)

# Устанавливаем стандарт C++20 (можно изменить на 17 или 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Указываем CMake искать заголовочные файлы и библиотеки в MacPorts
list(APPEND CMAKE_PREFIX_PATH "/opt/local")

# Находим инструмент PkgConfig
find_package(PkgConfig REQUIRED)

# Находим библиотеку libxml++-5.0 с помощью PkgConfig
# Имя модуля "libxml++-5.0" берется из файла libxml++-5.0.pc в MacPorts
pkg_check_modules(LIBXMLXX REQUIRED IMPORTED_TARGET libxml++-5.0)

add_subdirectory(tests)
add_subdirectory(src)
