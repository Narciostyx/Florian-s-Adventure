#include "fa/debug/DebugConsole.hpp"

#include <utility>

namespace fa::debug {

std::string trimCommandLine(std::string_view line) {
    std::size_t begin = 0;
    while (begin < line.size() &&
           (line[begin] == ' ' || line[begin] == '\t' || line[begin] == '\r' || line[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = line.size();
    while (end > begin &&
           (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r' || line[end - 1] == '\n')) {
        --end;
    }
    return std::string(line.substr(begin, end - begin));
}

std::vector<std::string> splitCommandLine(std::string_view line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    bool escaping = false;
    bool tokenStarted = false;

    for (const char character : line) {
        if (escaping) {
            current.push_back(character);
            escaping = false;
            tokenStarted = true;
            continue;
        }
        if (character == '\\') {
            escaping = true;
            continue;
        }
        if (character == '"') {
            inQuotes = !inQuotes;
            tokenStarted = true;
            continue;
        }
        if (!inQuotes && (character == ' ' || character == '\t')) {
            if (tokenStarted) {
                tokens.push_back(std::move(current));
                current.clear();
                tokenStarted = false;
            }
            continue;
        }
        current.push_back(character);
        tokenStarted = true;
    }

    if (escaping) {
        current.push_back('\\');
    }
    if (tokenStarted) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

DebugConsole::DebugConsole() {
    registerBuiltins();
}

bool DebugConsole::registerCommand(ConsoleCommand command) {
    if (command.name.empty() || !command.handler) {
        return false;
    }
    for (const ConsoleCommand& existing : m_commands) {
        if (existing.name == command.name) {
            return false;
        }
    }
    m_commands.push_back(std::move(command));
    return true;
}

bool DebugConsole::execute(std::string_view line) {
    const std::string trimmed = trimCommandLine(line);
    if (trimmed.empty()) {
        return false;
    }

    print("> " + trimmed);
    if (m_history.empty() || m_history.back() != trimmed) {
        m_history.push_back(trimmed);
        if (m_history.size() > kMaxHistory) {
            m_history.erase(m_history.begin());
        }
    }
    m_historyCursor = m_history.size();

    const std::vector<std::string> tokens = splitCommandLine(trimmed);
    if (tokens.empty()) {
        return false;
    }

    for (const ConsoleCommand& command : m_commands) {
        if (command.name == tokens[0]) {
            const std::vector<std::string> args(tokens.begin() + 1, tokens.end());
            command.handler(*this, std::span<const std::string>(args));
            return true;
        }
    }

    print("unknown command: " + tokens[0] + " (try help)");
    return false;
}

void DebugConsole::print(std::string_view line) {
    m_lines.emplace_back(line);
    if (m_lines.size() > kMaxLines) {
        m_lines.erase(m_lines.begin());
    }
}

void DebugConsole::clear() {
    m_lines.clear();
}

void DebugConsole::setOpen(bool open) noexcept {
    m_open = open;
    m_historyCursor = m_history.size();
    if (!open) {
        m_input.clear();
    }
}

void DebugConsole::setInput(std::string text) {
    m_input = std::move(text);
}

void DebugConsole::appendInput(std::string_view text) {
    for (const char character : text) {
        if (character != '\r' && character != '\n') {
            m_input.push_back(character);
        }
    }
}

void DebugConsole::backspace() {
    if (!m_input.empty()) {
        m_input.pop_back();
    }
}

bool DebugConsole::submitInput() {
    if (m_input.empty()) {
        return false;
    }
    const std::string line = m_input;
    m_input.clear();
    return execute(line);
}

void DebugConsole::historyPrevious() {
    if (m_history.empty() || m_historyCursor == 0) {
        return;
    }
    --m_historyCursor;
    m_input = m_history[m_historyCursor];
}

void DebugConsole::historyNext() {
    if (m_historyCursor + 1 < m_history.size()) {
        ++m_historyCursor;
        m_input = m_history[m_historyCursor];
    } else {
        m_historyCursor = m_history.size();
        m_input.clear();
    }
}

void DebugConsole::registerBuiltins() {
    static_cast<void>(registerCommand(ConsoleCommand{
        "help", "list commands or show help for one", [](DebugConsole& console, std::span<const std::string> args) {
            if (!args.empty()) {
                for (const ConsoleCommand& command : console.commands()) {
                    if (command.name == args[0]) {
                        console.print(command.name + " - " + command.help);
                        return;
                    }
                }
                console.print("no such command: " + args[0]);
                return;
            }
            for (const ConsoleCommand& command : console.commands()) {
                console.print(command.name + " - " + command.help);
            }
        }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"clear", "clear the console output", [](DebugConsole& console, std::span<const std::string>) {
                           console.clear();
                       }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"history", "show command history", [](DebugConsole& console, std::span<const std::string>) {
                           const auto& history = console.history();
                           for (std::size_t index = 0; index < history.size(); ++index) {
                               console.print(std::to_string(index + 1) + "  " + history[index]);
                           }
                       }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"echo", "print the arguments", [](DebugConsole& console, std::span<const std::string> args) {
                           std::string text;
                           for (std::size_t index = 0; index < args.size(); ++index) {
                               if (index > 0) {
                                   text.push_back(' ');
                               }
                               text += args[index];
                           }
                           console.print(text);
                       }}));
}

}  // namespace fa::debug
