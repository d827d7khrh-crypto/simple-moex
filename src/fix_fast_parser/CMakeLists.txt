
add_library(fix_fast_parser
    fix_fast_parser.cpp
    fast_message_parser.cpp
    fast_message_handler.cpp
)

target_include_directories(fix_fast_parser INTERFACE
    ${LIBXMLXX_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    project_headers
    /usr/local/include
)

target_link_libraries(fix_fast_parser PUBLIC
    project_headers
    PkgConfig::LIBXMLXX
    mfast_static
    mfast_coder_static
    mfast_xml_parser_static
)
