# The libraries the importer compiles in, from the sources tools/build-deps.sh unpacks into .deps/src,
# with the settings in third_party/ (libarchive's and liblzma's are the DOOM port's): libarchive's
# readers for ZIP, 7Z, RAR and ISO 9660, liblzma's decoders, zlib's inflate, qrcodegen and unshield.
# Included by cmake/ps5-vanillara.cmake for the console and test/CMakeLists.txt for the PC.
set(IMPORT_DEPS "${PS5_PORT_ROOT}/.deps/src")
set(LA_DIR "${IMPORT_DEPS}/libarchive-3.8.9/libarchive")
set(XZ_DIR "${IMPORT_DEPS}/xz-5.8.4/src/liblzma")
set(ZL_DIR "${IMPORT_DEPS}/zlib-1.3.2")
set(QR_DIR "${IMPORT_DEPS}/QR-Code-generator-1.8.0/c")
set(US_DIR "${IMPORT_DEPS}/unshield-1.6.2/lib")

set(LA_FILES archive_read.c archive_read_set_options.c archive_options.c archive_util.c archive_string.c
    archive_string_sprintf.c archive_entry.c archive_check_magic.c archive_virtual.c
    archive_read_support_format_zip.c archive_read_support_format_7zip.c archive_read_support_format_rar.c
    archive_read_support_format_rar5.c archive_read_support_format_iso9660.c archive_ppmd7.c
    archive_ppmd8.c archive_blake2s_ref.c archive_blake2sp_ref.c archive_cryptor.c archive_hmac.c
    archive_read_add_passphrase.c archive_time.c archive_acl.c archive_entry_xattr.c
    archive_entry_sparse.c archive_rb.c archive_random.c)
set(XZ_FILES common/common.c common/alone_decoder.c common/block_decoder.c common/block_header_decoder.c
    common/block_util.c common/filter_common.c common/filter_decoder.c common/filter_flags_decoder.c
    common/index_hash.c common/stream_decoder.c common/stream_flags_common.c common/stream_flags_decoder.c
    common/vli_decoder.c common/vli_size.c common/easy_preset.c check/check.c check/crc32_fast.c
    check/crc64_fast.c check/sha256.c lz/lz_decoder.c lzma/lzma_decoder.c lzma/lzma2_decoder.c
    lzma/lzma_encoder_presets.c delta/delta_common.c delta/delta_decoder.c simple/simple_coder.c
    simple/simple_decoder.c simple/x86.c simple/arm.c simple/armthumb.c simple/arm64.c simple/ia64.c
    simple/powerpc.c simple/sparc.c simple/riscv.c)
set(ZL_FILES adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c)
set(US_FILES component.c converter.c directory.c file.c file_group.c helper.c libunshield.c log.c md5/md5c.c)

list(TRANSFORM LA_FILES PREPEND "${LA_DIR}/" OUTPUT_VARIABLE LA_SRC)
list(TRANSFORM XZ_FILES PREPEND "${XZ_DIR}/" OUTPUT_VARIABLE XZ_SRC)
list(TRANSFORM ZL_FILES PREPEND "${ZL_DIR}/" OUTPUT_VARIABLE ZL_SRC)
list(TRANSFORM US_FILES PREPEND "${US_DIR}/" OUTPUT_VARIABLE US_SRC)

add_library(ra_import_libs STATIC ${LA_SRC} ${XZ_SRC} ${ZL_SRC} "${QR_DIR}/qrcodegen.c" ${US_SRC})
set_target_properties(ra_import_libs PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_compile_options(ra_import_libs PRIVATE -O2 -w -fno-strict-aliasing)
target_compile_definitions(ra_import_libs PRIVATE NDEBUG)
set_source_files_properties(${LA_SRC} PROPERTIES COMPILE_DEFINITIONS HAVE_CONFIG_H
    INCLUDE_DIRECTORIES "${PS5_PORT_ROOT}/third_party/libarchive;${LA_DIR};${XZ_DIR}/api;${ZL_DIR}")
set_source_files_properties(${XZ_SRC} PROPERTIES COMPILE_DEFINITIONS HAVE_CONFIG_H
    INCLUDE_DIRECTORIES "${PS5_PORT_ROOT}/third_party/xz;${XZ_DIR}/api;${XZ_DIR}/common;${XZ_DIR}/check;${XZ_DIR}/lz;${XZ_DIR}/rangecoder;${XZ_DIR}/lzma;${XZ_DIR}/delta;${XZ_DIR}/simple;${XZ_DIR}/../common")
set_source_files_properties(${US_SRC} PROPERTIES
    INCLUDE_DIRECTORIES "${PS5_PORT_ROOT}/third_party/unshield;${US_DIR};${US_DIR}/md5;${ZL_DIR}")
# What the importer's own code includes.
target_include_directories(ra_import_libs PUBLIC "${LA_DIR}" "${QR_DIR}" "${US_DIR}" "${ZL_DIR}")
