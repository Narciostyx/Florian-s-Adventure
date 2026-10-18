#include "fa/debug/DebugStats.hpp"

#include <cstdio>

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

[[nodiscard]] std::string formatCount(std::uint64_t value) { return std::to_string(value); }

[[nodiscard]] std::string formatSize(std::size_t value) {
    return std::to_string(static_cast<unsigned long long>(value));
}

}  // namespace

std::vector<std::string> formatStats(const DebugStats& stats) {
    std::vector<std::string> lines;
    lines.reserve(14);
    lines.push_back("FA DEBUG");
    lines.push_back("fps " + formatFloat(stats.fps) + "  frame " + formatFloat(stats.frameMs) + " ms");
    lines.push_back("frame " + formatCount(stats.frameCount) + "  fixed " + formatCount(stats.fixedSteps) +
                    "  dropped " + formatCount(stats.droppedSteps) + "  sim " + formatDouble(stats.simulatedSeconds) +
                    " s");
    lines.push_back("entities " + formatSize(stats.entities) + "  types " + formatSize(stats.componentTypes) +
                    "  comps " + formatSize(stats.components));
    lines.push_back("draw " + formatCount(stats.drawCalls) + " calls  " + formatCount(stats.quads) +
                    " quads  " + formatCount(stats.textureSwitches) + " switch");
    lines.push_back("items " + formatSize(stats.worldDrawItems) + " world  " + formatSize(stats.uiDrawItems) +
                    " ui  tex " + formatCount(stats.liveTextures));
    lines.push_back("assets " + formatSize(stats.assetTextures) + " tex  " +
                    formatSize(stats.placeholderTextures) + " placeholder  " + formatSize(stats.failedAssets) +
                    " failed");
    lines.push_back("physics " + formatSize(stats.physicsBodies) + " bodies  " + formatSize(stats.physicsCells) +
                    " cells  " + formatCount(stats.physicsMoves) + " moves  " +
                    formatCount(stats.physicsBlocked) + " blocked");
    lines.push_back("audio " + formatCount(static_cast<std::uint64_t>(stats.audioVoices)) + " voices  " +
                    formatCount(static_cast<std::uint64_t>(stats.audioSampleRate)) + " Hz  " +
                    formatCount(stats.audioDropped) + " dropped");
    lines.push_back("map " + formatSize(stats.mapTiles) + " tiles  " + formatSize(stats.mapSolidTiles) + " solid");
    return lines;
}

}  // namespace fa::debug
