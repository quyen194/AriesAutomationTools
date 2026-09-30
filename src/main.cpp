#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include "ui/app_ui.hpp"
#include "config/config_manager.hpp"
#include "core/logger.hpp"
#include "single_instance.hpp"
#include "icon_data.hpp"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdio>
#include <string>

static constexpr const char* kInstanceName = "AriesAutomationTools_Instance";

int main(int argc, char** argv) {
    // Single-instance enforcement (skip if --allow-multiple is passed)
    bool skipSingleInstance = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--allow-multiple") { skipSingleInstance = true; break; }

    if (!skipSingleInstance) {
        // Quick pre-read of config to respect the in-app toggle
        bool wantSingle = true;
        try {
            auto cfg = ConfigManager::Load(ConfigManager::DefaultPath());
            wantSingle = cfg.single_instance;
        } catch (...) {}

        if (wantSingle && !TryAcquireSingleInstance(kInstanceName)) {
            SDL_Init(SDL_INIT_VIDEO);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,
                "Aries Automation Tools",
                "Another instance is already running.",
                nullptr);
            SDL_Quit();
            return 1;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    // OpenGL 3 context (imgui_impl_opengl3). SDL_Renderer is not used: its ImGui
    // backend has no multi-viewport support, which we need so tooltips/pick
    // overlays can render outside the main window.
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_WindowFlags wflags = (SDL_WindowFlags)(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow(
        "Aries Automation Tools",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1100, 680, wflags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Set window icon from embedded 32x32 RGBA pixel data
    {
        SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(
            const_cast<uint8_t*>(kIconPixels),
            32, 32, 32, 32 * 4,
            0x000000FF,   // R mask
            0x0000FF00,   // G mask
            0x00FF0000,   // B mask
            0xFF000000);  // A mask
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_FreeSurface(icon);
        }
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        fprintf(stderr, "SDL_GL_CreateContext error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Multi-viewport: ImGui windows/tooltips may leave the main window and become
    // their own OS windows. Note: ImGui coordinates are then absolute desktop coords.
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // Keep imgui.ini with the rest of the app data (not the working directory)
    static const std::string s_iniPath = ConfigManager::DataDir() + "/imgui.ini";
    io.IniFilename = s_iniPath.c_str();

    // Workflow run / error log (also shown in the Log panel)
    Logger::SetFile(ConfigManager::DataDir() + "/aries.log");

    // Style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 4.0f;
    style.FrameRounding    = 3.0f;
    style.ScrollbarRounding= 3.0f;
    style.GrabRounding     = 3.0f;
    // Platform windows (viewports) must be opaque
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init(nullptr); // default GLSL version for the platform

    AppUI app;
    app.Init(ConfigManager::DefaultPath(), window);

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);

            if (ev.type == SDL_QUIT) {
                if (!app.RequestQuit()) {
                    // Quit dialog is shown — don't exit yet
                }
            }
            if (ev.type == SDL_WINDOWEVENT) {
                if (ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                    ev.window.windowID == SDL_GetWindowID(window)) {
                    // X button: respect "close to tray" setting
                    if (!app.RequestQuit()) {
                        // Either showing dialog or minimizing to tray — don't quit
                    }
                } else if (ev.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    app.OnWindowMinimized();
                }
            }
        }

        if (app.ShouldQuit()) {
            running = false;
            break;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        app.Render();

        ImGui::Render();
        glViewport(0, 0,
                   (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                   (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y));
        glClearColor(30 / 255.f, 30 / 255.f, 30 / 255.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Render ImGui windows that live outside the main window
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            SDL_Window*   backupWindow  = SDL_GL_GetCurrentWindow();
            SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(backupWindow, backupContext);
        }

        SDL_GL_SwapWindow(window);
    }

    app.Shutdown(); // releases GL textures — GL context must still be alive

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    ReleaseSingleInstance();
    return 0;
}
