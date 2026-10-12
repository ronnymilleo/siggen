/**
 * @file    ui_smoke_test.cpp
 * @brief   Smoke tests that draw the generator windows in their dock layout, plus help topics and image export.
 */

#include "help_topics.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"
#include "window_manager.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <gtest/gtest.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "plot_figures.h"
#include "stb_image_write.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

namespace GUI {

namespace {

constexpr const char *WindowTitles[] = {"Signal Setup", "Signal Summary", "Signal Views"};

// Found by its tab names, so the test does not depend on the ID scope the tab bar is created in
ImGuiTabBar *FindSignalViews() {
    auto *ctx = ImGui::GetCurrentContext();
    for (int i = 0; i < ctx->TabBars.GetMapSize(); ++i) {
        if (auto *bar = ctx->TabBars.TryGetMapData(i)) {
            for (auto &tab : bar->Tabs) {
                if (std::string(ImGui::TabBarGetTabName(bar, &tab)) == "Waveform") {
                    return bar;
                }
            }
        }
    }
    return nullptr;
}

// A headless ImGui/ImPlot context with docking, as ImGuiLayer::Init() configures it, and no imgui.ini
void CreateContexts(ImVec2 display_size) {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DeltaTime = 1.f / 60;
    io.DisplaySize = display_size;
}

void BuildFontAtlas() {
    unsigned char *pixels;
    int width, height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
}

void DrawFrame(WindowManager &manager) {
    ImGui::NewFrame();
    manager.Render();
    ImGui::Render();
}

} // namespace

TEST(UI, InitialInvalidAndResizedFrames) {
    CreateContexts(ImVec2(1100, 900));
    BuildFontAtlas();
    auto &io = ImGui::GetIO();
    for (bool invalid : {false, true}) {
        Core::GenerationConfig config;
        if (invalid) {
            config.SymbolCount = -1;
        }
        WindowManager manager(config);
        for (auto size : {ImVec2(1100, 900), ImVec2(640, 480), ImVec2(1600, 1000)}) {
            io.DisplaySize = size;
            DrawFrame(manager);
            DrawFrame(manager);
            const auto *viewport = ImGui::GetMainViewport();
            // Every window is docked in one dockspace that fills the work area below the menu bar
            for (const char *title : WindowTitles) {
                const auto *window = ImGui::FindWindowByName(title);
                ASSERT_NE(window, nullptr) << title;
                ASSERT_NE(window->DockNode, nullptr) << title;
                const auto *root = ImGui::DockNodeGetRootNode(window->DockNode);
                EXPECT_EQ(root->Pos.x, viewport->WorkPos.x) << title;
                EXPECT_EQ(root->Pos.y, viewport->WorkPos.y) << title;
                EXPECT_EQ(root->Size.x, viewport->WorkSize.x) << title;
                EXPECT_EQ(root->Size.y, viewport->WorkSize.y) << title;
                EXPECT_FALSE(window->HasCloseButton) << title;
            }
            // Default layout: setup on the left, summary above views on the right
            const auto *setup = ImGui::FindWindowByName("Signal Setup");
            const auto *summary = ImGui::FindWindowByName("Signal Summary");
            const auto *views = ImGui::FindWindowByName("Signal Views");
            EXPECT_LT(setup->Pos.x, summary->Pos.x);
            EXPECT_EQ(summary->Pos.x, views->Pos.x);
            EXPECT_LT(summary->Pos.y, views->Pos.y);
            ASSERT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
        }
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

struct GUITestAccess {
    static void Start(WindowManager &manager) { manager.m_Session.StartGeneration(); }
    static bool Busy(WindowManager &manager) { return manager.m_Session.IsGenerating(); }
    static void Edit(WindowManager &manager) { manager.m_Session.GetConfig().Seed++; }
    static void Matched(WindowManager &manager, bool enabled) {
        manager.m_ViewsWindow.m_ConstellationView = enabled ? 1 : 0;
    }
    static const auto &Result(WindowManager &manager) { return manager.m_Session.GetResult(); }
    static const auto &Error(WindowManager &manager) { return manager.m_Session.GetError(); }
    // What the Export image dialog does once a destination is chosen
    static std::string ExportImage(WindowManager &manager, const std::string &path, int format,
                                   bool overwrite = false) {
        auto &views = manager.m_ViewsWindow;
        views.m_ImageFigure = Core::WaveformFigure(manager.m_Session.GetPlots());
        std::snprintf(views.m_ImagePath, sizeof views.m_ImagePath, "%s", path.c_str());
        views.m_ImageFormat = format;
        views.m_ImageWidth = 800;
        views.m_ImageHeight = 450;
        views.ExportImage(overwrite);
        return views.m_ImageStatus;
    }
    static void Invalid(WindowManager &manager) { manager.m_Session.GetConfig().SymbolCount = 0; }
};

TEST(UI, GeneratedViewsAndPendingClosure) {
    const char *capture = std::getenv("SIGGEN_CAPTURE_DIR");
    constexpr int CaptureSize = 1100;
    SDL_Window *native = nullptr;
    SDL_GPUDevice *device = nullptr;
    SDL_GPUTexture *canvas = nullptr;
    SDL_GPUTransferBuffer *readback = nullptr;
    if (capture) {
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
        native = SDL_CreateWindow("IQ validation", CaptureSize, CaptureSize, SDL_WINDOW_HIDDEN);
        ASSERT_NE(native, nullptr);
        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL |
                                         SDL_GPU_SHADERFORMAT_METALLIB,
                                     false, nullptr);
        ASSERT_NE(device, nullptr);
        // An offscreen target with a fixed channel order, so the capture needs no format conversion
        SDL_GPUTextureCreateInfo texture_info{};
        texture_info.type = SDL_GPU_TEXTURETYPE_2D;
        texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        texture_info.width = CaptureSize;
        texture_info.height = CaptureSize;
        texture_info.layer_count_or_depth = 1;
        texture_info.num_levels = 1;
        canvas = SDL_CreateGPUTexture(device, &texture_info);
        ASSERT_NE(canvas, nullptr);
        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transfer_info.size = CaptureSize * CaptureSize * 4;
        readback = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        ASSERT_NE(readback, nullptr);
        std::filesystem::create_directories(capture);
    }
    CreateContexts(ImVec2(CaptureSize, CaptureSize));
    if (native) {
        ImGui_ImplSDL3_InitForSDLGPU(native);
        ImGui_ImplSDLGPU3_InitInfo init_info{};
        init_info.Device = device;
        init_info.ColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ImGui_ImplSDLGPU3_Init(&init_info);
    } else {
        BuildFontAtlas();
    }
    auto frame = [&](WindowManager &manager) {
        if (native) {
            SDL_PumpEvents();
            ImGui_ImplSDLGPU3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            manager.Render();
            ImGui::Render();
            SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
            ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), command_buffer);
            SDL_GPUColorTargetInfo target{};
            target.texture = canvas;
            target.clear_color = SDL_FColor{0.1f, 0.1f, 0.1f, 1.0f};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command_buffer, &target, 1, nullptr);
            ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), command_buffer, pass);
            SDL_EndGPURenderPass(pass);
            SDL_SubmitGPUCommandBuffer(command_buffer);
        } else {
            DrawFrame(manager);
        }
    };
    auto capture_pixels = [&]() {
        std::vector<unsigned char> pixels(CaptureSize * CaptureSize * 4);
        SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
        SDL_GPUTextureRegion source{};
        source.texture = canvas;
        source.w = CaptureSize;
        source.h = CaptureSize;
        source.d = 1;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = readback;
        destination.offset = 0;
        SDL_DownloadFromGPUTexture(copy_pass, &source, &destination);
        SDL_EndGPUCopyPass(copy_pass);
        SDL_SubmitGPUCommandBuffer(command_buffer);
        SDL_WaitForGPUIdle(device);
        if (void *mapped = SDL_MapGPUTransferBuffer(device, readback, false); mapped != nullptr) {
            std::memcpy(pixels.data(), mapped, pixels.size());
        }
        SDL_UnmapGPUTransferBuffer(device, readback);
        return pixels;
    };
    for (auto modulation :
         {Core::Modulation::BPSK, Core::Modulation::QPSK, Core::Modulation::PSK8, Core::Modulation::QAM16,
          Core::Modulation::QAM64, Core::Modulation::QAM256, Core::Modulation::OOK, Core::Modulation::PAM4,
          Core::Modulation::DBPSK, Core::Modulation::DQPSK, Core::Modulation::QAM32, Core::Modulation::OQPSK,
          Core::Modulation::PI4DQPSK, Core::Modulation::DPSK8, Core::Modulation::ASK4, Core::Modulation::FSK2,
          Core::Modulation::FSK4, Core::Modulation::MSK}) {
        Core::GenerationConfig c;
        c.Modulation = modulation;
        const bool fsk = Core::WaveformFamily(modulation) == Core::Family::Fsk;
        if (fsk) {
            c.Awgn.Enabled = true; // Noise makes the frequency estimate visibly non-ideal.
            c.Awgn.SnrDb = 20;
        }
        WindowManager window(c);
        GUITestAccess::Start(window);
        GUITestAccess::Edit(window); // Edit while work is pending.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (GUITestAccess::Busy(window) && std::chrono::steady_clock::now() < deadline) {
            frame(window);
            std::this_thread::yield();
        }
        ASSERT_FALSE(GUITestAccess::Busy(window));
        ASSERT_TRUE(GUITestAccess::Result(window));
        EXPECT_EQ(GUITestAccess::Result(window)->Config.Seed, c.Seed);
        EXPECT_TRUE(GUITestAccess::Error(window).empty());
        if (fsk) {
            frame(window);
            frame(window);
            auto *bar = FindSignalViews();
            ASSERT_NE(bar, nullptr);
            bool saw_frequency = false;
            for (auto &tab : bar->Tabs) {
                const std::string name = ImGui::TabBarGetTabName(bar, &tab);
                EXPECT_NE(name, "Constellation"); // FSK has no symbol constellation or matched-filter eye.
                EXPECT_NE(name, "Eye");
                EXPECT_NE(name, "Pipeline"); // No symbol/filter pipeline for FSK.
                saw_frequency |= name == "Frequency";
            }
            EXPECT_TRUE(saw_frequency);
        } else {
            frame(window);
            frame(window);
            auto *bar = FindSignalViews();
            ASSERT_NE(bar, nullptr);
            bool saw_pipeline = false;
            for (auto &tab : bar->Tabs) {
                saw_pipeline |= std::string(ImGui::TabBarGetTabName(bar, &tab)) == "Pipeline";
            }
            EXPECT_TRUE(saw_pipeline);
        }
        const std::vector<const char *> views =
            fsk ? std::vector<const char *>{"Waveform", "Frequency", "Spectrum"}
                : std::vector<const char *>{"Waveform", "Constellation", "Matched", "Eye", "Pipeline", "Spectrum"};
        for (const char *view : views) {
            frame(window);
            auto *gui_window = ImGui::FindWindowByName("Signal Views");
            ASSERT_NE(gui_window, nullptr);
            auto *bar = FindSignalViews();
            ASSERT_NE(bar, nullptr);
            const bool matched = std::string(view) == "Matched";
            GUITestAccess::Matched(window, matched);
            for (auto &tab : bar->Tabs) {
                if (std::string(ImGui::TabBarGetTabName(bar, &tab)) == (matched ? "Constellation" : view)) {
                    bar->NextSelectedTabId = tab.ID;
                }
            }
            frame(window);
            frame(window);
            EXPECT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
            if (std::string(view) == "Constellation" || matched) {
                bool checked = false;
                for (auto &plot : ImPlot::GetCurrentContext()->Plots.Buf) {
                    if (plot.Flags & ImPlotFlags_Equal) {
                        EXPECT_NEAR(plot.XAxis(0).GetAspect(), plot.YAxis(0).GetAspect(), 1e-12);
                        const bool real_axis =
                            modulation == Core::Modulation::BPSK || modulation == Core::Modulation::DBPSK ||
                            modulation == Core::Modulation::OOK || modulation == Core::Modulation::PAM4 ||
                            modulation == Core::Modulation::ASK4;
                        if (!real_axis) {
                            const double outer =
                                modulation == Core::Modulation::QPSK || modulation == Core::Modulation::DQPSK ||
                                        modulation == Core::Modulation::OQPSK
                                    ? 1 / std::sqrt(2.)
                                : modulation == Core::Modulation::PSK8 || modulation == Core::Modulation::PI4DQPSK ||
                                        modulation == Core::Modulation::DPSK8
                                    ? 1.
                                : modulation == Core::Modulation::QAM16  ? 3 / std::sqrt(10.)
                                : modulation == Core::Modulation::QAM256 ? 15 / std::sqrt(170.)
                                : modulation == Core::Modulation::QAM32  ? 5 / std::sqrt(20.)
                                                                         : 7 / std::sqrt(42.);
                            EXPECT_GT(plot.YAxis(0).Range.Max, outer);
                            EXPECT_LT(plot.YAxis(0).Range.Min, -outer);
                        }
                        checked = true;
                    }
                }
                EXPECT_TRUE(checked);
            }
            if (capture) {
                const auto pixels = capture_pixels();
                // pi/4-DQPSK's canonical name carries a slash, which must not become a path separator
                auto name = std::string(Core::ModulationName(modulation));
                std::replace(name.begin(), name.end(), '/', '-');
                const auto path = std::filesystem::path(capture) / (name + "-" + view + ".png");
                EXPECT_TRUE(
                    stbi_write_png(path.string().c_str(), CaptureSize, CaptureSize, 4, pixels.data(), CaptureSize * 4));
            }
        }
        auto previous = GUITestAccess::Result(window);
        GUITestAccess::Invalid(window);
        GUITestAccess::Start(window);
        const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (GUITestAccess::Busy(window) && std::chrono::steady_clock::now() < failure_deadline) {
            frame(window);
        }
        EXPECT_EQ(GUITestAccess::Result(window), previous);
        EXPECT_FALSE(GUITestAccess::Error(window).empty());
    }
    {
        Core::GenerationConfig c;
        c.SymbolCount = 65536;
        WindowManager closing(c);
        GUITestAccess::Start(closing);
        // Destruction with an outstanding job must complete before destroying the UI context.
    }
    if (native) {
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (native) {
        SDL_ReleaseGPUTransferBuffer(device, readback);
        SDL_ReleaseGPUTexture(device, canvas);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(native);
        SDL_Quit();
    }
}

TEST(UI, NoiseSourceRendersWithoutConstellation) {
    CreateContexts(ImVec2(1100, 900));
    BuildFontAtlas();
    Core::GenerationConfig c;
    c.Modulation = Core::Modulation::WGN;
    WindowManager window(c);
    auto frame = [&] { DrawFrame(window); };
    GUITestAccess::Start(window);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (GUITestAccess::Busy(window) && std::chrono::steady_clock::now() < deadline) {
        frame();
        std::this_thread::yield();
    }
    ASSERT_FALSE(GUITestAccess::Busy(window));
    ASSERT_TRUE(GUITestAccess::Result(window));
    EXPECT_TRUE(GUITestAccess::Error(window).empty());
    EXPECT_EQ(GUITestAccess::Result(window)->Family, Core::Family::Noise);
    for (int pass = 0; pass < 3; ++pass) {
        frame();
        EXPECT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
        auto *gui_window = ImGui::FindWindowByName("Signal Views");
        ASSERT_NE(gui_window, nullptr);
        auto *bar = FindSignalViews();
        ASSERT_NE(bar, nullptr);
        bool saw_waveform = false, saw_spectrum = false;
        for (auto &tab : bar->Tabs) {
            const std::string name = ImGui::TabBarGetTabName(bar, &tab);
            EXPECT_NE(name, "Constellation"); // Noise sources expose no symbol constellation.
            EXPECT_NE(name, "Pipeline");
            saw_waveform |= name == "Waveform";
            saw_spectrum |= name == "Spectrum";
        }
        EXPECT_TRUE(saw_waveform);
        EXPECT_TRUE(saw_spectrum);
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

TEST(UI, EveryHelpTopicHasATitleAndExplanation) {
    for (const auto &topic : AllHelpTopics) {
        EXPECT_FALSE(topic.Id.empty());
        EXPECT_FALSE(topic.Title.empty()) << topic.Id;
        EXPECT_GT(topic.Body.size(), 100u) << topic.Id;
    }
    // The topics the controls link to: roll-off, span, SPS and the SNR / Es/N0 / Eb/N0 relation.
    EXPECT_NE(SnrHelp.Body.find("Es/N0"), std::string_view::npos);
    EXPECT_NE(SnrHelp.Body.find("Eb/N0"), std::string_view::npos);
}

TEST(UI, ExportImageWritesPngAndSvgAndProtectsExistingFiles) {
    CreateContexts(ImVec2(1100, 800));
    BuildFontAtlas();
    WindowManager window;
    GUITestAccess::Start(window);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (GUITestAccess::Busy(window) && std::chrono::steady_clock::now() < deadline) {
        DrawFrame(window);
        std::this_thread::yield();
    }
    ASSERT_TRUE(GUITestAccess::Result(window));
    const auto dir = std::filesystem::temp_directory_path() / "siggen-ui-image-export";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto png = (dir / "wave.png").string(), svg = (dir / "wave.svg").string();
    EXPECT_EQ(GUITestAccess::ExportImage(window, png, 0), "Saved " + png);
    EXPECT_EQ(GUITestAccess::ExportImage(window, svg, 1), "Saved " + svg);
    EXPECT_GT(std::filesystem::file_size(png), 1000u);
    EXPECT_GT(std::filesystem::file_size(svg), 1000u);
    EXPECT_NE(GUITestAccess::ExportImage(window, png, 0).find("Export failed"), std::string::npos);
    EXPECT_EQ(GUITestAccess::ExportImage(window, png, 0, true), "Saved " + png);
    std::filesystem::remove_all(dir);
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

} // namespace GUI
