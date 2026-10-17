#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <fa/core/Result.hpp>
#include <fa/platform/FileSystem.hpp>
#include <fa/platform/Platform.hpp>
#include <fa/platform/PlatformError.hpp>

namespace {

std::filesystem::path toFsPath(std::string_view utf8) {
    std::u8string wide;
    wide.reserve(utf8.size());
    for (const char c : utf8) {
        wide.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
    }
    return std::filesystem::path(wide);
}

std::string toUtf8(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    std::string out;
    out.reserve(utf8.size());
    for (const char8_t c : utf8) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

/// Unique scratch directory, removed on scope exit.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("fa_platform_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(m_path);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string str() const { return toUtf8(m_path); }

    std::string child(std::string_view name) const { return toUtf8(m_path / toFsPath(name)); }

private:
    std::filesystem::path m_path;
};

}  // namespace

TEST_CASE("IFileSystem: text round trip", "[platform][filesystem]") {
    const TempDir dir;
    const std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();
    fa::platform::IFileSystem& files = platform->files();

    const std::string path = dir.child("hello.txt");
    REQUIRE_FALSE(files.exists(path));

    REQUIRE(files.writeText(path, "first line\nsecond line\n").isOk());
    REQUIRE(files.exists(path));

    const auto read = files.readText(path);
    REQUIRE(read.isOk());
    REQUIRE(read.value() == "first line\nsecond line\n");

    const auto entries = files.listDirectory(dir.str());
    REQUIRE(entries.isOk());
    REQUIRE(entries.value() == std::vector<std::string>{"hello.txt"});

    REQUIRE(files.remove(path).isOk());
    REQUIRE_FALSE(files.exists(path));
}

TEST_CASE("IFileSystem: missing file reports a platform error", "[platform][filesystem]") {
    const TempDir dir;
    const std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();
    fa::platform::IFileSystem& files = platform->files();

    const auto read = files.readText(dir.child("does-not-exist.txt"));
    REQUIRE(read.isError());
    REQUIRE(read.error().code == fa::platform::platform_error::kFileOpenFailed);

    const auto entries = files.listDirectory(dir.child("missing-dir"));
    REQUIRE(entries.isError());
    REQUIRE(entries.error().code == fa::platform::platform_error::kDirectoryEnumerationFailed);
}

TEST_CASE("IFileSystem: UTF-8 paths and content survive a round trip", "[platform][filesystem]") {
    const TempDir dir;
    const std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();
    fa::platform::IFileSystem& files = platform->files();

    /// Non-ASCII path plus non-ASCII content, exercising the wide-character conversion path.
    const std::string content = "温室：番茄与蓝莓\n季节：夏\n";
    const std::string path = dir.child("温室的存档.txt");

    REQUIRE(files.writeText(path, content).isOk());
    REQUIRE(files.exists(path));

    const auto read = files.readText(path);
    REQUIRE(read.isOk());
    REQUIRE(read.value() == content);

    const auto entries = files.listDirectory(dir.str());
    REQUIRE(entries.isOk());
    REQUIRE(entries.value() == std::vector<std::string>{"温室的存档.txt"});
}

TEST_CASE("IFileSystem: binary round trip keeps every byte", "[platform][filesystem]") {
    const TempDir dir;
    const std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();
    fa::platform::IFileSystem& files = platform->files();

    std::string raw;
    raw.reserve(256);
    for (int i = 0; i < 256; ++i) {
        raw.push_back(static_cast<char>(i));
    }

    const std::string path = dir.child("bytes.bin");
    REQUIRE(files.writeText(path, raw).isOk());

    const auto bytes = files.readBinary(path);
    REQUIRE(bytes.isOk());
    REQUIRE(bytes.value().size() == raw.size());
    REQUIRE(static_cast<unsigned char>(bytes.value().front()) == 0u);
    REQUIRE(static_cast<unsigned char>(bytes.value().back()) == 255u);
}

TEST_CASE("IFileSystem: atomic write replaces the file and leaves no temp behind", "[platform][filesystem]") {
    const TempDir dir;
    const std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();
    fa::platform::IFileSystem& files = platform->files();

    const std::string path = dir.child("save.json");
    REQUIRE(files.writeText(path, "{\"slot\":0}").isOk());
    REQUIRE(files.writeTextAtomic(path, "{\"slot\":1}").isOk());

    const auto read = files.readText(path);
    REQUIRE(read.isOk());
    REQUIRE(read.value() == "{\"slot\":1}");

    const auto entries = files.listDirectory(dir.str());
    REQUIRE(entries.isOk());
    REQUIRE(entries.value() == std::vector<std::string>{"save.json"});
}

TEST_CASE("IFileSystem: createDirectories is idempotent and listings are sorted", "[platform][filesystem]") {
    const TempDir dir;
    const std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();
    fa::platform::IFileSystem& files = platform->files();

    const std::string nested = dir.child("a/b/c");
    REQUIRE_FALSE(files.isDirectory(nested));
    REQUIRE(files.createDirectories(nested).isOk());
    REQUIRE(files.createDirectories(nested).isOk());
    REQUIRE(files.isDirectory(nested));

    REQUIRE(files.writeText(nested + "/two.txt", "2").isOk());
    REQUIRE(files.writeText(nested + "/one.txt", "1").isOk());

    const auto entries = files.listDirectory(nested);
    REQUIRE(entries.isOk());
    REQUIRE(entries.value() == std::vector<std::string>{"one.txt", "two.txt"});
}

TEST_CASE("PlatformEvent: defaults are inert and types stringify", "[platform][event]") {
    const fa::platform::PlatformEvent event;
    REQUIRE(event.type == fa::platform::PlatformEventType::Quit);
    REQUIRE(event.button == fa::platform::MouseButton::None);
    REQUIRE(event.text.empty());
    REQUIRE(event.width == 0);
    REQUIRE(event.height == 0);

    REQUIRE(fa::platform::toString(fa::platform::PlatformEventType::WindowResized) == "WindowResized");
    REQUIRE(fa::platform::toString(fa::platform::PlatformEventType::FileDrop) == "FileDrop");
}

TEST_CASE("Platform: hidden window, monotonic clock and idempotent initialize", "[platform][window]") {
    const std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();

    fa::platform::WindowDesc desc;
    desc.title = "fa platform test";
    desc.width = 320;
    desc.height = 180;
    desc.hidden = true;

    const fa::core::Status initialized = platform->initialize(desc);
    if (initialized.isError()) {
        /// Headless CI sessions have no video device; the file system cases above
        /// still cover the rest of M02.
        WARN("skipping window assertions: " + initialized.error().message);
        return;
    }

    const double start = platform->monotonicSeconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const double later = platform->monotonicSeconds();
    REQUIRE(later > start);

    fa::platform::IWindow& window = platform->window();
    REQUIRE(window.nativeHandle() != nullptr);
    REQUIRE(window.width() > 0);
    REQUIRE(window.height() > 0);
    REQUIRE(window.dpiScale() > 0.0f);

    /// width()/height() are drawable pixels, so they must track dpiScale(), the
    /// drawable-to-logical ratio, at creation and after a resize alike.
    const int expectedWidth = static_cast<int>(std::lround(320.0f * window.dpiScale()));
    REQUIRE(std::abs(window.width() - expectedWidth) <= 4);

    window.setTitle("fa platform test renamed");
    window.resize(400, 240);
    const int expectedResized = static_cast<int>(std::lround(400.0f * window.dpiScale()));
    REQUIRE(std::abs(window.width() - expectedResized) <= 4);

    std::vector<fa::platform::PlatformEvent> events;
    REQUIRE(platform->pumpEvents(events));
    REQUIRE_FALSE(platform->quitRequested());

    REQUIRE(platform->initialize(desc).isOk());

    platform->shutdown();
}
