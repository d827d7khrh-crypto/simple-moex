cmake_minimum_required(VERSION 3.20)
project(x-moex LANGUAGES C CXX)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# Устанавливаем стандарт C++20 (можно изменить на 17 или 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Указываем CMake искать заголовочные файлы и библиотеки в MacPorts
list(APPEND CMAKE_PREFIX_PATH "/opt/local/libexec/boost/1.91/lib/cmake/Boost-1.91.0")
list(APPEND CMAKE_PREFIX_PATH "/opt/local")

set(Boost_ROOT "/opt/local/libexec/boost/1.91")
set(Boost_NO_SYSTEM_PATHS ON) # Запрещаем CMake смотреть в другие папки

# Находим инструмент PkgConfig
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED CONFIG COMPONENTS thread program_options)

pkg_check_modules(LIBXMLXX REQUIRED IMPORTED_TARGET libxml++-5.0)

# Создаём цель, которая просто хранит пути к include
add_library(project_headers INTERFACE)

target_include_directories(project_headers INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/feedhandler>"
    "$<INSTALL_INTERFACE:src/feedhandler>"
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/fix_fast_parser>"
    "$<INSTALL_INTERFACE:src/fix_fast_parser>"
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/misc>"
    "$<INSTALL_INTERFACE:src/misc>"
)

add_subdirectory(src)
add_subdirectory(tests)
