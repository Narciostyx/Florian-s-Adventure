#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fa::debug {

/// \brief One snapshot for the debug overlay and the smoke summary.
///
/// The composition root fills this from the owning modules; M27 never reaches into
/// they internals itself. Keeping the snapshot plain data is what lets the overlay
/// be tested without a renderer or an audio device.
struct DebugStats {
    float fps = 0.0f;
    float frameMs = 0.0f;
    std::uint64_t frameCount = 0;
    std::uint64_t fixedSteps = 0;
    std::uint64_t droppedSteps = 0;
    double simulatedSeconds = 0.0;

    std::size_t entities = 0;
    std::size_t componentTypes = 0;
    std::size_t components = 0;

    std::size_t worldDrawItems = 0;
    std::size_t uiDrawItems = 0;
    std::uint32_t drawCalls = 0;
    std::uint32_t quads = 0;
    std::uint32_t textureSwitches = 0;
    std::uint32_t liveTextures = 0;

    std::size_t assetTextures = 0;
    std::size_t placeholderTextures = 0;
    std::size_t failedAssets = 0;

    std::size_t physicsBodies = 0;
    std::size_t physicsCells = 0;
    std::uint64_t physicsMoves = 0;
    std::uint64_t physicsBlocked = 0;

    int audioVoices = 0;
    int audioSampleRate = 0;
    std::uint64_t audioDropped = 0;

    std::size_t mapTiles = 0;
    std::size_t mapSolidTiles = 0;
};

/// \brief Formats the snapshot as short overlay lines.
[[nodiscard]] std::vector<std::string> formatStats(const DebugStats& stats);

}  // namespace fa::debug
