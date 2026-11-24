# IJG 6b matches the retail decoder's embedded JVERSION and scalar codec API.
# The fetched release is unmodified; its supplied Windows configuration is
# copied to the build tree. Windows x64 retains 32-bit long / 64-bit size_t.
FetchContent_Declare(jpeg6b
    URL https://www.ijg.org/files/jpegsrc.v6b.tar.gz
    URL_HASH SHA256=75c3ec241e9996504fe02a9ed4d12f16b74ade713972f3db9e65ce95cd27e35d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR opengangstar-unused
)
FetchContent_MakeAvailable(jpeg6b)

if(NOT EXISTS "${jpeg6b_SOURCE_DIR}/jversion.h")
    message(FATAL_ERROR "Expected the IJG jpeg-6b release layout.")
endif()
file(READ "${jpeg6b_SOURCE_DIR}/jversion.h" jpeg_version)
if(NOT jpeg_version MATCHES "6b  27-Mar-1998")
    message(FATAL_ERROR "The native JPEG decoder requires the pinned IJG 6b release.")
endif()
set(jpeg6b_native "${CMAKE_CURRENT_BINARY_DIR}/jpeg6b-native")
configure_file("${jpeg6b_SOURCE_DIR}/jconfig.vc" "${jpeg6b_native}/jconfig.h" COPYONLY)
configure_file("${jpeg6b_SOURCE_DIR}/README" "${jpeg6b_native}/IJG-README.txt" COPYONLY)

# Full release library list from makefile.vc (both codec directions retained),
# with jmemnobs as the original jmemirr-compatible no-backing-store allocator.
set(jpeg6b_sources
    jcapimin.c jcapistd.c jccoefct.c jccolor.c jcdctmgr.c jchuff.c
    jcinit.c jcmainct.c jcmarker.c jcmaster.c jcomapi.c jcparam.c
    jcphuff.c jcprepct.c jcsample.c jctrans.c jdapimin.c jdapistd.c
    jdatadst.c jdatasrc.c jdcoefct.c jdcolor.c jddctmgr.c jdhuff.c
    jdinput.c jdmainct.c jdmarker.c jdmaster.c jdmerge.c jdphuff.c
    jdpostct.c jdsample.c jdtrans.c jerror.c jfdctflt.c jfdctfst.c
    jfdctint.c jidctflt.c jidctfst.c jidctint.c jidctred.c jquant1.c
    jquant2.c jutils.c jmemmgr.c jmemnobs.c)
list(TRANSFORM jpeg6b_sources PREPEND "${jpeg6b_SOURCE_DIR}/")
add_library(opengangstar_jpeg6b STATIC ${jpeg6b_sources} "${CMAKE_CURRENT_LIST_DIR}/../jpeg_decode.c")
target_include_directories(opengangstar_jpeg6b PRIVATE "${jpeg6b_native}" "${jpeg6b_SOURCE_DIR}")
target_compile_features(opengangstar_jpeg6b PRIVATE c_std_11)
if(MSVC)
    target_compile_options(opengangstar_jpeg6b PRIVATE /fp:strict /we4302 /we4311 /we4312)
    target_compile_definitions(opengangstar_jpeg6b PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# IJG's complete unaltered notice accompanies binaries, including build-tree
# executables. Invoke after creating each distributed executable target.
function(opengangstar_copy_jpeg_notice target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${jpeg6b_native}/IJG-README.txt" "$<TARGET_FILE_DIR:${target}>/IJG-README.txt"
        VERBATIM)
endfunction()
