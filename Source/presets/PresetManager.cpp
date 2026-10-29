#include "PresetManager.h"
#include "PluginProcessor.h"
#include <algorithm>

namespace kf
{
namespace
{
const char* scopePrefix (PresetScope s)
{
    switch (s)
    {
        case PresetScope::sub:   return "sub_";
        case PresetScope::click: return "click_";
        case PresetScope::fx:    return "fx_";
        default:                 return "";
    }
}
const char* scopeChild (PresetScope s)
{
    switch (s)
    {
        case PresetScope::sub:   return "CURVES";
        case PresetScope::click: return "click";
        default:                 return nullptr;
    }
}
PresetScope scopeFromString (const juce::String& s)
{
    for (auto sc : { PresetScope::sub, PresetScope::click, PresetScope::fx })
        if (s == presetScopeName (sc)) return sc;
    return PresetScope::full;
}
juce::String toText (const juce::XmlElement& x)
{
    return x.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}
void stripMeta (juce::XmlElement& x)
{
    for (auto* a : { "kfName", "kfCategory", "kfScope", "kfVersion" })
        x.removeAttribute (a);
}
}

const char* presetScopeName (PresetScope s) noexcept
{
    switch (s)
    {
        case PresetScope::sub:   return "sub";
        case PresetScope::click: return "click";
        case PresetScope::fx:    return "fx";
        default:                 return "full";
    }
}

juce::File PresetManager::defaultUserDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Kickforge").getChildFile ("Presets");
}

juce::String PresetManager::sanitize (const juce::String& s)
{
    return juce::File::createLegalFileName (s.trim());
}

juce::StringArray PresetManager::factoryCategories()
{
    juce::StringArray c;
    for (auto& f : factoryPresets()) c.addIfNotAlreadyThere (f.category);
    return c;
}

PresetManager::PresetManager (KickforgeProcessor& p, juce::File dir) : proc (p), userDir (std::move (dir))
{
    loadFavourites();
    rescan();
    markClean ("Init", -1);
}

std::unique_ptr<juce::XmlElement> PresetManager::stateXml() const
{
    auto st = proc.apvts.copyState();
    st.removeChild (st.getChildWithName ("click"), nullptr);
    st.appendChild (proc.getClickLayer().saveState(), nullptr);
    return st.createXml();
}

juce::MemoryBlock PresetManager::captureState() const
{
    juce::MemoryBlock mb;
    proc.getStateInformation (mb);
    return mb;
}

juce::String PresetManager::captureXmlString() const
{
    auto x = stateXml();
    return x != nullptr ? toText (*x) : juce::String();
}

void PresetManager::markClean (const juce::String& name, int index)
{
    currentName = name;
    currentIndex = index;
    cleanSnapshot = captureXmlString();
    sendChangeMessage();
}

bool PresetManager::isDirty() const { return captureXmlString() != cleanSnapshot; }

// ------------------------------------------------------------------ scan
void PresetManager::rescan()
{
    const juce::String keepId = currentIndex >= 0 && currentIndex < (int) presets.size()
                                    ? presets[(size_t) currentIndex].id : juce::String();
    presets.clear();
    for (auto& f : factoryPresets())
    {
        PresetInfo i;
        i.name = f.name;
        i.category = f.category;
        i.factory = true;
        i.id = "F:" + f.category + "/" + f.name;
        presets.push_back (i);
    }
    std::stable_sort (presets.begin(), presets.end(), [] (auto& a, auto& b) {
        return a.category == b.category ? false : a.category.compareIgnoreCase (b.category) < 0;
    });

    std::vector<PresetInfo> user;
    if (userDir.isDirectory())
        for (auto& f : userDir.findChildFiles (juce::File::findFiles, true, "*" + juce::String (extension())))
        {
            PresetInfo i;
            i.file = f;
            i.name = f.getFileNameWithoutExtension();
            i.category = f.getParentDirectory() == userDir ? juce::String ("User")
                                                            : f.getParentDirectory().getFileName();
            if (auto x = juce::XmlDocument::parse (f))
            {
                i.name = x->getStringAttribute ("kfName", i.name);
                i.category = x->getStringAttribute ("kfCategory", i.category);
                i.scope = scopeFromString (x->getStringAttribute ("kfScope"));
            }
            i.id = "U:" + f.getRelativePathFrom (userDir);
            user.push_back (i);
        }
    std::sort (user.begin(), user.end(), [] (auto& a, auto& b) {
        const int c = a.category.compareIgnoreCase (b.category);
        return c != 0 ? c < 0 : a.name.compareIgnoreCase (b.name) < 0;
    });
    presets.insert (presets.end(), user.begin(), user.end());
    for (auto& p : presets) p.favourite = favouriteIds.contains (p.id);

    currentIndex = -1;
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].id == keepId) currentIndex = (int) i;
    sendChangeMessage();
}

juce::StringArray PresetManager::categories() const
{
    juce::StringArray c;
    for (auto& p : presets) c.addIfNotAlreadyThere (p.category);
    return c;
}

std::vector<int> PresetManager::filter (const juce::String& text, const juce::String& category, bool fav) const
{
    std::vector<int> r;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        auto& p = presets[i];
        if (category.isNotEmpty() && p.category != category) continue;
        if (fav && ! p.favourite) continue;
        if (text.isNotEmpty() && ! p.name.containsIgnoreCase (text) && ! p.category.containsIgnoreCase (text))
            continue;
        r.push_back ((int) i);
    }
    return r;
}

int PresetManager::findByName (const juce::String& name) const
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name.equalsIgnoreCase (name)) return (int) i;
    return -1;
}

// ------------------------------------------------------------------ loading
std::unique_ptr<juce::XmlElement> PresetManager::xmlFor (const PresetInfo& p) const
{
    if (p.factory)
    {
        for (auto& f : factoryPresets())
            if (f.name == p.name && f.category == p.category)
                return std::make_unique<juce::XmlElement> (*f.state);
        return nullptr;
    }
    return juce::XmlDocument::parse (p.file);
}

bool PresetManager::applyXml (const juce::XmlElement& src, PresetScope scope)
{
    if (! src.hasTagName (proc.apvts.state.getType())) return false;

    std::unique_ptr<juce::XmlElement> merged;
    if (scope == PresetScope::full)
    {
        merged = std::make_unique<juce::XmlElement> (src);
    }
    else
    {
        merged = stateXml();
        if (merged == nullptr) return false;
        const juce::String pre = scopePrefix (scope);
        const char* child = scopeChild (scope);
        for (auto* c = merged->getFirstChildElement(); c != nullptr;)
        {
            auto* nxt = c->getNextElement();
            if ((c->hasTagName ("PARAM") && c->getStringAttribute ("id").startsWith (pre))
                || (child != nullptr && c->hasTagName (child)))
                merged->removeChildElement (c, true);
            c = nxt;
        }
        for (auto* c = src.getFirstChildElement(); c != nullptr; c = c->getNextElement())
            if ((c->hasTagName ("PARAM") && c->getStringAttribute ("id").startsWith (pre))
                || (child != nullptr && c->hasTagName (child)))
                merged->addChildElement (new juce::XmlElement (*c));
    }
    stripMeta (*merged);

    proc.apvts.replaceState (juce::ValueTree::fromXml (*merged));
    proc.getClickLayer().loadState (proc.apvts.state.getChildWithName ("click"));
    proc.getSubCurves().loadFromState();
    return true;
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= (int) presets.size()) return false;
    const auto& p = presets[(size_t) index];
    auto x = xmlFor (p);
    if (x == nullptr || ! applyXml (*x, p.scope)) return false;
    markClean (p.name, index);
    return true;
}

bool PresetManager::loadInit()
{
    // Default state = a pristine processor's state.
    if (initSnapshotXml.isEmpty())
    {
        KickforgeProcessor fresh;
        auto st = fresh.apvts.copyState();
        st.removeChild (st.getChildWithName ("click"), nullptr);
        st.appendChild (fresh.getClickLayer().saveState(), nullptr);
        if (auto x = st.createXml()) initSnapshotXml = toText (*x);
    }
    auto x = juce::XmlDocument::parse (initSnapshotXml);
    if (x == nullptr || ! applyXml (*x, PresetScope::full)) return false;
    markClean ("Init", -1);
    return true;
}

bool PresetManager::loadFile (const juce::File& f)
{
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr) return false;
    const auto scope = scopeFromString (x->getStringAttribute ("kfScope"));
    const auto name = x->getStringAttribute ("kfName", f.getFileNameWithoutExtension());
    if (! applyXml (*x, scope)) return false;
    markClean (name, -1);
    return true;
}

bool PresetManager::step (int dir, const std::vector<int>* subset)
{
    std::vector<int> all;
    if (subset == nullptr)
    {
        for (size_t i = 0; i < presets.size(); ++i) all.push_back ((int) i);
        subset = &all;
    }
    if (subset->empty()) return false;
    int pos = -1;
    for (size_t i = 0; i < subset->size(); ++i)
        if ((*subset)[i] == currentIndex) pos = (int) i;
    const int n = (int) subset->size();
    pos = pos < 0 ? (dir > 0 ? 0 : n - 1) : (pos + dir + n) % n;
    return load ((*subset)[(size_t) pos]);
}

// ------------------------------------------------------------------ saving
bool PresetManager::save (const juce::String& nameIn, const juce::String& categoryIn, PresetScope scope)
{
    const auto name = nameIn.trim();
    const auto category = categoryIn.trim().isEmpty() ? juce::String ("User") : categoryIn.trim();
    if (name.isEmpty()) return false;

    auto x = stateXml();
    if (x == nullptr) return false;
    if (scope != PresetScope::full)
    {
        const juce::String pre = scopePrefix (scope);
        const char* child = scopeChild (scope);
        for (auto* c = x->getFirstChildElement(); c != nullptr;)
        {
            auto* nxt = c->getNextElement();
            const bool keep = (c->hasTagName ("PARAM") && c->getStringAttribute ("id").startsWith (pre))
                              || (child != nullptr && c->hasTagName (child));
            if (! keep) x->removeChildElement (c, true);
            c = nxt;
        }
    }
    x->setAttribute ("kfName", name);
    x->setAttribute ("kfCategory", category);
    x->setAttribute ("kfScope", presetScopeName (scope));
    x->setAttribute ("kfVersion", 1);

    auto dir = userDir.getChildFile (sanitize (category));
    dir.createDirectory();
    auto file = dir.getChildFile (sanitize (name) + extension());
    if (! file.replaceWithText (x->toString()))
        return false;

    rescan();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].file == file) { markClean (name, (int) i); return true; }
    markClean (name, -1);
    return true;
}

bool PresetManager::saveCurrent()
{
    if (currentIndex < 0 || currentIndex >= (int) presets.size()) return false;
    const auto p = presets[(size_t) currentIndex];
    if (p.factory) return false;
    return save (p.name, p.category, p.scope);
}

bool PresetManager::rename (int index, const juce::String& newName)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].factory) return false;
    const auto nn = newName.trim();
    if (nn.isEmpty()) return false;
    const auto p = presets[(size_t) index];
    auto x = juce::XmlDocument::parse (p.file);
    if (x == nullptr) return false;
    x->setAttribute ("kfName", nn);
    auto dest = p.file.getParentDirectory().getChildFile (sanitize (nn) + extension());
    if (dest != p.file && dest.exists()) return false;
    if (! dest.replaceWithText (x->toString())) return false;
    if (dest != p.file) p.file.deleteFile();
    const bool wasCurrent = index == currentIndex;
    if (favouriteIds.contains (p.id))
    {
        favouriteIds.removeString (p.id);
        favouriteIds.add ("U:" + dest.getRelativePathFrom (userDir));
        saveFavourites();
    }
    rescan();
    if (wasCurrent)
    {
        currentName = nn;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].file == dest) currentIndex = (int) i;
    }
    return true;
}

bool PresetManager::remove (int index)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].factory) return false;
    const auto p = presets[(size_t) index];
    if (! p.file.moveToTrash() && ! p.file.deleteFile()) return false;
    if (favouriteIds.contains (p.id)) { favouriteIds.removeString (p.id); saveFavourites(); }
    if (p.file.getParentDirectory() != userDir
        && p.file.getParentDirectory().getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        p.file.getParentDirectory().deleteFile();
    const bool wasCurrent = index == currentIndex;
    rescan();
    if (wasCurrent) currentIndex = -1;
    return true;
}

bool PresetManager::importFile (const juce::File& src, const juce::String& category)
{
    auto x = juce::XmlDocument::parse (src);
    if (x == nullptr || ! x->hasTagName (proc.apvts.state.getType())) return false;
    const auto name = x->getStringAttribute ("kfName", src.getFileNameWithoutExtension());
    auto dir = userDir.getChildFile (sanitize (x->getStringAttribute ("kfCategory", category)));
    dir.createDirectory();
    auto dest = dir.getChildFile (sanitize (name) + extension());
    for (int n = 2; dest.exists(); ++n)
        dest = dir.getChildFile (sanitize (name) + " " + juce::String (n) + extension());
    x->setAttribute ("kfName", name);
    x->setAttribute ("kfCategory", dir.getFileName());
    if (! dest.replaceWithText (x->toString())) return false;
    rescan();
    return true;
}

bool PresetManager::exportFile (int index, const juce::File& dest) const
{
    if (index < 0 || index >= (int) presets.size()) return false;
    const auto& p = presets[(size_t) index];
    auto x = xmlFor (p);
    if (x == nullptr) return false;
    x->setAttribute ("kfName", p.name);
    x->setAttribute ("kfCategory", p.category);
    x->setAttribute ("kfScope", presetScopeName (p.scope));
    x->setAttribute ("kfVersion", 1);
    return dest.replaceWithText (x->toString());
}

// ------------------------------------------------------------------ favourites
void PresetManager::loadFavourites()
{
    favouriteIds.clear();
    favouriteIds.addLines (userDir.getChildFile ("favourites.txt").loadFileAsString());
    favouriteIds.removeEmptyStrings();
}

void PresetManager::saveFavourites() const
{
    userDir.createDirectory();
    userDir.getChildFile ("favourites.txt").replaceWithText (favouriteIds.joinIntoString ("\n"));
}

void PresetManager::setFavourite (int index, bool fav)
{
    if (index < 0 || index >= (int) presets.size()) return;
    auto& p = presets[(size_t) index];
    p.favourite = fav;
    if (fav) favouriteIds.addIfNotAlreadyThere (p.id);
    else favouriteIds.removeString (p.id);
    saveFavourites();
    sendChangeMessage();
}

bool PresetManager::isFavourite (int index) const
{
    return index >= 0 && index < (int) presets.size() && presets[(size_t) index].favourite;
}
}
