
add_executable(udp_mcst
    main.cpp
)

target_include_directories(udp_mcst PRIVATE ${Boost_INCLUDE_DIRS})
target_include_directories(udp_mcst PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(udp_mcst PRIVATE
    Boost::program_options	
    Boost::thread
)

add_executable(udp_rec
    receiver.cpp
)

target_include_directories(udp_rec PRIVATE ${Boost_INCLUDE_DIRS})
target_include_directories(udp_rec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(udp_rec PRIVATE
    Boost::program_options	
    Boost::thread
)
