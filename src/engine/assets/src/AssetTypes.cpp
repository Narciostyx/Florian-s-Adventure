#include <fa/assets/AssetTypes.hpp>

#include <fa/assets/AssetHandle.hpp>

namespace fa::assets {

std::string_view toString(AssetKind kind) noexcept {
    switch (kind) {
        case AssetKind::Texture:
            return "Texture";
        case AssetKind::JsonText:
            return "JsonText";
        default:
            return "Unknown";
    }
}

std::string_view toString(AssetState state) noexcept {
    switch (state) {
        case AssetState::Unloaded:
            return "Unloaded";
        case AssetState::Loading:
            return "Loading";
        case AssetState::Ready:
            return "Ready";
        case AssetState::Failed:
            return "Failed";
        default:
            return "Unknown";
    }
}

}  // namespace fa::assets
