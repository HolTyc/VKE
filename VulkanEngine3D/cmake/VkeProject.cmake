# vke_add_project(<name>) — declares a game/app built on the vke engine.
#
# Expected layout of a project directory (projects/<name>/):
#   CMakeLists.txt        calls vke_add_project(<name>) (plus any extra deps)
#   src/*.cpp             all sources, globbed recursively
#   shaders/*.{vert,frag} optional — compiled to build/assets/shaders/<name>/*.spv,
#                         so register them as "shaders/<name>/foo.vert.spv"
#   assets/               optional — copied verbatim to build/assets/<name>/,
#                         so load models as "<name>/models/foo.obj"
#
# The root CMakeLists auto-adds every projects/*/CMakeLists.txt it finds.
function(vke_add_project NAME)
    file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
    add_executable(${NAME} ${PROJECT_SOURCES})
    target_link_libraries(${NAME} PRIVATE vke)
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # All game binaries land in the build root: ./build/<name>
    set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

    # ---- per-project shaders (GLSL -> SPIR-V, namespaced by project name)
    file(GLOB PROJECT_SHADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag)
    if(PROJECT_SHADERS)
        set(PROJECT_SPIRV)
        foreach(SHADER ${PROJECT_SHADERS})
            get_filename_component(SHADER_NAME ${SHADER} NAME)
            set(SPV ${CMAKE_BINARY_DIR}/assets/shaders/${NAME}/${SHADER_NAME}.spv)
            add_custom_command(
                OUTPUT ${SPV}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets/shaders/${NAME}
                COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPV}
                DEPENDS ${SHADER}
                COMMENT "Compiling shader ${NAME}/${SHADER_NAME}")
            list(APPEND PROJECT_SPIRV ${SPV})
        endforeach()
        add_custom_target(${NAME}_shaders ALL DEPENDS ${PROJECT_SPIRV})
        add_dependencies(${NAME} ${NAME}_shaders)
    endif()

    # ---- per-project plain assets (models, data files, ...)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
        add_custom_target(${NAME}_assets ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets/${NAME}
            COMMENT "Copying ${NAME} assets")
        add_dependencies(${NAME} ${NAME}_assets)
    endif()
endfunction()
