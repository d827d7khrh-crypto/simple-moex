add_subdirectory(feedhandler)
add_subdirectory(fix_fast_parser)

add_executable(app
    main.cpp
)
target_link_libraries(app PRIVATE 
    project_headers
    feedhandler
    PkgConfig::LIBXMLXX
    Boost::thread
)

add_subdirectory(udp_mcast)
