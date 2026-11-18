add_library(message_handler INTERFACE)

set(HFFIX "/usr/local/include/hffix")

target_include_directories(message_handler INTERFACE
    "$<BUILD_INTERFACE:${HFFIX}>"
)
