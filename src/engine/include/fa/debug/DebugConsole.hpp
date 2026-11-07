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

    /// \brief Appends one output line to the ring.
    void print(std::string_view line);
    /// \brief Empties the output ring (history is kept).
    void clear();
    /// \brief Output lines, oldest first.
    [[nodiscard]] const std::vector<std::string>& lines() const noexcept { return m_lines; }
    /// \brief Submitted command lines, oldest first.
    [[nodiscard]] const std::vector<std::string>& history() const noexcept { return m_history; }
    /// \brief The command registry.
    [[nodiscard]] const std::vector<ConsoleCommand>& commands() const noexcept { return m_commands; }

    /// \brief Opens or closes the console; closing clears the input line.
    void setOpen(bool open) noexcept;
    /// \brief Whether the console currently owns the keyboard.
    [[nodiscard]] bool open() const noexcept { return m_open; }

    /// \brief The unsubmitted input line.
    [[nodiscard]] const std::string& input() const noexcept { return m_input; }
    /// \brief Replaces the input line (history recall).
    void setInput(std::string text);
    /// \brief Appends printable characters, ignoring carriage returns and newlines.
    void appendInput(std::string_view text);
    /// \brief Removes up to `count` bytes from the end of the input line. Used by
    /// DebugTools to undo a KeyDown-derived fallback character when the real
    /// TextInput event arrives in the same batch (IME-safe typing).
    void dropTrailingBytes(std::size_t count);
    /// \brief Deletes the last input character.
    void backspace();
    /// \brief Executes the current input, clears it and records history.
    [[nodiscard]] bool submitInput();
    /// \brief Recalls the previous history entry into the input line.
    void historyPrevious();
    /// \brief Steps forward through the history; past the end clears the input.
    void historyNext();

private:
    /// \brief Registers `help` and `clear`.
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
