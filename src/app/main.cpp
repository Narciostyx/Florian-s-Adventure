/// \file
/// \brief Composition root: the only translation unit that knows about every engine module.
///
/// Wires M02 (platform) so the window, event and clock path can be verified end
/// to end. M04 (frame loop) and M06 (render) are attached here later.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fa/core/Result.hpp>
#include <fa/core/Version.hpp>
#include <fa/platform/Platform.hpp>

namespace {

struct Options {
    /// A visible window is opt-in so CI and automated runs never block.
    bool visibleWindow = false;
    /// 0 means "run until the user closes the window".
    int frames = 0;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--window") {
            options.visibleWindow = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            options.frames = std::atoi(argv[++i]);
        }
    }
    return options;
}

void printBanner() {
    const std::string_view version = fa::core::engineVersion();
    std::printf("Florian's Adventure - engine v%.*s\n", static_cast<int>(version.size()), version.data());
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    printBanner();

    std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();

    fa::platform::WindowDesc desc;
    desc.title = "Florian's Adventure";
    desc.width = 1280;
    desc.height = 720;
    desc.hidden = !options.visibleWindow;

    const fa::core::Status initialized = platform->initialize(desc);
    if (initialized.isError()) {
        std::printf("platform init failed (%d): %s\n", initialized.error().code, initialized.error().message.c_str());
        return 1;
    }

    const fa::platform::IWindow& window = platform->window();
    std::printf("window %dx%d px, dpi scale %.2f\n", window.width(), window.height(),
                static_cast<double>(window.dpiScale()));

    /// A hidden smoke run needs a few frames; a visible window runs until closed.
    const int frameLimit = options.frames > 0 ? options.frames : (options.visibleWindow ? 0 : 3);

    std::vector<fa::platform::PlatformEvent> events;
    int frames = 0;
    int quitEvents = 0;
    int resizeEvents = 0;

    while (true) {
        events.clear();
        const bool stillRunning = platform->pumpEvents(events);

        for (const fa::platform::PlatformEvent& event : events) {
            switch (event.type) {
                case fa::platform::PlatformEventType::Quit:
                    ++quitEvents;
                    break;
                case fa::platform::PlatformEventType::WindowResized:
                    ++resizeEvents;
                    std::printf("resized -> %dx%d\n", event.width, event.height);
                    break;
                default:
                    break;
            }
        }

        ++frames;
        if (!stillRunning) {
            break;
        }
        if (frameLimit > 0 && frames >= frameLimit) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    const double elapsed = platform->monotonicSeconds();
    platform->shutdown();

    std::printf("ran %d frame(s) in %.3fs (quit events: %d, resize events: %d)\n", frames, elapsed, quitEvents,
                resizeEvents);
    std::printf("M02 platform smoke test OK\n");
    return 0;
}
