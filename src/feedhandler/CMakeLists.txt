
add_library(feedhandler
    udp_multicast_receiver.cpp
)

target_include_directories(feedhandler PUBLIC
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(feedhandler INTERFACE
    project_headers
    PkgConfig::LIBXMLXX
    Boost::program_options	
    Boost::thread
)
