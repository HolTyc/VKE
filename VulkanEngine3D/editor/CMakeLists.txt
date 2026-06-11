# vke-editor: the standalone engine editor. Added explicitly (not via the
# projects/ auto-discovery) because hot-reloaded game scripts dlopen against
# this executable: every engine symbol must end up in its dynamic symbol table,
# hence whole-archive linking + ENABLE_EXPORTS.

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(vke-editor ${EDITOR_SOURCES})
set_target_properties(vke-editor PROPERTIES
    ENABLE_EXPORTS ON  # --export-dynamic: scripts.so resolves vke symbols from the exe
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

target_link_libraries(vke-editor PRIVATE
    -Wl,--whole-archive vke -Wl,--no-whole-archive
    nlohmann_json::nlohmann_json)

target_compile_definitions(vke-editor PRIVATE
    VKE_EDITOR_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/templates")
