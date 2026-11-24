# Use the historical release matching the retail b2_version and shape-based API.
FetchContent_Declare(box2d
    URL https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/box2d/Box2D_v2.0.1.zip
    URL_HASH SHA256=62857048aa089b558561074154430883cee491eedd71247f75f488cba859e21f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR opengangstar-unused
)
FetchContent_MakeAvailable(box2d)

set(box2d_original "${box2d_SOURCE_DIR}/Box2D")
set(box2d_native "${CMAKE_CURRENT_BINARY_DIR}/box2d-native")
if(NOT EXISTS "${box2d_original}/Source/Dynamics/b2World.cpp")
    message(FATAL_ERROR "Expected the Box2D 2.0.1 release layout (Box2D/Source).")
endif()

function(replace_box2d_native variable before after)
    string(FIND "${${variable}}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Box2D native adaptation did not match: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" changed "${${variable}}")
    set(${variable} "${changed}" PARENT_SCOPE)
endfunction()

# Keep the fetched release and the matching ARM project unchanged. Adapt copies
# for native pointer alignment and the independently verified retail proxy limit.
file(GLOB_RECURSE box2d_files RELATIVE "${box2d_original}"
    "${box2d_original}/Source/*.h" "${box2d_original}/Source/*.cpp"
    "${box2d_original}/Include/*.h")
foreach(relative IN LISTS box2d_files)
    file(READ "${box2d_original}/${relative}" contents)
    string(REPLACE "\r\n" "\n" contents "${contents}")
    if(relative STREQUAL "Source/Common/b2Settings.h")
        replace_box2d_native(contents "#include <math.h>"
            "#include <math.h>\n// OpenGangstar native adaptation: host declarations and allocator alignment.\n#include <cstddef>\n#include <cstring>\n#include <climits>")
        replace_box2d_native(contents "b2_maxProxies = 512" "b2_maxProxies = 1024")
    elseif(relative STREQUAL "Source/Dynamics/b2World.h")
        replace_box2d_native(contents "int32 GetProxyCount() const;"
            "int32 GetProxyCount() const;\n\n\t// OpenGangstar native adaptation: expose the world's authoritative proxy-range predicate.\n\tbool InRange(const b2AABB& aabb) const;")
    elseif(relative STREQUAL "Source/Dynamics/b2World.cpp")
        replace_box2d_native(contents "int32 b2World::GetProxyCount() const"
            "// OpenGangstar native adaptation: no layout or broad-phase behavior change.\nbool b2World::InRange(const b2AABB& aabb) const\n{\n\treturn m_broadPhase->InRange(aabb);\n}\n\nint32 b2World::GetProxyCount() const")
    elseif(relative STREQUAL "Source/Common/b2Settings.cpp")
        replace_box2d_native(contents "b2Version b2_version = {2, 0, 1};"
            "// OpenGangstar native adaptation: preserve malloc's alignment after the size header.\nstatic const int32 allocationHeaderSize = static_cast<int32>(alignof(std::max_align_t));\nstatic_assert(allocationHeaderSize >= sizeof(int32), \"Allocation header must hold its size.\");\n\nb2Version b2_version = {2, 0, 1};")
        replace_box2d_native(contents "size += 4;" "size += allocationHeaderSize;")
        replace_box2d_native(contents "return bytes + 4;" "return bytes + allocationHeaderSize;")
        replace_box2d_native(contents "bytes -= 4;" "bytes -= allocationHeaderSize;")
    elseif(relative STREQUAL "Source/Common/b2StackAllocator.h")
        replace_box2d_native(contents "char m_data[b2_stackSize];"
            "// OpenGangstar native adaptation: the local arena must align native objects.\n\talignas(std::max_align_t) char m_data[b2_stackSize];")
    elseif(relative STREQUAL "Source/Common/b2StackAllocator.cpp")
        replace_box2d_native(contents "void* b2StackAllocator::Allocate(int32 size)\n{"
            "void* b2StackAllocator::Allocate(int32 size)\n{\n\t// OpenGangstar native adaptation: preserve alignment across differently sized blocks.\n\tconst int32 alignment = static_cast<int32>(alignof(std::max_align_t));\n\tb2Assert(size >= 0 && size <= INT_MAX - (alignment - 1));\n\tsize = (size + alignment - 1) / alignment * alignment;")
    elseif(relative STREQUAL "Source/Common/b2BlockAllocator.cpp")
        replace_box2d_native(contents "int32 gap = (int32)((int8*)&m_chunks->blocks - (int8*)m_chunks);"
            "// OpenGangstar native adaptation: this is a member offset, not a 32-bit address.\n\tconst std::size_t gap = offsetof(b2Chunk, blocks);")
    endif()
    file(CONFIGURE OUTPUT "${box2d_native}/${relative}" CONTENT "${contents}" @ONLY)
endforeach()
configure_file("${box2d_original}/License.txt" "${box2d_native}/License.txt" COPYONLY)

file(GLOB_RECURSE box2d_sources "${box2d_native}/Source/*.cpp")
list(LENGTH box2d_sources box2d_source_count)
if(NOT box2d_source_count EQUAL 31)
    message(FATAL_ERROR "Expected all 31 historical Box2D engine translation units.")
endif()
add_library(opengangstar_box2d STATIC ${box2d_sources})
target_compile_features(opengangstar_box2d PUBLIC cxx_std_17)
target_include_directories(opengangstar_box2d PUBLIC "${box2d_native}/Include" "${box2d_native}/Source")
if(MSVC)
    target_compile_options(opengangstar_box2d PRIVATE /fp:strict /we4302 /we4311 /we4312)
endif()
