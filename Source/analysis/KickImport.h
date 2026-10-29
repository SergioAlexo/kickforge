#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include "KickAnalysis.h"

class KickforgeProcessor;

namespace kf
{
struct ImportOptions
{
    int regionStart = -1, regionEnd = -1;  // samples of the source file; -1 = strongest detected hit
    bool harmonics = true;
    int clickLayer = 0;                    // sample layer that receives the top end (0-based)
    juce::File importsDir;                 // default: %APPDATA%\Kickforge\Imports
};

struct ImportSummary
{
    bool ok = false;
    juce::String message;
    analysis::Result result;
    juce::File topEndFile;
    int numHits = 0, hitIndex = -1;
};

/** %APPDATA%\Kickforge\Imports (created on demand). */
juce::File defaultImportsDir();

/** Decodes any supported audio file to mono (channel mean). */
bool loadAudioMono (const juce::File&, std::vector<float>& mono, double& sampleRate, juce::String& error);

/** Background half of an import: decode, detect hits, analyse, write the top-end WAV. No processor access. */
ImportSummary analyseFile (const juce::File& source, const ImportOptions&, double a4 = 440.0);

/** Message-thread half: applies sub params/curves/harmonics and loads the top-end sample. */
void applyImport (KickforgeProcessor&, const ImportSummary&, const ImportOptions&);

/** C-10: swap the top-end sample layer only. The sub layer (params + curves) is not touched. */
void replaceTopEndSample (KickforgeProcessor&, const juce::File& sample, int clickLayer = 0);

/** Runs analyseFile on a worker thread and applyImport on the message thread. */
class KickImporter
{
public:
    explicit KickImporter (KickforgeProcessor&);
    ~KickImporter();
    bool isBusy() const noexcept { return busy->load(); }
    /** Message thread. done() is called on the message thread (not if the importer is destroyed first). */
    void start (const juce::File& source, ImportOptions, std::function<void (const ImportSummary&)> done);

private:
    struct Worker;
    KickforgeProcessor& proc;
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };
    std::shared_ptr<std::atomic<bool>> busy { std::make_shared<std::atomic<bool>> (false) };
    std::unique_ptr<juce::Thread> thread;
};
}
