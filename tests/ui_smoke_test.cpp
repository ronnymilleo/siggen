#include "implot.h"
#include "implot_internal.h"
#include "help_topics.h"
#include "signal_generator.h"
#include <gtest/gtest.h>
// The tab bar lives inside a layout table cell, whose ID scope differs from the window's; find it by its tab names.
static ImGuiTabBar* find_signal_views()
{
    auto* ctx = ImGui::GetCurrentContext();
    for (int i = 0; i < ctx->TabBars.GetMapSize(); ++i)
        if (auto* bar = ctx->TabBars.TryGetMapData(i))
            for (auto& tab : bar->Tabs)
                if (std::string(ImGui::TabBarGetTabName(bar, &tab)) == "Waveform")
                    return bar;
    return nullptr;
}

TEST(UI, InitialInvalidAndResizedFrames)
{
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto& io       = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime   = 1.f / 60;
    unsigned char* pixels;
    int            w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    for (bool invalid : {false, true})
    {
        iq::GenerationConfig c;
        if (invalid)
            c.symbol_count = -1;
        SignalGenerator window(c);
        for (auto size : {ImVec2(1100, 900), ImVec2(640, 480), ImVec2(1600, 1000)})
        {
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
            const auto* resized = ImGui::FindWindowByName("Signal Generator");
            ASSERT_NE(resized, nullptr);
            const auto* viewport = ImGui::GetMainViewport();
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

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <cstdlib>
#include <filesystem>
#include <thread>

struct SignalGeneratorTestAccess
{
    static void start(SignalGenerator& w)
    {
        w.job_.start(w.config_);
    }
    static bool busy(SignalGenerator& w)
    {
        return w.job_.busy();
    }
    static void edit(SignalGenerator& w)
    {
        w.config_.seed++;
    }
    static void matched(SignalGenerator& w, bool enabled)
    {
        w.constellation_view_ = enabled ? 1 : 0;
    }
    static const auto& result(SignalGenerator& w)
    {
        return w.job_.result();
    }
    static const auto& error(SignalGenerator& w)
    {
        return w.error_;
    }
    static void invalid(SignalGenerator& w)
    {
        w.config_.symbol_count = 0;
    }
};
TEST(UI, GeneratedViewsAndPendingClosure)
{
    const char* capture = std::getenv("SIGGEN_CAPTURE_DIR");
    GLFWwindow* native  = nullptr;
    if (capture)
    {
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
    auto& io       = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime   = 1.f / 60;
    io.DisplaySize = ImVec2(1100, 1100);
    if (native)
    {
        ImGui_ImplGlfw_InitForOpenGL(native, true);
        ImGui_ImplOpenGL3_Init("#version 330");
    }
    else
    {
        unsigned char* pixels;
        int            w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    }
    auto frame = [&](SignalGenerator& window) {
        if (native)
        {
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
        if (native)
        {
            glViewport(0, 0, 1100, 1100);
            glClearColor(.1f, .1f, .1f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glFinish();
        }
    };
    for (auto modulation : {iq::Modulation::BPSK, iq::Modulation::QPSK, iq::Modulation::PSK8,
                            iq::Modulation::QAM16, iq::Modulation::QAM64, iq::Modulation::QAM256,
                            iq::Modulation::OOK, iq::Modulation::PAM4, iq::Modulation::DBPSK,
                            iq::Modulation::DQPSK, iq::Modulation::QAM32, iq::Modulation::OQPSK, iq::Modulation::PI4DQPSK, iq::Modulation::DPSK8, iq::Modulation::ASK4, iq::Modulation::FSK2, iq::Modulation::FSK4,
                            iq::Modulation::MSK})
    {
        iq::GenerationConfig c;
        c.modulation = modulation;
        const bool fsk = iq::waveform_family(modulation) == iq::Family::Fsk;
        if (fsk)
        {
            c.awgn.enabled = true; // Noise makes the frequency estimate visibly non-ideal.
            c.awgn.snr_db  = 20;
        }
        SignalGenerator window(c);
        SignalGeneratorTestAccess::start(window);
        SignalGeneratorTestAccess::edit(window); // Edit while work is pending.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (SignalGeneratorTestAccess::busy(window) && std::chrono::steady_clock::now() < deadline)
        {
            frame(window);
            std::this_thread::yield();
        }
        ASSERT_FALSE(SignalGeneratorTestAccess::busy(window));
        ASSERT_TRUE(SignalGeneratorTestAccess::result(window));
        EXPECT_EQ(SignalGeneratorTestAccess::result(window)->config.seed, c.seed);
        EXPECT_TRUE(SignalGeneratorTestAccess::error(window).empty());
        if (fsk)
        {
            frame(window);
            frame(window);
            auto* bar = find_signal_views();
            ASSERT_NE(bar, nullptr);
            bool saw_frequency = false;
            for (auto& tab : bar->Tabs)
            {
                const std::string name = ImGui::TabBarGetTabName(bar, &tab);
                EXPECT_NE(name, "Constellation"); // FSK has no symbol constellation or matched-filter eye.
                EXPECT_NE(name, "Eye");
                EXPECT_NE(name, "Pipeline"); // No symbol/filter pipeline for FSK.
                saw_frequency |= name == "Frequency";
            }
            EXPECT_TRUE(saw_frequency);
        }
        else
        {
            frame(window);
            frame(window);
            auto* bar = find_signal_views();
            ASSERT_NE(bar, nullptr);
            bool saw_pipeline = false;
            for (auto& tab : bar->Tabs)
                saw_pipeline |= std::string(ImGui::TabBarGetTabName(bar, &tab)) == "Pipeline";
            EXPECT_TRUE(saw_pipeline);
        }
        const std::vector<const char*> views =
            fsk ? std::vector<const char*>{"Waveform", "Frequency", "Spectrum"}
                : std::vector<const char*>{"Waveform", "Constellation", "Matched", "Eye", "Pipeline", "Spectrum"};
        for (const char* view : views)
        {
            frame(window);
            auto* gui_window = ImGui::FindWindowByName("Signal Generator");
            ASSERT_NE(gui_window, nullptr);
            auto* bar = find_signal_views();
            ASSERT_NE(bar, nullptr);
            const bool matched = std::string(view) == "Matched";
            SignalGeneratorTestAccess::matched(window, matched);
            for (auto& tab : bar->Tabs)
                if (std::string(ImGui::TabBarGetTabName(bar, &tab)) == (matched ? "Constellation" : view))
                    bar->NextSelectedTabId = tab.ID;
            frame(window);
            frame(window);
            EXPECT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
            if (std::string(view) == "Constellation" || matched)
            {
                bool checked = false;
                for (auto& plot : ImPlot::GetCurrentContext()->Plots.Buf)
                {
                    if (plot.Flags & ImPlotFlags_Equal)
                    {
                        EXPECT_NEAR(plot.XAxis(0).GetAspect(), plot.YAxis(0).GetAspect(), 1e-12);
                        const bool real_axis = modulation == iq::Modulation::BPSK || modulation == iq::Modulation::DBPSK ||
                                               modulation == iq::Modulation::OOK || modulation == iq::Modulation::PAM4 ||
                                               modulation == iq::Modulation::ASK4;
                        if (!real_axis)
                        {
                            const double outer = modulation == iq::Modulation::QPSK || modulation == iq::Modulation::DQPSK || modulation == iq::Modulation::OQPSK
                                                     ? 1 / std::sqrt(2.)
                                                 : modulation == iq::Modulation::PSK8 || modulation == iq::Modulation::PI4DQPSK || modulation == iq::Modulation::DPSK8 ? 1.
                                                 : modulation == iq::Modulation::QAM16  ? 3 / std::sqrt(10.)
                                                 : modulation == iq::Modulation::QAM256 ? 15 / std::sqrt(170.)
                                                 : modulation == iq::Modulation::QAM32  ? 5 / std::sqrt(20.)
                                                                                        : 7 / std::sqrt(42.);
                            EXPECT_GT(plot.YAxis(0).Range.Max, outer);
                            EXPECT_LT(plot.YAxis(0).Range.Min, -outer);
                        }
                        checked = true;
                    }
                }
                EXPECT_TRUE(checked);
            }
            if (capture)
            {
                std::vector<unsigned char> pixels(1100 * 1100 * 4);
                glReadPixels(0, 0, 1100, 1100, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                stbi_flip_vertically_on_write(true);
                const auto path = std::filesystem::path(capture) /
                                  (std::string(iq::modulation_name(modulation)) + "-" + view + ".png");
                EXPECT_TRUE(stbi_write_png(path.string().c_str(), 1100, 1100, 4, pixels.data(), 1100 * 4));
            }
        }
        auto previous = SignalGeneratorTestAccess::result(window);
        SignalGeneratorTestAccess::invalid(window);
        SignalGeneratorTestAccess::start(window);
        const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (SignalGeneratorTestAccess::busy(window) && std::chrono::steady_clock::now() < failure_deadline)
            frame(window);
        EXPECT_EQ(SignalGeneratorTestAccess::result(window), previous);
        EXPECT_FALSE(SignalGeneratorTestAccess::error(window).empty());
    }
    {
        iq::GenerationConfig c;
        c.symbol_count = 65536;
        SignalGenerator closing(c);
        SignalGeneratorTestAccess::start(closing);
        // Destruction with an outstanding job must complete before destroying the UI context.
    }
    if (native)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (native)
    {
        glfwDestroyWindow(native);
        glfwTerminate();
    }
}

TEST(UI, NoiseSourceRendersWithoutConstellation)
{
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto& io       = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime   = 1.f / 60;
    io.DisplaySize = ImVec2(1100, 900);
    unsigned char* pixels;
    int            w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    iq::GenerationConfig c;
    c.modulation = iq::Modulation::WGN;
    SignalGenerator window(c);
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        window.Render();
        ImGui::Render();
    };
    SignalGeneratorTestAccess::start(window);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (SignalGeneratorTestAccess::busy(window) && std::chrono::steady_clock::now() < deadline)
    {
        frame();
        std::this_thread::yield();
    }
    ASSERT_FALSE(SignalGeneratorTestAccess::busy(window));
    ASSERT_TRUE(SignalGeneratorTestAccess::result(window));
    EXPECT_TRUE(SignalGeneratorTestAccess::error(window).empty());
    EXPECT_EQ(SignalGeneratorTestAccess::result(window)->family, iq::Family::Noise);
    for (int pass = 0; pass < 3; ++pass)
    {
        frame();
        EXPECT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
        auto* gui_window = ImGui::FindWindowByName("Signal Generator");
        ASSERT_NE(gui_window, nullptr);
        auto* bar = find_signal_views();
        ASSERT_NE(bar, nullptr);
        bool saw_waveform = false, saw_spectrum = false;
        for (auto& tab : bar->Tabs)
        {
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

TEST(UI, EveryHelpTopicHasATitleAndExplanation)
{
    for (const auto& topic : help::all)
    {
        EXPECT_FALSE(topic.id.empty());
        EXPECT_FALSE(topic.title.empty()) << topic.id;
        EXPECT_GT(topic.body.size(), 100u) << topic.id;
    }
    // The topics the controls link to: roll-off, span, SPS and the SNR / Es/N0 / Eb/N0 relation.
    EXPECT_NE(help::snr.body.find("Es/N0"), std::string_view::npos);
    EXPECT_NE(help::snr.body.find("Eb/N0"), std::string_view::npos);
}
