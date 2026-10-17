#include <fa/input/InputContext.hpp>

namespace fa::input {

std::string_view toString(InputContext context) noexcept {
    switch (context) {
        case InputContext::Gameplay:
            return "Gameplay";
        case InputContext::UI:
            return "UI";
        case InputContext::Dialogue:
            return "Dialogue";
        case InputContext::Editor:
            return "Editor";
        case InputContext::TextInput:
            return "TextInput";
        case InputContext::Count:
            break;
    }
    return "Gameplay";
}

InputContext inputContextFromName(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kInputContextCount; ++i) {
        const auto candidate = static_cast<InputContext>(i);
        if (toString(candidate) == name) {
            return candidate;
        }
    }
    return InputContext::Gameplay;
}

InputContextStack::InputContextStack() : m_stack{InputContext::Gameplay} {}

void InputContextStack::push(InputContext context) noexcept {
    if (context == InputContext::Count) {
        return;
    }
    m_stack.push_back(context);
}

InputContext InputContextStack::pop() noexcept {
    if (m_stack.size() > 1) {
        m_stack.pop_back();
    }
    return top();
}

bool InputContextStack::tryPop(InputContext context) noexcept {
    if (m_stack.size() <= 1 || m_stack.back() != context) {
        return false;
    }
    m_stack.pop_back();
    return true;
}

InputContext InputContextStack::top() const noexcept {
    return m_stack.empty() ? InputContext::Gameplay : m_stack.back();
}

bool InputContextStack::contains(InputContext context) const noexcept {
    for (const auto entry : m_stack) {
        if (entry == context) {
            return true;
        }
    }
    return false;
}

void InputContextStack::clear() noexcept {
    m_stack.clear();
    m_stack.push_back(InputContext::Gameplay);
}

}  // namespace fa::input
