#include "GlApi.hpp"

namespace fa::render::detail {

bool loadGlFunctions(GlFunctions& functions, platform::IGlContext& context, const char** outMissing) {
    functions = GlFunctions{};
    if (outMissing != nullptr) {
        *outMissing = nullptr;
    }

    bool ok = true;
#define FA_GL_RESOLVE(returnType, name, args)                                                                          \
    {                                                                                                                  \
        void* address = context.procAddress("gl" #name);                                                               \
        functions.gl##name = reinterpret_cast<decltype(functions.gl##name)>(address);                                  \
        if (functions.gl##name == nullptr && ok) {                                                                     \
            ok = false;                                                                                                \
            if (outMissing != nullptr) {                                                                               \
                *outMissing = "gl" #name;                                                                              \
            }                                                                                                          \
        }                                                                                                              \
    }
    FA_GL_FUNCTION_LIST(FA_GL_RESOLVE)
#undef FA_GL_RESOLVE

    functions.loaded = ok;
    return ok;
}

GLenum takeGlError(const GlFunctions& functions) {
    if (functions.glGetError == nullptr) {
        return GL_NO_ERROR;
    }
    GLenum first = GL_NO_ERROR;
    for (int guard = 0; guard < 16; ++guard) {
        const GLenum error = functions.glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        if (first == GL_NO_ERROR) {
            first = error;
        }
    }
    return first;
}

}  // namespace fa::render::detail
