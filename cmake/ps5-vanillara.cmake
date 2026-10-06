# Included into Vanilla Conquer's project (CMAKE_PROJECT_INCLUDE) by tools/build-ps5.sh: adds the PS5
# port's sources to the VanillaRA target once Vanilla Conquer's CMake files have defined it.
function(ps5_port)
    file(GLOB port_sources CONFIGURE_DEPENDS "${PS5_PORT_ROOT}/src/port/*.cpp" "${PS5_PORT_ROOT}/src/port/*.c")
    target_sources(VanillaRA PRIVATE ${port_sources})
    target_include_directories(VanillaRA PRIVATE "${PS5_PORT_ROOT}/src/runtime" "${PS5_PREFIX}/include/SDL2")
    # The static OpenAL library plays through SDL2, which has to come after it.
    target_link_libraries(VanillaRA "${PS5_PREFIX}/lib/libSDL2.a")
    # tools/ps5-link adds these to every link; relink when they change.
    file(GLOB stubs CONFIGURE_DEPENDS "${PS5_PREFIX}/stubs/*.so")
    set_property(TARGET VanillaRA APPEND PROPERTY LINK_DEPENDS
        "${PS5_PREFIX}/lib/libps5runtime.a" "${PS5_PREFIX}/lib/ps5-crt.o"
        "${PS5_PORT_ROOT}/src/runtime/ps5.ld" "${PS5_PORT_ROOT}/tools/ps5-link" ${stubs})
endfunction()
cmake_language(DEFER CALL ps5_port)
