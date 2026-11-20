#define NOMINMAX
#include <windows.h>
#define GLFW_INCLUDE_NONE
#include "Camera.h"
#include "FileDialog.h"
#include "RtxView.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "viewer_imgui_theme.h"
#include "viewer_imgui_widgets.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <ecm_ovstage/EcmOverlay.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace
{
struct Window
{
    GLFWwindow *handle = nullptr;
    GLuint texture = 0;
    bool imgui = false;
    ~Window()
    {
        if (texture)
            glDeleteTextures(1, &texture);
        if (imgui)
            ImGui_ImplGLFW_Shutdown();
        if (handle)
            glfwDestroyWindow(handle);
        glfwTerminate();
    }
};

struct Input
{
    std::string pending;
    bool open = false, frame = false;
    bool loading = false;
    int dragButton = -1;
};
Input &input(GLFWwindow *window)
{
    return *static_cast<Input *>(glfwGetWindowUserPointer(window));
}

void keyCallback(GLFWwindow *window, int key, int scan, int action, int mods)
{
    ImGui_ImplGLFW_KeyCallback(window, key, scan, action, mods);
    if (action != GLFW_PRESS || input(window).loading)
        return;
    if (key == GLFW_KEY_O && (mods & GLFW_MOD_CONTROL))
        input(window).open = true;
    if (!ImGui::GetIO().WantCaptureKeyboard && key == GLFW_KEY_HOME)
        input(window).frame = true;
}
void dropCallback(GLFWwindow *window, int count, const char **paths)
{
    if (count > 0 && !input(window).loading)
        input(window).pending = paths[0];
}

const char *loadStageLabel(viewer::RtxView::LoadStage stage)
{
    using Stage = viewer::RtxView::LoadStage;
    switch (stage)
    {
    case Stage::Initialize:
        return "Initializing RTX and stage runtime";
    case Stage::ReadUsd:
        return "Reading USD and referenced assets";
    case Stage::ComputeHierarchy:
        return "Computing scene hierarchy";
    case Stage::ReadScene:
        return "Reading scene bounds and cameras";
    case Stage::SetupViewer:
        return "Setting up viewer camera and lighting";
    case Stage::PrepareRenderer:
        return "Preparing geometry, materials and textures";
    case Stage::FirstFrame:
        return "Preparing the first RTX image";
    case Stage::Complete:
        return "Ready";
    }
    return "Loading";
}

// Only the worker touches RtxView until get() joins it. GLFW, ImGui and OpenGL
// stay on the main thread, including during blocking SDK initialization calls.
void withLoadingProgress(
    GLFWwindow *window, const std::string &path,
    const std::function<void(const viewer::RtxView::LoadProgress &, const viewer::RtxView::RenderProgress &)> &work)
{
    using Stage = viewer::RtxView::LoadStage;
    std::atomic<Stage> stage{Stage::Initialize};
    std::mutex detailMutex;
    std::string operationDetail;
    double operationFraction = -1;
    const auto started = std::chrono::steady_clock::now();
    const auto filename = fs::u8path(path).filename().u8string();
    std::error_code ec;
    const auto bytes = fs::file_size(fs::u8path(path), ec);
    auto task = std::async(std::launch::async, [&] {
        auto previous = Stage::Initialize;
        auto phaseStarted = started;
        const auto report = [&](Stage value) {
            const auto now = std::chrono::steady_clock::now();
            std::printf("Model loading: %s finished in %.3f s (total %.3f s)\n", loadStageLabel(previous),
                        std::chrono::duration<double>(now - phaseStarted).count(),
                        std::chrono::duration<double>(now - started).count());
            stage.store(value);
            std::printf("Model loading: %s\n", loadStageLabel(value));
            std::fflush(stdout);
            previous = value;
            phaseStarted = now;
        };
        work(report, [&](double fraction, const std::string &detail) {
            std::lock_guard<std::mutex> lock(detailMutex);
            operationDetail = detail;
            operationFraction = fraction;
        });
        report(Stage::Complete);
    });
    do
    {
        glfwPollEvents();
        int fw = 0, fh = 0, ww = 0, wh = 0;
        glfwGetFramebufferSize(window, &fw, &fh);
        glfwGetWindowSize(window, &ww, &wh);
        if (fw <= 0 || fh <= 0 || ww <= 0 || wh <= 0)
            continue;
        ImGui_ImplGLFW_NewFrame();
        const auto phase = stage.load();
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        ImGui::SetNextWindowPos(ImVec2(ww * 0.5f, wh * 0.5f), 0, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(float(ww) - 32, viewer::imgui_theme::ui(540)), 0));
        ImGui::Begin("Loading model", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextWrapped("%s", filename.c_str());
        if (!ec)
            ImGui::Text("%.1f MB", double(bytes) / (1024 * 1024));
        ImGui::Spacing();
        ImGui::TextWrapped("%s", loadStageLabel(phase));
        char label[64];
        std::snprintf(label, sizeof(label), "%d / %d stages complete", int(phase), int(Stage::Complete));
        ImGui::ProgressBar(float(phase) / float(Stage::Complete), ImVec2(-1, 0), label);
        if (phase == Stage::FirstFrame)
        {
            std::string detail;
            double fraction;
            {
                std::lock_guard<std::mutex> lock(detailMutex);
                detail = operationDetail;
                fraction = operationFraction;
            }
            ImGui::TextWrapped("USD imported. Waiting for RTX to prepare textures, materials and the first image.");
            if (!detail.empty())
            {
                ImGui::TextWrapped("%s", detail.c_str());
                if (fraction >= 0)
                    ImGui::ProgressBar(float(std::clamp(fraction, 0.0, 1.0)), ImVec2(-1, 0));
            }
        }
        const char *dots[] = {"", ".", "..", "..."};
        ImGui::Text("Elapsed: %.0f s%s", elapsed, dots[int(std::fmod(elapsed * 2, 4))]);
        ImGui::TextWrapped("Progress tracks loading stages. Large models and first-use shader compilation "
                           "can take several minutes.");
        if (glfwWindowShouldClose(window))
            ImGui::TextWrapped("Closing when the current SDK operation finishes...");
        ImGui::End();
        glViewport(0, 0, fw, fh);
        glClearColor(0.075f, 0.075f, 0.085f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplGLFW_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    } while (task.wait_for(std::chrono::milliseconds(16)) != std::future_status::ready);
    // Propagate SDK errors only after the worker has stopped using the renderer.
    task.get();
}

void drawImage(GLuint texture, const viewer::RtxView &rtx)
{
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rtx.width(), rtx.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rtx.pixels().data());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1);
    glVertex2f(-1, -1);
    glTexCoord2f(1, 1);
    glVertex2f(1, -1);
    glTexCoord2f(1, 0);
    glVertex2f(1, 1);
    glTexCoord2f(0, 0);
    glVertex2f(-1, 1);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// Runs `work` on a worker thread while the main thread keeps painting a small status window.
// Only the worker touches the renderer until it finishes; its exception is rethrown here.
using Status = std::function<void(const std::string &)>;
void withBusyDialog(GLFWwindow *window, const char *title, const std::function<void(const Status &)> &work)
{
    std::mutex mutex;
    std::string status = "Starting";
    const auto started = std::chrono::steady_clock::now();
    auto task = std::async(std::launch::async, [&] {
        work([&](const std::string &text) {
            std::printf("%s\n", text.c_str());
            std::lock_guard<std::mutex> lock(mutex);
            status = text;
        });
    });
    do
    {
        glfwPollEvents();
        int fw = 0, fh = 0, ww = 0, wh = 0;
        glfwGetFramebufferSize(window, &fw, &fh);
        glfwGetWindowSize(window, &ww, &wh);
        if (fw <= 0 || fh <= 0 || ww <= 0 || wh <= 0)
            continue;
        ImGui_ImplGLFW_NewFrame();
        ImGui::SetNextWindowPos(ImVec2(ww * 0.5f, wh * 0.5f), 0, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(float(ww) - 32, viewer::imgui_theme::ui(420)), 0));
        ImGui::Begin(title, nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_AlwaysAutoResize);
        {
            std::lock_guard<std::mutex> lock(mutex);
            ImGui::TextWrapped("%s", status.c_str());
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        ImGui::Text("Elapsed: %.1f s", elapsed);
        ImGui::End();
        glViewport(0, 0, fw, fh);
        glClearColor(0.075f, 0.075f, 0.085f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplGLFW_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    } while (task.wait_for(std::chrono::milliseconds(16)) != std::future_status::ready);
    task.get();
}

// 32-bit top-down BMP of an RGBA image.
void writeBmp(const fs::path &path, const std::vector<uint8_t> &rgba, int width, int height)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Could not open " + path.u8string());
    const auto put = [&](uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            file.put(char((value >> (8 * i)) & 0xff));
    };
    const uint32_t imageSize = uint32_t(width) * height * 4;
    file.put('B');
    file.put('M');
    put(54 + imageSize, 4);
    put(0, 4);
    put(54, 4);
    put(40, 4);
    put(uint32_t(width), 4);
    put(uint32_t(-height), 4); // negative: rows are stored top to bottom
    put(1, 2);
    put(32, 2);
    put(0, 4);
    put(imageSize, 4);
    put(2835, 4);
    put(2835, 4);
    put(0, 4);
    put(0, 4);
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
    {
        file.put(char(rgba[i + 2]));
        file.put(char(rgba[i + 1]));
        file.put(char(rgba[i]));
        file.put(char(uint8_t(255)));
    }
    if (!file)
        throw std::runtime_error("Could not write " + path.u8string());
}

struct Options
{
    std::string scene;
    bool createEcm = false, topView = false;
    float lineWidth = 0; // meters; 0 keeps the default
    fs::path screenshot; // written after the scene (and ECM) are shown, then the viewer exits
};

int run(const Options &options)
{
    Window window;
    glfwSetErrorCallback([](int code, const char *message) { std::fprintf(stderr, "GLFW %d: %s\n", code, message); });
    if (!glfwInit())
        throw std::runtime_error("Could not initialize GLFW.");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    window.handle = glfwCreateWindow(1600, 1000, "crowdSim | USD Viewer", nullptr, nullptr);
    if (!window.handle)
        throw std::runtime_error("Could not create the viewer window.");
    glfwSetWindowSizeLimits(window.handle, 640, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window.handle);
    glfwSwapInterval(1);
    window.imgui = ImGui_ImplGLFW_Init(window.handle, false);
    if (!window.imgui)
        throw std::runtime_error("Could not initialize ImGui.");
    ImGui::GetIO().IniFilename = nullptr;
    viewer::imgui_theme::init_ui_scale();
    viewer::imgui_theme::apply_kit_dark_style();
    viewer::imgui_theme::load_ui_font(window.handle);
    glGenTextures(1, &window.texture);
    Input actions;
    glfwSetWindowUserPointer(window.handle, &actions);
    glfwSetKeyCallback(window.handle, keyCallback);
    glfwSetCharCallback(window.handle, ImGui_ImplGLFW_CharCallback);
    glfwSetMouseButtonCallback(window.handle, ImGui_ImplGLFW_MouseButtonCallback);
    glfwSetScrollCallback(window.handle, ImGui_ImplGLFW_ScrollCallback);
    glfwSetDropCallback(window.handle, dropCallback);

    actions.pending = options.scene;
    std::string error;
    viewer::RtxView rtx;
    viewer::Camera camera;
    camera.init();
    viewer::Vec3 orbitCenter;
    bool presentLoadedFrame = false;

    // Explicit corridor map.
    ecm::BuildSettings ecmSettings;
    std::optional<ecm::Map> ecmMap;
    ecm_ovstage::EcmDisplay ecmDisplay;
    if (options.lineWidth > 0)
        ecmDisplay.lineWidth = options.lineWidth;
    std::string ecmInfo;
    bool createEcm = options.createEcm, clearEcm = false, updateOverlay = false;
    int screenshotFrames = 0;
    while (!glfwWindowShouldClose(window.handle))
    {
        glfwPollEvents();
        int fw = 0, fh = 0, ww = 0, wh = 0;
        glfwGetFramebufferSize(window.handle, &fw, &fh);
        glfwGetWindowSize(window.handle, &ww, &wh);
        if (fw <= 0 || fh <= 0 || ww <= 0 || wh <= 0)
        {
            glfwWaitEventsTimeout(0.05);
            continue;
        }
        ImGui_ImplGLFW_NewFrame();
        auto &io = ImGui::GetIO();
        const float panel = std::max(240.0f, viewer::imgui_theme::ui(300));
        const int left = static_cast<int>(panel * fw / ww);
        const int rw = std::max(1, fw - left), rh = fh;
        const auto frameStage = [&] {
            orbitCenter = rtx.center();
            camera.frame(orbitCenter, rtx.radius(), rtx.zUp(), float(rw) / rh);
        };
        const auto topView = [&] {
            frameStage();
            const float distance = (camera.pos - orbitCenter).magnitude();
            camera.look_at(orbitCenter + camera.orbitUp * distance, orbitCenter);
        };
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(panel, float(wh)));
        ImGui::Begin("Scene", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        viewer::imgui_widgets::section_heading("USD VIEWER");
        if (viewer::imgui_widgets::button_full_width("Open..."))
            actions.open = true;
        ImGui::TextWrapped("Ctrl+O, or drop a USD file into this window.");
        if (rtx.loaded())
        {
            ImGui::Separator();
            viewer::imgui_widgets::section_heading("Scene");
            ImGui::TextWrapped("%s", fs::u8path(rtx.path()).filename().u8string().c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", rtx.path().c_str());
            if (viewer::imgui_widgets::button_full_width("Frame stage"))
                actions.frame = true;
            if (viewer::imgui_widgets::button_full_width("Top view"))
                topView();
            if (rtx.initialCamera() && viewer::imgui_widgets::button_full_width("Restore USD camera"))
            {
                frameStage();
                const float speed = camera.speed;
                camera = *rtx.initialCamera();
                camera.speed = speed;
                camera.orbitUp = rtx.zUp() ? viewer::Vec3{0, 0, 1} : viewer::Vec3{0, 1, 0};
            }

            ImGui::Separator();
            viewer::imgui_widgets::section_heading("Explicit corridor map");
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
            const auto parameter = [](const char *label, double &value, float step, int precision) {
                float v = float(value);
                if (!ImGui::InputFloat(label, &v, step, step * 5, precision))
                    return false;
                value = v;
                return true;
            };
            if (parameter("Actor radius (m)", ecmSettings.actorRadius, 0.05f, 2) && ecmMap)
            {
                // The map itself is radius independent; only the display of narrow parts changes.
                ecmMap->settings.actorRadius = ecmSettings.actorRadius;
                updateOverlay = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Corridors narrower than this are drawn red or hidden. Applies immediately.");
            parameter("Actor height (m)", ecmSettings.actorHeight, 0.1f, 2);
            parameter("Max slope (deg)", ecmSettings.maxSlopeDegrees, 1, 1);
            parameter("Max step (m)", ecmSettings.maxStepHeight, 0.05f, 2);
            if (ImGui::CollapsingHeader("Advanced"))
            {
                parameter("Cell size (m)", ecmSettings.cellSize, 0.01f, 3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Voxel size of the floor analysis. Smaller is more accurate and slower.");
            }
            ImGui::PopItemWidth();
            if (viewer::imgui_widgets::button_full_width("Create ECM"))
                createEcm = true;
            if (ecmMap)
            {
                ImGui::TextWrapped("%s", ecmInfo.c_str());
                updateOverlay |= ImGui::Checkbox("Medial axis", &ecmDisplay.medialAxis);
                updateOverlay |= ImGui::Checkbox("Color by clearance", &ecmDisplay.clearanceColors);
                updateOverlay |= ImGui::Checkbox("Show narrower than actor", &ecmDisplay.narrow);
                updateOverlay |= ImGui::Checkbox("Closest-point spokes", &ecmDisplay.spokes);
                updateOverlay |= ImGui::Checkbox("Free-space boundary", &ecmDisplay.boundary);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
                if (ImGui::InputFloat("Line width (m)", &ecmDisplay.lineWidth, 0.01f, 0.05f, 2,
                                      ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    ecmDisplay.lineWidth = std::clamp(ecmDisplay.lineWidth, 0.001f, 10.0f);
                    updateOverlay = true;
                }
                ImGui::PopItemWidth();
                if (ecmDisplay.clearanceColors)
                    ImGui::TextDisabled("Clearance: red < radius, yellow > green > cyan > blue (8x radius)");
                if (ecmDisplay.spokes)
                    ImGui::TextDisabled("Spokes: orange left, purple right");
                if (viewer::imgui_widgets::button_full_width("Clear ECM"))
                    clearEcm = true;
            }
        }
        const bool loadingShown = !actions.pending.empty();
        if (loadingShown)
            ImGui::TextWrapped("Loading stage... First use may take a few minutes while shaders compile.");
        if (!error.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.4f, 1));
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopStyleColor();
            if (ImGui::Button("Dismiss"))
                error.clear();
        }
        ImGui::Spacing();
        ImGui::Separator();
        viewer::imgui_widgets::section_heading("Navigation");
        ImGui::TextWrapped("Left drag: orbit\nRight drag: look\nMiddle drag: pan\nWheel: dolly / zoom\nW A S D: "
                           "move\nQ / E: down / up\nShift: move faster\nHome: frame stage");
        ImGui::End();
        if (actions.frame && rtx.loaded())
            frameStage();
        actions.frame = false;

        if (rtx.loaded())
        {
            if (actions.dragButton < 0 && !io.WantCaptureMouse && io.MousePos.x >= panel)
                for (int button = 0; button < 3; ++button)
                    if (ImGui::IsMouseClicked(button))
                        actions.dragButton = button;
            if (actions.dragButton >= 0)
            {
                const int button = actions.dragButton;
                if (!ImGui::IsMouseDown(button))
                    actions.dragButton = -1;
                else if (button == 0)
                    camera.orbit(io.MouseDelta.x, io.MouseDelta.y, orbitCenter);
                else if (button == 1)
                    camera.look(io.MouseDelta.x, io.MouseDelta.y);
                else
                {
                    const auto previous = camera.pos;
                    const float scale = camera.orthographic ? camera.orthographicHeight / rh
                                                            : (camera.pos - orbitCenter).magnitude() * 0.001f;
                    camera.pan(io.MouseDelta.x, io.MouseDelta.y, scale);
                    orbitCenter += camera.pos - previous;
                }
            }
            if (!io.WantCaptureMouse)
                camera.zoom(io.MouseWheel * 3);
            if (!io.WantCaptureKeyboard && glfwGetWindowAttrib(window.handle, GLFW_FOCUSED))
            {
                const auto down = [&](int key) { return glfwGetKey(window.handle, key) == GLFW_PRESS ? 1.0f : 0.0f; };
                const float speed = camera.speed * std::min(io.DeltaTime, 0.1f) * 15 * (io.KeyShift ? 4 : 1);
                const auto move = (camera.forward * (down(GLFW_KEY_W) - down(GLFW_KEY_S)) +
                                   camera.right * (down(GLFW_KEY_D) - down(GLFW_KEY_A)) +
                                   camera.orbitUp * (down(GLFW_KEY_E) - down(GLFW_KEY_Q))) *
                                  speed;
                camera.pos += move;
                orbitCenter += move;
            }
        }
        if (rtx.loaded() && actions.pending.empty())
        {
            // Present the image produced by the loading worker before asking
            // RTX for another frame, which can itself wait on deferred assets.
            if (presentLoadedFrame)
                presentLoadedFrame = false;
            else
                rtx.render(camera, rw, rh);
            // Let the path tracer converge for a moment before capturing.
            if (!options.screenshot.empty() && !createEcm && ++screenshotFrames == 60)
            {
                writeBmp(options.screenshot, rtx.pixels(), rtx.width(), rtx.height());
                std::printf("Screenshot written: %s\n", options.screenshot.u8string().c_str());
                glfwSetWindowShouldClose(window.handle, GLFW_TRUE);
            }
        }
        glViewport(0, 0, fw, fh);
        glClearColor(0.075f, 0.075f, 0.085f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(left, 0, rw, rh);
        if (!rtx.pixels().empty())
            drawImage(window.texture, rtx);
        glViewport(0, 0, fw, fh);
        ImGui::Render();
        ImGui_ImplGLFW_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window.handle);

        if (actions.open)
        {
            actions.open = false;
            try
            {
                const auto selected = selectUsdFile(window.handle);
                if (!selected.empty())
                    actions.pending = selected;
            }
            catch (const std::exception &e)
            {
                error = e.what();
            }
        }
        if (createEcm && rtx.loaded() && actions.pending.empty())
        {
            createEcm = false;
            actions.loading = true;
            actions.dragButton = -1;
            try
            {
                std::optional<ecm::Map> built;
                withBusyDialog(window.handle, "Creating ECM", [&](const Status &status) {
                    status("Extracting scene triangles...");
                    auto triangles = rtx.extractTriangles();
                    std::printf("Stage geometry: %zu meshes, %zu shapes, %zu instances, %zu hidden, %zu triangles, "
                                "%s-up, %g m/unit\n",
                                triangles.meshes, triangles.primitives, triangles.instances, triangles.hidden,
                                triangles.mesh.indices.size() / 3, triangles.zUp ? "Z" : "Y",
                                triangles.metersPerUnit);
                    auto settings = ecmSettings;
                    settings.up = triangles.zUp ? ecm::UpAxis::Z : ecm::UpAxis::Y;
                    settings.metersPerUnit = triangles.metersPerUnit;
                    status("Building the corridor map from " + std::to_string(triangles.mesh.indices.size() / 3) +
                           " triangles...");
                    built = ecm::build(triangles.mesh, settings);
                    status("Updating the overlay...");
                    rtx.setOverlay(ecm_ovstage::ecmOverlayLayer(*built, ecmDisplay, rtx.overlayRoot()));
                });
                ecmMap = std::move(built);
                const auto &s = ecmMap->stats;
                char text[512];
                std::snprintf(text, sizeof(text),
                              "%zu vertices, %zu edges\n%d x %d cells of %.3f m, %zu on the floor\n"
                              "%zu boundary loops, %zu segments\n"
                              "Time: voxels %.0f ms, floor %.0f ms, outline %.0f ms, Voronoi %.0f ms, graph %.0f ms",
                              ecmMap->vertices.size(), ecmMap->edges.size(), s.gridWidth, s.gridHeight, s.cellSize,
                              s.walkableCells, s.boundaryLoops, s.boundarySegments, s.rasterizeMs, s.floorMs,
                              s.contourMs, s.voronoiMs, s.graphMs);
                ecmInfo = text;
                std::printf("ECM: %s\n", ecmInfo.c_str());
                error.clear();
            }
            catch (const std::exception &e)
            {
                error = e.what();
                std::fprintf(stderr, "Create ECM: %s\n", error.c_str());
            }
            actions.loading = false;
            updateOverlay = false;
        }
        if ((clearEcm || updateOverlay) && rtx.loaded())
        {
            if (clearEcm)
                ecmMap.reset();
            clearEcm = updateOverlay = false;
            try
            {
                rtx.setOverlay(ecmMap ? ecm_ovstage::ecmOverlayLayer(*ecmMap, ecmDisplay, rtx.overlayRoot()) : "");
            }
            catch (const std::exception &e)
            {
                error = e.what();
            }
        }
        // Keep pumping and painting the window while the SDK loads the model.
        if (loadingShown && !actions.pending.empty())
        {
            const auto requested = fs::absolute(fs::u8path(actions.pending)).u8string();
            actions.pending.clear();
            glfwSetWindowTitle(window.handle, "crowdSim | Loading USD...");
            actions.loading = true;
            actions.dragButton = -1;
            try
            {
                withLoadingProgress(window.handle, requested,
                                    [&](const viewer::RtxView::LoadProgress &progress,
                                        const viewer::RtxView::RenderProgress &renderProgress) {
                                        rtx.initialize();
                                        rtx.load(requested, rw, rh, progress);
                                        frameStage();
                                        progress(viewer::RtxView::LoadStage::FirstFrame);
                                        rtx.render(camera, rw, rh, renderProgress);
                                    });
                presentLoadedFrame = true;
                // The overlay belonged to the previous stage.
                ecmMap.reset();
                if (options.topView)
                    topView();
                error.clear();
                std::printf("First image ready: %s\n", requested.c_str());
            }
            catch (const std::exception &e)
            {
                error = e.what();
                std::fprintf(stderr, "%s\n", error.c_str());
            }
            actions.loading = false;
            if (glfwWindowShouldClose(window.handle))
                break;
            const auto title =
                "crowdSim | " + (rtx.loaded() ? fs::u8path(rtx.path()).filename().u8string() : "USD Viewer");
            glfwSetWindowTitle(window.handle, title.c_str());
        }
    }
    return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try
    {
        Options options;
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring arg = argv[i];
            if (arg == L"--ecm")
                options.createEcm = true;
            else if (arg == L"--top")
                options.topView = true;
            else if (arg == L"--line-width" && i + 1 < argc)
                options.lineWidth = std::stof(argv[++i]);
            else if (arg == L"--screenshot" && i + 1 < argc)
                options.screenshot = fs::absolute(argv[++i]);
            else if (arg == L"--help" || arg == L"-h")
            {
                std::puts("Usage: crowdSim_viewer [scene.usd] [--ecm] [--top] [--screenshot image.bmp]\n"
                          "  --ecm         create the corridor map once the scene is loaded\n"
                          "  --top         look straight down at the scene\n"
                          "  --screenshot  save the rendered view after it settles, then exit");
                return 0;
            }
            else if (!arg.empty() && arg[0] != L'-')
                options.scene = fs::absolute(fs::path(arg)).u8string();
            else
                throw std::runtime_error("Unknown argument. Run with --help.");
        }
        return run(options);
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "crowdSim viewer: %s\n", e.what());
        return 1;
    }
}
