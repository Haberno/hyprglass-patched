// Test-only capture pass: read the rendered framebuffer without screencopy
// requesting a full repaint and hiding partial-damage regressions.
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprutils/signal/Listener.hpp>
#include <GLES3/gl32.h>
#include <fstream>
#include <cstdlib>

static Hyprutils::Signal::CHyprSignalListener listener;
static SP<SHyprCtlCommand> command;
static HANDLE handle;
static int pending = 0, sequence = 0;

class CapturePass : public IPassElement {
  public:
    std::vector<UP<IPassElement>> draw() override {
        if (!pending)
            return {};
        const auto& data = g_pHyprRenderer->m_renderData;
        int width = data.currentFB->m_size.x, height = data.currentFB->m_size.y;
        std::vector<unsigned char> pixels(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        const std::string base = std::string(std::getenv("HYPRGLASS_PROBE_DIR")) + "/" + std::to_string(pending);
        std::ofstream image(base + ".rgba", std::ios::binary);
        image.write(reinterpret_cast<char*>(pixels.data()), pixels.size());
        image.close();
        std::ofstream info(base + ".txt");
        info << width << ' ' << height << '\n';
        for (const auto& rect : data.damage.getRects())
            info << rect.x1 << ' ' << rect.y1 << ' ' << rect.x2 << ' ' << rect.y2 << '\n';
        pending = 0;
        return {};
    }
    bool needsLiveBlur() override { return false; }
    bool needsPrecomputeBlur() override { return false; }
    bool undiscardable() override { return true; }
    const char* passName() override { return "HyprGlassTestCapture"; }
    ePassElementType type() override { return EK_CUSTOM; }
};

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE h) {
    handle = h;
    listener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        if (stage == RENDER_LAST_MOMENT && pending)
            g_pHyprRenderer->m_renderPass.add(makeUnique<CapturePass>());
    });
    command = HyprlandAPI::registerHyprCtlCommand(handle, SHyprCtlCommand{
        .name = "glassprobe", .exact = false,
        .fn = [](eHyprCtlOutputFormat, std::string request) -> std::string {
            pending = ++sequence;
            // A small repaint inside the test pane, with no client content change.
            g_pHyprRenderer->damageBox(request.ends_with("full") ? CBox{0, 0, 10000, 10000} : CBox{140, 140, 8, 8});
            return std::to_string(pending);
        }});
    return {"glassprobe", "Partial-damage regression capture", "HyprGlass tests", "1"};
}
APICALL EXPORT void PLUGIN_EXIT() {
    listener.reset();
    HyprlandAPI::unregisterHyprCtlCommand(handle, command);
    command.reset();
    g_pHyprRenderer->m_renderPass.removeAllOfType("HyprGlassTestCapture");
}
