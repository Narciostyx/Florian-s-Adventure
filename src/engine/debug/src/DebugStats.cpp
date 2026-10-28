#include "fa/debug/DebugStats.hpp"

#include <cstdio>
#include <utility>

namespace fa::debug {
namespace {

[[nodiscard]] std::string formatFloat(float value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(value));
    return buffer;
}

[[nodiscard]] std::string formatDouble(double value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

[[nodiscard]] std::string formatCount(std::uint64_t value) {
    return std::to_string(value);
}

[[nodiscard]] std::string formatSize(std::size_t value) {
    return std::to_string(static_cast<unsigned long long>(value));
}

[[nodiscard]] std::string formatVector(core::Vec2 value) {
    return "(" + formatFloat(value.x) + ", " + formatFloat(value.y) + ")";
}

[[nodiscard]] std::string formatCell(core::Vec2 value) {
    char buffer[48]{};
    std::snprintf(buffer, sizeof(buffer), "(%.0f, %.0f)", static_cast<double>(value.x), static_cast<double>(value.y));
    return buffer;
}

}  // namespace

std::vector<std::string> formatStats(const DebugStats& stats) {
    std::vector<std::string> lines;
    lines.reserve(24);
    lines.push_back("FA DEBUG");
    lines.push_back("fps " + formatFloat(stats.fps) + "  frame " + formatFloat(stats.frameMs) + " ms");
    lines.push_back("frame " + formatCount(stats.frameCount) + "  fixed " + formatCount(stats.fixedSteps) +
                    "  dropped " + formatCount(stats.droppedSteps) + "  sim " + formatDouble(stats.simulatedSeconds) +
                    " s");
    lines.push_back("entities " + formatSize(stats.entities) + "  types " + formatSize(stats.componentTypes) +
                    "  comps " + formatSize(stats.components));
    lines.push_back("draw " + formatCount(stats.drawCalls) + " calls  " + formatCount(stats.quads) + " quads  " +
                    formatCount(stats.textureSwitches) + " switch");
    lines.push_back("items " + formatSize(stats.worldDrawItems) + " world  " + formatSize(stats.uiDrawItems) +
                    " ui  tex " + formatCount(stats.liveTextures));
    lines.push_back("assets " + formatSize(stats.assetTextures) + " tex  " + formatSize(stats.placeholderTextures) +
                    " placeholder  " + formatSize(stats.failedAssets) + " failed");
    lines.push_back("physics " + formatSize(stats.physicsBodies) + " bodies  " + formatSize(stats.physicsCells) +
                    " cells  " + formatCount(stats.physicsMoves) + " moves  " + formatCount(stats.physicsBlocked) +
                    " blocked");
    lines.push_back("audio " + formatCount(static_cast<std::uint64_t>(stats.audioVoices)) + " voices  " +
                    formatCount(static_cast<std::uint64_t>(stats.audioSampleRate)) + " Hz  " +
                    formatCount(stats.audioDropped) + " dropped");
    lines.push_back("map " + formatSize(stats.mapTiles) + " tiles  " + formatSize(stats.mapSolidTiles) + " solid");
    if (stats.hasPlayerPosition) {
        std::string line = "player " + formatVector(stats.playerPosition);
        if (stats.hasPlayerCell) {
            line += "  tile " + formatCell(stats.playerCell);
        }
        lines.push_back(std::move(line));
    }

    if (stats.camera.valid) {
        lines.push_back("camera " + formatVector(stats.camera.position) + "  view " +
                        formatFloat(stats.camera.viewportSize.x) + "x" + formatFloat(stats.camera.viewportSize.y));
        std::string follow = "follow ";
        follow += stats.camera.following ? "player " + scene::toString(stats.camera.followTarget) : std::string("none");
        follow += "  dz " + formatFloat(stats.camera.deadzoneX) + "x" + formatFloat(stats.camera.deadzoneY) +
                  "  smooth " + formatFloat(stats.camera.smoothing);
        lines.push_back(std::move(follow));
    }

    lines.push_back(stats.aiTargets.empty() ? std::string("ai targets none")
                                            : "ai targets " + formatSize(stats.aiTargets.size()));
    constexpr std::size_t kMaxAiLines = 6;
    const std::size_t shown = stats.aiTargets.size() < kMaxAiLines ? stats.aiTargets.size() : kMaxAiLines;
    for (std::size_t index = 0; index < shown; ++index) {
        const DebugAiTargetInfo& info = stats.aiTargets[index];
        std::string line = "  " + scene::toString(info.agent) + " " + info.state + " -> ";
        line += info.hasTarget ? scene::toString(info.target) : std::string("none");
        line += info.targetVisible ? " vis" : " hid";
        lines.push_back(std::move(line));
    }
    if (stats.aiTargets.size() > shown) {
        lines.push_back("  +" + formatSize(stats.aiTargets.size() - shown) + " more");
    }
    return lines;
}

}  // namespace fa::debug
