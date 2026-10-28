#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fa::debug {

class DebugConsole;

/// \brief One registered console command.
struct ConsoleCommand {
    std::string name;
    std::string help;
    std::function<void(DebugConsole&, std::span<const std::string>)> handler;
};

/// \brief Text console state: command registry, output ring, history and input line.
///
/// The console is pure data and has no UI or input dependency. `DebugTools` routes
/// platform text/key events into it; `DebugOverlay` draws its output.
class DebugConsole {
public:
    static constexpr std::size_t kMaxLines = 256;
    static constexpr std::size_t kMaxHistory = 64;

    DebugConsole();

    /// \brief Adds a command. Duplicate or empty names are rejected.
    [[nodiscard]] bool registerCommand(ConsoleCommand command);

    /// \brief Parses and runs one line, appending the echo and any output.
    /// \return false when the line was empty or the command was unknown.
    [[nodiscard]] bool execute(std::string_view line);

    void print(std::string_view line);
    void clear();
    [[nodiscard]] const std::vector<std::string>& lines() const noexcept { return m_lines; }
    [[nodiscard]] const std::vector<std::string>& history() const noexcept { return m_history; }
    [[nodiscard]] const std::vector<ConsoleCommand>& commands() const noexcept { return m_commands; }

    void setOpen(bool open) noexcept;
    [[nodiscard]] bool open() const noexcept { return m_open; }

    [[nodiscard]] const std::string& input() const noexcept { return m_input; }
    void setInput(std::string text);
    void appendInput(std::string_view text);
    void backspace();
    /// \brief Executes the current input, clears it and records history.
    [[nodiscard]] bool submitInput();
    void historyPrevious();
    void historyNext();

private:
    void registerBuiltins();

    std::vector<ConsoleCommand> m_commands;
    std::vector<std::string> m_lines;
    std::vector<std::string> m_history;
    std::size_t m_historyCursor = 0;
    std::string m_input;
    bool m_open = false;
};

/// \brief Splits a console line into arguments. Supports double quotes and `\"`.
[[nodiscard]] std::vector<std::string> splitCommandLine(std::string_view line);

/// \brief Removes leading/trailing whitespace.
[[nodiscard]] std::string trimCommandLine(std::string_view line);

}  // namespace fa::debug
