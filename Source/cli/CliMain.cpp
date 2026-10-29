#include <juce_events/juce_events.h>
#include <cstdio>
#include "CliCore.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back (argv[i]);
    int code = 0;
    const auto out = kf::cli::run (args, code);
    std::fwrite (out.data(), 1, out.size(), stdout);
    std::fputc ('\n', stdout);
    std::fflush (stdout);
    return code;
}
