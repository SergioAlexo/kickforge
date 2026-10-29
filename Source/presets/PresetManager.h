#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

class KickforgeProcessor;

namespace kf
{
/** What part of the state a preset covers (P-02). Full presets replace everything; partial
    presets only touch their own part when loaded. */
enum class PresetScope { full, sub, click, fx };
const char* presetScopeName (PresetScope) noexcept;

struct PresetInfo
{
    juce::String name, category;
    PresetScope scope = PresetScope::full;
    bool factory = false;
    juce::File file;     // user presets only
    juce::String id;     // stable key: "F:<cat>/<name>" or "U:<relative path>"
    bool favourite = false;
};

/** Preset save/load/browse (P-01..P-03). All calls on the message thread.
    State format = the plugin state XML (apvts + CURVES + click incl. sample paths) with extra
    root attributes kfName / kfCategory / kfScope / kfVersion; file extension .kfpreset. */
class PresetManager : public juce::ChangeBroadcaster
{
public:
    explicit PresetManager (KickforgeProcessor&, juce::File userDir = defaultUserDir());

    static juce::File defaultUserDir();
    static const char* extension() { return ".kfpreset"; }
    static juce::StringArray factoryCategories();

    juce::File getUserDir() const { return userDir; }
    void rescan();
    const std::vector<PresetInfo>& all() const { return presets; }
    juce::StringArray categories() const;

    /** Search text matches name or category (case-insensitive). category empty = any. */
    std::vector<int> filter (const juce::String& text, const juce::String& category = {},
                             bool favouritesOnly = false) const;

    // ---- loading ----
    bool load (int index);
    bool loadInit();
    bool loadFile (const juce::File&);
    /** Step through the (optionally filtered) list; wraps around. */
    bool next (const std::vector<int>* subset = nullptr) { return step (+1, subset); }
    bool previous (const std::vector<int>* subset = nullptr) { return step (-1, subset); }

    // ---- state ----
    juce::String getCurrentName() const { return currentName; }
    int getCurrentIndex() const { return currentIndex; }
    /** True when the live state differs from what was last loaded/saved. */
    bool isDirty() const;

    // ---- saving (user presets) ----
    /** Saves the current state to <userDir>/<category>/<name>.kfpreset. Overwrites. */
    bool save (const juce::String& name, const juce::String& category = "User", PresetScope = PresetScope::full);
    bool saveCurrent(); // overwrite the loaded user preset; false if the current one is not a user preset
    bool rename (int index, const juce::String& newName);
    bool remove (int index);
    bool importFile (const juce::File& src, const juce::String& category = "Imported");
    bool exportFile (int index, const juce::File& dest) const;

    // ---- favourites ----
    void setFavourite (int index, bool fav);
    bool isFavourite (int index) const;

    // ---- raw state helpers (also used by tests) ----
    juce::MemoryBlock captureState() const;
    juce::String captureXmlString() const;
    int findByName (const juce::String& name) const;

private:
    bool step (int dir, const std::vector<int>*);
    bool applyXml (const juce::XmlElement& xml, PresetScope scope);
    std::unique_ptr<juce::XmlElement> stateXml() const;
    std::unique_ptr<juce::XmlElement> xmlFor (const PresetInfo&) const;
    void markClean (const juce::String& name, int index);
    void loadFavourites();
    void saveFavourites() const;
    static juce::String sanitize (const juce::String&);

    KickforgeProcessor& proc;
    juce::File userDir;
    std::vector<PresetInfo> presets;
    juce::StringArray favouriteIds;
    juce::String currentName = "Init", cleanSnapshot;
    int currentIndex = -1;
    juce::String initSnapshotXml; // default state, captured at construction
};

// ---- factory presets (FactoryPresets.cpp) ----
struct FactoryPreset
{
    juce::String name, category;
    std::unique_ptr<juce::XmlElement> state; // full plugin-state XML
};
/** Built lazily with a scratch processor; cached for the process lifetime. */
const std::vector<FactoryPreset>& factoryPresets();
}
