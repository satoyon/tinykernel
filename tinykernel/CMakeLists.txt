# tinykernel: minimal preemptive task scheduler library (RP2350 / Pico 2)

add_library(tinykernel STATIC
    src/scheduler.cpp
    src/port/m33/port_m33.c
    src/port/m33/context.S
)

target_include_directories(tinykernel PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(tinykernel PUBLIC
    pico_stdlib
)

