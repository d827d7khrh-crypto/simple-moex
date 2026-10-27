
add_library(receiver
    udp_multicast_receiver.cpp
)

target_include_directories(receiver INTERFACE
    ${Boost_INCLUDE_DIRS}
    project_headers
)

target_link_libraries(receiver PUBLIC
    project_headers
    PkgConfig::LIBXMLXX
    Boost::program_options	
    Boost::thread
)
