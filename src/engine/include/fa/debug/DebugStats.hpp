#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/scene/EntityId.hpp>

namespace fa::debug {

/// \brief Camera state of the currently controlled character.
///
/// The composition root fills this from its live `Camera2D`; M27 stays unaware of
/// the render module and only formats/draws the values it is handed.
struct DebugCameraInfo {
    bool valid = false;
    core::Vec2 position{};
    core::Vec2 viewportSize{};
    core::Rect visible{};
    scene::EntityId followTarget{};
    bool following = false;
    float deadzoneX = 0.0f;
    float deadzoneY = 0.0f;
    float smoothing = 0.0f;
};

/// \brief One AI agent and the target it currently has locked.
///
/// Positions are included so the composition root can draw an enemy-to-target
/// line without asking M27 to query the world.
struct DebugAiTargetInfo {
    scene::EntityId agent{};
    scene::EntityId target{};
    bool hasTarget = false;
    bool targetVisible = false;
    std::string state;
    core::Vec2 agentPosition{};
    core::Vec2 targetPosition{};
    bool hasAgentPosition = false;
    bool hasTargetPosition = false;
};

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

    /// \brief Player centre in world pixels and its tile coordinate.
    bool hasPlayerPosition = false;
    core::Vec2 playerPosition{};
    bool hasPlayerCell = false;
    core::Vec2 playerCell{};

    DebugCameraInfo camera;
    std::vector<DebugAiTargetInfo> aiTargets;
};

/// \brief Formats the snapshot as short overlay lines.
[[nodiscard]] std::vector<std::string> formatStats(const DebugStats& stats);

}  // namespace fa::debug
