FetchContent_Declare(ogg
    URL https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.xz
    URL_HASH SHA256=5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(vorbis
    URL https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.xz
    URL_HASH SHA256=b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

function(add_retail_vorbis)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.13)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(INSTALL_DOCS OFF)
    set(INSTALL_PKG_CONFIG_MODULE OFF)
    set(INSTALL_CMAKE_PACKAGE_MODULE OFF)
    FetchContent_MakeAvailable(ogg)
    # Vorbis's find module expects these variables even when Ogg::ogg exists.
    set(OGG_INCLUDE_DIR "${ogg_SOURCE_DIR}/include")
    set(OGG_LIBRARY ogg)
    FetchContent_MakeAvailable(vorbis)
endfunction()

add_retail_vorbis()
