#include "fa/debug/DebugTools.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include <fa/core/Log.hpp>
#include <fa/platform/PlatformEvent.hpp>
#include <fa/ui/Ui.hpp>

namespace fa::debug {

DebugTools::DebugTools() {
    m_logSink.setMinimumLevel(core::LogLevel::Info);
    registerBuiltins();
}

DebugTools::~DebugTools() {
    detachLogSink();
}

bool DebugTools::handleEvent(const platform::PlatformEvent& event) {
    if (event.type == platform::PlatformEventType::KeyDown) {
        switch (event.scancode) {
            case platform::Scancode::F3:
                toggleStats();
                return true;
            case platform::Scancode::F4:
                toggleLog();
                return true;
            case platform::Scancode::F5:
                togglePhysics();
                return true;
            case platform::Scancode::Grave:
                setConsoleOpen(!consoleOpen());
                return true;
            default:
                break;
        }

        if (!consoleOpen()) {
            return false;
        }

        switch (event.scancode) {
            case platform::Scancode::Escape:
                setConsoleOpen(false);
                return true;
            case platform::Scancode::Enter:
            case platform::Scancode::KeypadEnter:
                static_cast<void>(m_console.submitInput());
                return true;
            case platform::Scancode::Backspace:
                m_console.backspace();
                return true;
            case platform::Scancode::Up:
                m_console.historyPrevious();
                return true;
            case platform::Scancode::Down:
                m_console.historyNext();
                return true;
            default:
                return true;
        }
    }

    if (event.type == platform::PlatformEventType::KeyUp) {
        return consoleOpen();
    }

    if (event.type == platform::PlatformEventType::TextInput) {
        if (!consoleOpen()) {
            return false;
        }
        m_console.appendInput(event.text);
        return true;
    }

    return false;
}

void DebugTools::draw(ui::UiContext& ui, core::Rect viewport) const {
    m_overlay.draw(ui, viewport, m_stats, m_console, m_logSink, m_overlayState);
}

bool DebugTools::registerCommand(ConsoleCommand command) {
    return m_console.registerCommand(std::move(command));
}

void DebugTools::attachLogSink() {
    if (!m_logAttached) {
        core::addLogSink(&m_logSink);
        m_logAttached = true;
    }
}

void DebugTools::detachLogSink() {
    if (m_logAttached) {
        core::removeLogSink(&m_logSink);
        m_logAttached = false;
    }
}

void DebugTools::registerBuiltins() {
    static_cast<void>(registerCommand(ConsoleCommand{
        "stats", "print the current debug stats", [this](DebugConsole& console, std::span<const std::string>) {
            for (const std::string& line : formatStats(m_stats)) {
                console.print(line);
            }
        }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"stats.show", "show the stats panel", [this](DebugConsole&, std::span<const std::string>) {
                           setStatsVisible(true);
                       }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"stats.hide", "hide the stats panel", [this](DebugConsole&, std::span<const std::string>) {
                           setStatsVisible(false);
                       }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"stats.toggle", "toggle the stats panel", [this](DebugConsole&, std::span<const std::string>) {
                           toggleStats();
                       }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"log.show", "show the log panel", [this](DebugConsole&, std::span<const std::string>) {
                           setLogVisible(true);
                       }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"log.hide", "hide the log panel", [this](DebugConsole&, std::span<const std::string>) {
                           setLogVisible(false);
                       }}));
    static_cast<void>(registerCommand(ConsoleCommand{
        "log.toggle", "toggle the log panel", [this](DebugConsole&, std::span<const std::string>) { toggleLog(); }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"log.clear", "clear captured logs", [this](DebugConsole& console, std::span<const std::string>) {
                           m_logSink.clear();
                           console.print("log cleared");
                       }}));
    static_cast<void>(registerCommand(ConsoleCommand{
        "physics.show", "draw physics debug shapes", [this](DebugConsole&, std::span<const std::string>) {
            setPhysicsVisible(true);
        }}));
    static_cast<void>(registerCommand(ConsoleCommand{
        "physics.hide", "hide physics debug shapes", [this](DebugConsole&, std::span<const std::string>) {
            setPhysicsVisible(false);
        }}));
    static_cast<void>(registerCommand(ConsoleCommand{
        "physics.toggle", "toggle physics debug shapes", [this](DebugConsole&, std::span<const std::string>) {
            togglePhysics();
        }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"console.close", "close the console", [this](DebugConsole&, std::span<const std::string>) {
                           setConsoleOpen(false);
                       }}));
    static_cast<void>(registerCommand(
        ConsoleCommand{"cvar.list", "list runtime cvars", [this](DebugConsole& console, std::span<const std::string>) {
                           for (const CVarInfo& info : m_cvars.list()) {
                               console.print(info.name + " = " + info.value + "  (" + info.help + ")");
                           }
                       }}));
    static_cast<void>(registerCommand(ConsoleCommand{"cvar.get",
                                                     "print one cvar: cvar.get <name>",
                                                     [this](DebugConsole& console, std::span<const std::string> args) {
                                                         if (args.size() != 1) {
                                                             console.print("usage: cvar.get <name>");
                                                             return;
                                                         }
                                                         const CVarValue* value = m_cvars.get(args[0]);
                                                         if (value == nullptr) {
                                                             console.print("no such cvar: " + args[0]);
                                                             return;
                                                         }
                                                         console.print(args[0] + " = " + value->toString());
                                                     }}));
    static_cast<void>(registerCommand(ConsoleCommand{"cvar.set",
                                                     "set one cvar: cvar.set <name> <value>",
                                                     [this](DebugConsole& console, std::span<const std::string> args) {
                                                         if (args.size() != 2) {
                                                             console.print("usage: cvar.set <name> <value>");
                                                             return;
                                                         }
                                                         if (!m_cvars.set(args[0], args[1])) {
                                                             console.print("failed to set " + args[0]);
                                                             return;
                                                         }
                                                         console.print("set " + args[0] + " = " + args[1]);
                                                     }}));
}

}  // namespace fa::debug
