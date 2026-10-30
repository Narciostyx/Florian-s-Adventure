#pragma once

#include <string_view>

#include <fa/core/Result.hpp>
#include <fa/ui/Font.hpp>

namespace fa::platform {
class IFileSystem;
}

namespace fa::app {

/// \brief Loads the font atlas baked by `tools/build_font_atlas.py`.
///
/// Reads `<assets>/fonts/ui_atlas.json` plus its PNG and returns a filled `FontAtlas`
/// with an invalid texture handle: the caller uploads `pixels` and owns the texture,
/// exactly as it does for the built-in font. A missing or unreadable atlas is an error,
/// never a silent fallback - `initializeUi` decides what that means.
///
/// Lives in the composition root rather than in M09 because parsing needs the file
/// system and JSON, and `fa_ui` deliberately depends on neither (dependency-rules).
[[nodiscard]] core::Result<ui::FontAtlas> loadBakedUiFont(platform::IFileSystem& fileSystem,
                                                          std::string_view assetsRoot);

}  // namespace fa::app
