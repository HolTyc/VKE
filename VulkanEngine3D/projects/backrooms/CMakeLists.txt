vke_add_project(backrooms)

# miniaudio (single header) for the synthesized ambience. Download only —
# we compile the implementation ourselves in src/Audio.cpp.
include(FetchContent)
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

target_include_directories(backrooms PRIVATE ${miniaudio_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(backrooms PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
