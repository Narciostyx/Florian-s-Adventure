// fa_render - M00 placeholder translation unit.
// SDL3 + OpenGL 4.1 renderer with 640x360 virtual resolution lands with M06.

namespace fa::render {

constexpr int kVirtualWidth = 640;
constexpr int kVirtualHeight = 360;

int virtualWidth() noexcept {
    return kVirtualWidth;
}

int virtualHeight() noexcept {
    return kVirtualHeight;
}

}  // namespace fa::render
