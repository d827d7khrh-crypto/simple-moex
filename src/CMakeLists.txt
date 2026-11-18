add_subdirectory(receiver)
add_subdirectory(fix_fast_parser)
add_subdirectory(message_handler)
add_subdirectory(config)

add_executable(app
    main.cpp
)
target_link_libraries(app PRIVATE 
    project_headers
    message_handler
    receiver
    fix_fast_parser
    config
    PkgConfig::LIBXMLXX
    Boost::thread
)

#add_subdirectory(udp_mcast)
