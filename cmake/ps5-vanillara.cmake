# Included into Vanilla Conquer's project (CMAKE_PROJECT_INCLUDE) by tools/build-ps5.sh: adds the PS5
# port's sources (the launcher, the importer and the upload server) to the VanillaRA target once
# Vanilla Conquer's CMake files have defined it, with the libraries the importer compiles in.
function(ps5_port)
    include("${PS5_PORT_ROOT}/cmake/import-libs.cmake")

    # The launcher's fonts and the upload page, as C headers.
    set(generated "${CMAKE_BINARY_DIR}/ps5-generated")
    add_custom_command(OUTPUT "${generated}/launcher_font.h"
        COMMAND python3 "${PS5_PORT_ROOT}/tools/make-font.py" "${generated}/launcher_font.h"
        DEPENDS "${PS5_PORT_ROOT}/tools/make-font.py")
    add_custom_command(OUTPUT "${generated}/upload_page.h"
        COMMAND python3 "${PS5_PORT_ROOT}/tools/embed-file.py" "${PS5_PORT_ROOT}/src/port/upload.html"
            upload_page "${generated}/upload_page.h"
        DEPENDS "${PS5_PORT_ROOT}/tools/embed-file.py" "${PS5_PORT_ROOT}/src/port/upload.html")

    # VanillaRA is defined in another folder of the project, which a custom command's output cannot
    # reach: a target makes them before it is built.
    add_custom_target(ps5_generated DEPENDS "${generated}/launcher_font.h" "${generated}/upload_page.h")
    add_dependencies(VanillaRA ps5_generated)

    file(GLOB port_sources CONFIGURE_DEPENDS "${PS5_PORT_ROOT}/src/port/*.cpp" "${PS5_PORT_ROOT}/src/port/*.c")
    target_sources(VanillaRA PRIVATE ${port_sources})
    target_include_directories(VanillaRA PRIVATE "${PS5_PORT_ROOT}/src/runtime" "${PS5_PORT_ROOT}/src/port"
        "${generated}" "${PS5_PREFIX}/include/SDL2")
    # The static OpenAL library plays through SDL2, which has to come after it.
    target_link_libraries(VanillaRA ra_import_libs "${PS5_PREFIX}/lib/libSDL2.a")
    # tools/ps5-link adds these to every link; relink when they change.
    file(GLOB stubs CONFIGURE_DEPENDS "${PS5_PREFIX}/stubs/*.so")
    set_property(TARGET VanillaRA APPEND PROPERTY LINK_DEPENDS
        "${PS5_PREFIX}/lib/libps5runtime.a" "${PS5_PREFIX}/lib/ps5-crt.o"
        "${PS5_PORT_ROOT}/src/runtime/ps5.ld" "${PS5_PORT_ROOT}/tools/ps5-link" ${stubs})
endfunction()
cmake_language(DEFER CALL ps5_port)
