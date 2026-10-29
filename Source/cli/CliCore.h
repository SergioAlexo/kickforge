#pragma once
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

/** kickforge-cli: everything except main(), so the tests can drive it in-process. */
namespace kf::cli
{
/** %APPDATA%\Kickforge\Live, or env KICKFORGE_LIVE_DIR. */
juce::File liveDir();

/** Runs one command line (args WITHOUT the program name). Returns the text for stdout (JSON, or plain
    text for `help`/`--version`); exitCode is 0 on success, non-zero on error (the text is then {"error":...}). */
std::string run (const std::vector<std::string>& args, int& exitCode);

/** Atomic write: <dest>.tmp then rename over dest. */
bool writeFileAtomic (const juce::File& dest, const juce::String& text);

constexpr const char* kVersion = "kickforge-cli 1.0 (AI-LINK contract v1)";
} // namespace kf::cli
