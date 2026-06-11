#include "EditorApp.hpp"

#include <vke/ScriptHost.hpp>

#include <cstdio>
#include <cstring>
#include <string>

// Headless smoke test for the script pipeline: compile + dlopen a project's
// scripts and dump the registry, without touching Vulkan or opening a window.
static int compileTest(const char* projectDir) {
    vke::ScriptHost host;
    host.setProjectDir(projectDir);

    std::string log;
    if (!host.compile(log)) {
        std::fprintf(stderr, "compile failed:\n%s\n", log.c_str());
        return 1;
    }
    if (!log.empty()) std::fprintf(stderr, "%s", log.c_str());

    if (!host.load(log)) {
        std::fprintf(stderr, "load failed: %s\n", log.c_str());
        return 1;
    }

    static const char* typeNames[] = {"float", "int", "bool", "vec3", "string"};
    for (const vke::ScriptClassDesc* desc : host.classes()) {
        std::printf("%s:", desc->name);
        for (size_t i = 0; i < desc->propCount; ++i)
            std::printf(" %s(%s)", desc->props[i].name,
                        typeNames[static_cast<int>(desc->props[i].type)]);
        std::printf("\n");
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--compile-test") == 0)
        return compileTest(argv[2]);

    vke::AppConfig cfg;
    cfg.title = "VKE Editor";
    cfg.width = 1600;
    cfg.height = 900;
    cfg.editor = true;

    EditorApp app(cfg);
    if (argc >= 2) app.setStartupProject(argv[1]);
    app.run();
    return 0;
}
