# tinykernel: minimal preemptive task scheduler library (RP2350 / Pico 2)

option(TK_ENABLE_SMP "Enable dual-core SMP support in tinykernel" ON)

add_library(tinykernel_lib STATIC
    src/scheduler.cpp
    src/port/m33/port_m33.c
    src/port/m33/context.S
)

target_include_directories(tinykernel_lib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(tinykernel_lib PUBLIC
    pico_stdlib
    pico_multicore
    hardware_sync
)

if(NOT TK_ENABLE_SMP)
    target_compile_definitions(tinykernel_lib PUBLIC TK_ENABLE_SMP=0)
    message(STATUS "tinykernel: SMP disabled (single-core mode on Core 0)")
else()
    target_compile_definitions(tinykernel_lib PUBLIC TK_ENABLE_SMP=1)
    message(STATUS "tinykernel: SMP enabled (dual-core mode)")
endif()
