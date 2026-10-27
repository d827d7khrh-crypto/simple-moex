add_library(config
    config.cpp
)

target_include_directories(config INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
    /opt/local/include
)

target_link_libraries(config PRIVATE
    PkgConfig::LIBXMLXX
    Boost::program_options
    toml11::toml11
)
