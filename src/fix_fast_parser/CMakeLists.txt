
add_library(fix_fast_parser
    fix_fast_parser.cpp
)

message("LIBXMLXX: ${LIBXMLXX_INCLUDE_DIRS}")
target_include_directories(fix_fast_parser PUBLIC
    ${LIBXMLXX_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(fix_fast_parser INTERFACE
    project_headers
    PkgConfig::LIBXMLXX
)
