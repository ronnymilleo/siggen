/**
 * @file    ui_smoke_test.cpp
 * @brief   Smoke tests that draw the generator window, plus help topics and plot image export.
 */

#include "help_topics.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"
#include "signal_generator.h"
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "plot_figures.h"
#include "stb_image_write.h"
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace GUI {

namespace {

// The tab bar lives inside a layout table cell, whose ID scope differs from the window's; find it by its tab names
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

} // namespace

TEST(UI, InitialInvalidAndResizedFrames) {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime = 1.f / 60;
    unsigned char *pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    for (bool invalid : {false, true}) {
        Core::GenerationConfig c;
        if (invalid) {
            c.SymbolCount = -1;
        }
        SignalGenerator window(c);
        for (auto size : {ImVec2(1100, 900), ImVec2(640, 480), ImVec2(1600, 1000)}) {
            io.DisplaySize = size;
            ImGui::NewFrame();
            // Simulate stale floating-window geometry; the root layout overrides it.
            ImGui::SetNextWindowPos(ImVec2(2000, 2000));
            ImGui::SetNextWindowSize(ImVec2(900, 760));
            window.Render();
            ImGui::Render();
            ImGui::NewFrame();
            window.Render();
            ImGui::Render();
            const auto *resized = ImGui::FindWindowByName("Signal Generator");
            ASSERT_NE(resized, nullptr);
            const auto *viewport = ImGui::GetMainViewport();
            EXPECT_EQ(resized->Size.x, viewport->WorkSize.x);
            EXPECT_EQ(resized->Size.y, viewport->WorkSize.y);
            EXPECT_EQ(resized->Pos.x, viewport->WorkPos.x);
            EXPECT_EQ(resized->Pos.y, viewport->WorkPos.y);
            EXPECT_EQ(resized->ViewportId, viewport->ID);
            EXPECT_FALSE(resized->HasCloseButton);
            ASSERT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
        }
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

struct SignalGeneratorTestAccess {
    static void Start(SignalGenerator &w) { w.m_Job.Start(w.m_Config); }
    static bool Busy(SignalGenerator &w) { return w.m_Job.Busy(); }
    static void Edit(SignalGenerator &w) { w.m_Config.Seed++; }
    static void Matched(SignalGenerator &w, bool enabled) { w.m_ConstellationView = enabled ? 1 : 0; }
    static const auto &Result(SignalGenerator &w) { return w.m_Job.Result(); }
    static const auto &Error(SignalGenerator &w) { return w.m_Error; }
    // What the Export image dialog does once a destination is chosen.
    static std::string ExportImage(SignalGenerator &w, const std::string &path, int format, bool overwrite = false) {
        w.m_ImageFigure = Core::WaveformFigure(w.m_Plots);
        std::snprintf(w.m_ImagePath, sizeof w.m_ImagePath, "%s", path.c_str());
        w.m_ImageFormat = format;
        w.m_ImageWidth = 800;
        w.m_ImageHeight = 450;
        w.ExportImage(overwrite);
        return w.m_ImageStatus;
    }
    static void Invalid(SignalGenerator &w) { w.m_Config.SymbolCount = 0; }
};
TEST(UI, GeneratedViewsAndPendingClosure) {
    const char *capture = std::getenv("SIGGEN_CAPTURE_DIR");
    GLFWwindow *native = nullptr;
    if (capture) {
        ASSERT_TRUE(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        native = glfwCreateWindow(1100, 1100, "IQ validation", nullptr, nullptr);
        ASSERT_NE(native, nullptr);
        glfwMakeContextCurrent(native);
        std::filesystem::create_directories(capture);
    }
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime = 1.f / 60;
    io.DisplaySize = ImVec2(1100, 1100);
    if (native) {
        ImGui_ImplGlfw_InitForOpenGL(native, true);
        ImGui_ImplOpenGL3_Init("#version 330");
    } else {
        unsigned char *pixels;
        int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    }
    auto frame = [&](SignalGenerator &window) {
        if (native) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
        }
        ImGui::SetWindowSize("Signal Generator", io.DisplaySize);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        window.Render();
        ImGui::Render();
        if (native) {
            glViewport(0, 0, 1100, 1100);
            glClearColor(.1f, .1f, .1f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glFinish();
        }
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
        SignalGenerator window(c);
        SignalGeneratorTestAccess::Start(window);
        SignalGeneratorTestAccess::Edit(window); // Edit while work is pending.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (SignalGeneratorTestAccess::Busy(window) && std::chrono::steady_clock::now() < deadline) {
            frame(window);
            std::this_thread::yield();
        }
        ASSERT_FALSE(SignalGeneratorTestAccess::Busy(window));
        ASSERT_TRUE(SignalGeneratorTestAccess::Result(window));
        EXPECT_EQ(SignalGeneratorTestAccess::Result(window)->Config.Seed, c.Seed);
        EXPECT_TRUE(SignalGeneratorTestAccess::Error(window).empty());
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
            auto *gui_window = ImGui::FindWindowByName("Signal Generator");
            ASSERT_NE(gui_window, nullptr);
            auto *bar = FindSignalViews();
            ASSERT_NE(bar, nullptr);
            const bool matched = std::string(view) == "Matched";
            SignalGeneratorTestAccess::Matched(window, matched);
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
                std::vector<unsigned char> pixels(1100 * 1100 * 4);
                glReadPixels(0, 0, 1100, 1100, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                stbi_flip_vertically_on_write(true);
                const auto path = std::filesystem::path(capture) /
                                  (std::string(Core::ModulationName(modulation)) + "-" + view + ".png");
                EXPECT_TRUE(stbi_write_png(path.string().c_str(), 1100, 1100, 4, pixels.data(), 1100 * 4));
            }
        }
        auto previous = SignalGeneratorTestAccess::Result(window);
        SignalGeneratorTestAccess::Invalid(window);
        SignalGeneratorTestAccess::Start(window);
        const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (SignalGeneratorTestAccess::Busy(window) && std::chrono::steady_clock::now() < failure_deadline) {
            frame(window);
        }
        EXPECT_EQ(SignalGeneratorTestAccess::Result(window), previous);
        EXPECT_FALSE(SignalGeneratorTestAccess::Error(window).empty());
    }
    {
        Core::GenerationConfig c;
        c.SymbolCount = 65536;
        SignalGenerator closing(c);
        SignalGeneratorTestAccess::Start(closing);
        // Destruction with an outstanding job must complete before destroying the UI context.
    }
    if (native) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (native) {
        glfwDestroyWindow(native);
        glfwTerminate();
    }
}

TEST(UI, NoiseSourceRendersWithoutConstellation) {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime = 1.f / 60;
    io.DisplaySize = ImVec2(1100, 900);
    unsigned char *pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    Core::GenerationConfig c;
    c.Modulation = Core::Modulation::WGN;
    SignalGenerator window(c);
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        window.Render();
        ImGui::Render();
    };
    SignalGeneratorTestAccess::Start(window);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (SignalGeneratorTestAccess::Busy(window) && std::chrono::steady_clock::now() < deadline) {
        frame();
        std::this_thread::yield();
    }
    ASSERT_FALSE(SignalGeneratorTestAccess::Busy(window));
    ASSERT_TRUE(SignalGeneratorTestAccess::Result(window));
    EXPECT_TRUE(SignalGeneratorTestAccess::Error(window).empty());
    EXPECT_EQ(SignalGeneratorTestAccess::Result(window)->Family, Core::Family::Noise);
    for (int pass = 0; pass < 3; ++pass) {
        frame();
        EXPECT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
        auto *gui_window = ImGui::FindWindowByName("Signal Generator");
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
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1100, 800);
    io.DeltaTime = 1.f / 60;
    unsigned char *pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    SignalGenerator window;
    SignalGeneratorTestAccess::Start(window);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (SignalGeneratorTestAccess::Busy(window) && std::chrono::steady_clock::now() < deadline) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        window.Render();
        ImGui::Render();
        std::this_thread::yield();
    }
    ASSERT_TRUE(SignalGeneratorTestAccess::Result(window));
    const auto dir = std::filesystem::temp_directory_path() / "siggen-ui-image-export";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto png = (dir / "wave.png").string(), svg = (dir / "wave.svg").string();
    EXPECT_EQ(SignalGeneratorTestAccess::ExportImage(window, png, 0), "Saved " + png);
    EXPECT_EQ(SignalGeneratorTestAccess::ExportImage(window, svg, 1), "Saved " + svg);
    EXPECT_GT(std::filesystem::file_size(png), 1000u);
    EXPECT_GT(std::filesystem::file_size(svg), 1000u);
    EXPECT_NE(SignalGeneratorTestAccess::ExportImage(window, png, 0).find("Export failed"), std::string::npos);
    EXPECT_EQ(SignalGeneratorTestAccess::ExportImage(window, png, 0, true), "Saved " + png);
    std::filesystem::remove_all(dir);
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

} // namespace GUI
