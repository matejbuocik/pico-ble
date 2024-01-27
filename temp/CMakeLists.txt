set(LIB temp)

add_library(${LIB} ${LIB}.h ${LIB}.c)

target_include_directories(${LIB} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${LIB} pico_stdlib hardware_adc)
