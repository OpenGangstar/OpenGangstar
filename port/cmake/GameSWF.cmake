# Use the single-threaded GameSWF core with our own Windows renderer integration.
FetchContent_Declare(gameswf
    URL https://codeload.github.com/killerdevildog/gameswf/tar.gz/6385d75c9c02c0e4059ce8629854406c3fa9063d
    URL_HASH SHA256=432aad386dde5bec0a7169994ba1ed31980b4531f7ba8a5bfd2dd6a6435e91b3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR opengangstar-unused
)
FetchContent_MakeAvailable(gameswf)

function(patch_gameswf path before after)
    file(READ "${path}" contents)
    string(FIND "${contents}" "${after}" patched)
    if(patched EQUAL -1)
        string(FIND "${contents}" "${before}" original)
        if(original EQUAL -1)
            message(FATAL_ERROR "GameSWF compatibility patch did not match ${path}")
        endif()
        string(REPLACE "${before}" "${after}" contents "${contents}")
        file(WRITE "${path}" "${contents}")
    endif()
endfunction()

if(MSVC)
    patch_gameswf("${gameswf_SOURCE_DIR}/base/tu_math.h"
        [=[#include <math.h>]=]
        [=[#include <math.h>
#include <cmath>]=])
    patch_gameswf("${gameswf_SOURCE_DIR}/gameswf/gameswf_player.cpp"
        [=["__DATE__" "__TIME__]=]
        [=[" __DATE__ " " __TIME__]=])
endif()

# The game uses exported JPEG tags as placeholders for external textures.
patch_gameswf("${gameswf_SOURCE_DIR}/gameswf/gameswf_impl.cpp"
    [=[IF_VERBOSE_PARSE(log_msg("  define_bits_jpeg3_loader: charid = %d pos = 0x%x\n", character_id, in->get_position()));]=]
    [=[IF_VERBOSE_PARSE(log_msg("  define_bits_jpeg3_loader: charid = %d pos = 0x%x\n", character_id, in->get_position()));

        extern bitmap_info* load_opengangstar_bitmap(Uint16 id);
        if (bitmap_info* external = load_opengangstar_bitmap(character_id)) {
            m->add_bitmap_character(character_id, new bitmap_character(m, external));
            return;
        }]=])
patch_gameswf("${gameswf_SOURCE_DIR}/gameswf/gameswf_impl.cpp"
    [=[else if (sound_sample* ch = m->get_sound_sample(id))]=]
    [=[else if (bitmap_character_def* bitmap = m->get_bitmap_character(id))
            {
                m->export_resource(resource_name, bitmap);
            }
            else if (sound_sample* ch = m->get_sound_sample(id))]=])

# Register the retail device font before the SWF initializes its text fields.
patch_gameswf("${gameswf_SOURCE_DIR}/gameswf/gameswf_movie_def.cpp"
    [=[{
	}

	movie_def_impl::~movie_def_impl()]=]
    [=[{
        extern font* load_opengangstar_font(gameswf::player* player);
        if (font* external = load_opengangstar_font(player)) {
            add_font(-1, external);
        }
	}

	movie_def_impl::~movie_def_impl()]=])

set(gameswf_sources
    base/configvars.cpp
    base/container.cpp
    base/dlmalloc.c
    base/ear_clip_triangulate_float.cpp
    base/ear_clip_triangulate_sint16.cpp
    base/file_util.cpp
    base/image.cpp
    base/image_filters.cpp
    base/jpeg.cpp
    base/logger.cpp
    base/membuf.cpp
    base/png_helper.cpp
    base/postscript.cpp
    base/triangulate_float.cpp
    base/triangulate_sint32.cpp
    base/tu_file.cpp
    base/tu_gc_singlethreaded_marksweep.cpp
    base/tu_loadlib.cpp
    base/tu_random.cpp
    base/tu_timer.cpp
    base/tu_types.cpp
    base/utf8.cpp
    base/utility.cpp
    base/zlib_adapter.cpp
    gameswf/gameswf_abc.cpp
    gameswf/gameswf_action.cpp
    gameswf/gameswf_as_sprite.cpp
    gameswf/gameswf_avm2.cpp
    gameswf/gameswf_button.cpp
    gameswf/gameswf_canvas.cpp
    gameswf/gameswf_character.cpp
    gameswf/gameswf_disasm.cpp
    gameswf/gameswf_dlist.cpp
    gameswf/gameswf_environment.cpp
    gameswf/gameswf_filters.cpp
    gameswf/gameswf_font.cpp
    gameswf/gameswf_fontlib.cpp
    gameswf/gameswf_freetype.cpp
    gameswf/gameswf_function.cpp
    gameswf/gameswf_impl.cpp
    gameswf/gameswf_listener.cpp
    gameswf/gameswf_log.cpp
    gameswf/gameswf_morph2.cpp
    gameswf/gameswf_movie_def.cpp
    gameswf/gameswf_mutex.cpp
    gameswf/gameswf_object.cpp
    gameswf/gameswf_parser.cpp
    gameswf/gameswf_player.cpp
    gameswf/gameswf_render.cpp
    gameswf/gameswf_root.cpp
    gameswf/gameswf_shape.cpp
    gameswf/gameswf_sound.cpp
    gameswf/gameswf_sprite.cpp
    gameswf/gameswf_sprite_def.cpp
    gameswf/gameswf_stream.cpp
    gameswf/gameswf_styles.cpp
    gameswf/gameswf_tesselate.cpp
    gameswf/gameswf_text.cpp
    gameswf/gameswf_tools.cpp
    gameswf/gameswf_types.cpp
    gameswf/gameswf_value.cpp
    gameswf/gameswf_video_impl.cpp
    gameswf/gameswf_as_classes/as_array.cpp
    gameswf/gameswf_as_classes/as_boolean.cpp
    gameswf/gameswf_as_classes/as_broadcaster.cpp
    gameswf/gameswf_as_classes/as_class.cpp
    gameswf/gameswf_as_classes/as_color.cpp
    gameswf/gameswf_as_classes/as_color_transform.cpp
    gameswf/gameswf_as_classes/as_date.cpp
    gameswf/gameswf_as_classes/as_event.cpp
    gameswf/gameswf_as_classes/as_flash.cpp
    gameswf/gameswf_as_classes/as_geom.cpp
    gameswf/gameswf_as_classes/as_global.cpp
    gameswf/gameswf_as_classes/as_key.cpp
    gameswf/gameswf_as_classes/as_loadvars.cpp
    gameswf/gameswf_as_classes/as_math.cpp
    gameswf/gameswf_as_classes/as_matrix.cpp
    gameswf/gameswf_as_classes/as_mcloader.cpp
    gameswf/gameswf_as_classes/as_mouse.cpp
    gameswf/gameswf_as_classes/as_mouse_event.cpp
    gameswf/gameswf_as_classes/as_netconnection.cpp
    gameswf/gameswf_as_classes/as_netstream.cpp
    gameswf/gameswf_as_classes/as_number.cpp
    gameswf/gameswf_as_classes/as_point.cpp
    gameswf/gameswf_as_classes/as_selection.cpp
    gameswf/gameswf_as_classes/as_sharedobject.cpp
    gameswf/gameswf_as_classes/as_sound.cpp
    gameswf/gameswf_as_classes/as_string.cpp
    gameswf/gameswf_as_classes/as_textformat.cpp
    gameswf/gameswf_as_classes/as_transform.cpp
    gameswf/gameswf_as_classes/as_xml.cpp
    gameswf/gameswf_as_classes/as_xmlsocket.cpp
)
list(TRANSFORM gameswf_sources PREPEND "${gameswf_SOURCE_DIR}/")
add_library(gameswf_core STATIC ${gameswf_sources})
target_compile_features(gameswf_core PUBLIC cxx_std_17)
target_compile_definitions(gameswf_core
    PUBLIC TU_CONFIG_LINK_STATIC=1 TU_USE_SDL=0 TU_CONFIG_LINK_TO_THREAD=0
        TU_CONFIG_LINK_TO_JPEGLIB=0 TU_CONFIG_LINK_TO_LIBPNG=0 TU_ENABLE_NETWORK=0
    PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX
)
target_include_directories(gameswf_core SYSTEM PUBLIC "${gameswf_SOURCE_DIR}")
target_link_libraries(gameswf_core PRIVATE ZLIB::ZLIBSTATIC winmm ws2_32)
