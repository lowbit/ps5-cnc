# The importer's tests on a PC: tools/test-import.sh builds this and runs test/run-tests.py, which
# makes small stand-ins for the discs, archives and folders the importer reads and checks where
# their files end up.
cmake_minimum_required(VERSION 3.16)
project(ra_import_tests C)

set(PS5_PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
include("${PS5_PORT_ROOT}/cmake/import-libs.cmake")
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

set(PORT "${PS5_PORT_ROOT}/src/port")
add_executable(import-test import_test.c "${PORT}/importer.c" "${PORT}/rafiles.c" "${PORT}/sha1.c"
    "${PORT}/tfd.c" "${PORT}/http.c" "${PORT}/url.c" "${PORT}/listing.c")
set_target_properties(import-test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_compile_options(import-test PRIVATE -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -g)
target_link_options(import-test PRIVATE -fsanitize=address,undefined)
target_include_directories(import-test PRIVATE "${PORT}")
target_link_libraries(import-test ra_import_libs CURL::libcurl Threads::Threads)

# The launcher on a PC, with the system's SDL2: tools/test-import.sh runs it with scripted key
# presses and saves its views (see launcher.c), to look at without a console.
find_package(SDL2 REQUIRED)
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${GENERATED}/launcher_font.h"
    COMMAND python3 "${PS5_PORT_ROOT}/tools/make-font.py" "${GENERATED}/launcher_font.h"
    DEPENDS "${PS5_PORT_ROOT}/tools/make-font.py")
add_custom_command(OUTPUT "${GENERATED}/upload_page.h"
    COMMAND python3 "${PS5_PORT_ROOT}/tools/embed-file.py" "${PORT}/upload.html" upload_page "${GENERATED}/upload_page.h"
    DEPENDS "${PS5_PORT_ROOT}/tools/embed-file.py" "${PORT}/upload.html")
add_executable(launcher-test launcher_test.c "${PORT}/launcher.c" "${PORT}/upload.c" "${PORT}/importer.c"
    "${PORT}/rafiles.c" "${PORT}/sha1.c" "${PORT}/tfd.c" "${PORT}/http.c" "${PORT}/url.c" "${PORT}/listing.c"
    "${GENERATED}/launcher_font.h" "${GENERATED}/upload_page.h")
set_target_properties(launcher-test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_compile_options(launcher-test PRIVATE -Wall -Wextra -Wno-unused-parameter -g)
target_include_directories(launcher-test PRIVATE "${PORT}" "${GENERATED}" ${SDL2_INCLUDE_DIRS})
target_link_libraries(launcher-test ra_import_libs ${SDL2_LIBRARIES} CURL::libcurl Threads::Threads)
