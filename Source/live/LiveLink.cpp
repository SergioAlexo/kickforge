#include "LiveLink.h"
#include "Reference.h"
#include "PluginProcessor.h"
#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <unistd.h>
#endif

namespace kf
{
namespace
{
int currentPid()
{
#if JUCE_WINDOWS
    return (int) _getpid();
#else
    return (int) getpid();
#endif
}
const char* const kStateFile = "state.kfpreset";
const char* const kApplyFile = "apply.kfpreset";
const char* const kRefFile = "reference.json";
const char* const kStatusFile = "status.json";
}

juce::File LiveLink::resolveFolder()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("KICKFORGE_LIVE_DIR", {}).trim();
    if (env.isNotEmpty())
        return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Kickforge").getChildFile ("Live");
}

LiveLink::LiveLink (KickforgeProcessor& p, Reference& r) : proc (p), ref (r), instanceId (juce::Uuid().toString())
{
    proc.apvts.state.addListener (this);
    for (auto* prm : proc.getParameters())
        if (auto* wp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            proc.apvts.addParameterListener (wp->getParameterID(), this);
}

LiveLink::~LiveLink()
{
    stopTimer();
    for (auto* prm : proc.getParameters())
        if (auto* wp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            proc.apvts.removeParameterListener (wp->getParameterID(), this);
    proc.apvts.state.removeListener (this);
}

void LiveLink::setEnabled (bool on)
{
    if (on)
    {
        superseded = false;
        claimPending.store (true);
        enabled.store (true);
        startTimer (tickMs);
    }
    else
    {
        enabled.store (false);
        claimPending.store (false);
        stopTimer();
    }
}

LiveLink::Status LiveLink::getStatus() const
{
    Status s;
    s.enabled = enabled.load();
    s.superseded = superseded;
    s.lastApplyOk = lastApplyOk;
    s.applied = applied;
    s.error = error;
    s.instance = instanceId;
    s.folder = folder.getFullPathName().isEmpty() ? resolveFolder() : folder;
    return s;
}

bool LiveLink::writeAtomically (const juce::File& target, const juce::String& text)
{
    auto tmp = target.getSiblingFile (target.getFileName() + ".tmp");
    if (! tmp.replaceWithText (text, false, false, "\n"))
        return false;
    if (! tmp.moveFileTo (target))
    {
        tmp.deleteFile();
        return false;
    }
    return true;
}

void LiveLink::writeState()
{
    auto st = proc.apvts.copyState();
    st.removeChild (st.getChildWithName ("click"), nullptr);
    st.appendChild (proc.getClickLayer().saveState(), nullptr);
    if (auto xml = st.createXml())
        writeAtomically (folder.getChildFile (kStateFile), xml->toString());
    lastClickVer = proc.getClickLayer().getStateVersion();
    pendingWrite = false;
    dirty.store (false);
    ++writeCount;
    if (ref.getInfo().note < 0 && ref.hasAudio()) writeReference(); // default note follows sub_root
}

void LiveLink::writeReference()
{
    lastRefVer = ref.getVersion();
    const auto f = folder.getChildFile (kRefFile);
    const auto i = ref.getInfo();
    if (! i.loaded)
    {
        f.deleteFile();
        return;
    }
    int note = i.note;
    if (note < 0)
        if (auto* root = proc.apvts.getRawParameterValue ("sub_root"))
            note = juce::jlimit (0, 127, (int) std::lround (root->load()));
    auto* o = new juce::DynamicObject();
    o->setProperty ("path", i.path);
    o->setProperty ("regionStart", i.regionStart);
    o->setProperty ("regionEnd", i.regionEnd);
    o->setProperty ("sampleRate", i.sampleRate);
    o->setProperty ("note", note);
    writeAtomically (f, juce::JSON::toString (juce::var (o)));
}

void LiveLink::writeStatus()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("instance", instanceId);
    o->setProperty ("pid", currentPid());
    o->setProperty ("applied", applied);
    o->setProperty ("lastApplyOk", lastApplyOk);
    o->setProperty ("error", error);
    o->setProperty ("time", juce::Time::getCurrentTime().toISO8601 (true));
    writeAtomically (folder.getChildFile (kStatusFile), juce::JSON::toString (juce::var (o)));
}

void LiveLink::claim()
{
    folder = resolveFolder();
    folder.createDirectory();
    // Stay monotonic across instances: the CLI waits for `applied` to increase.
    applied = 0;
    const auto sf = folder.getChildFile (kStatusFile);
    if (sf.existsAsFile())
    {
        auto v = juce::JSON::parse (sf.loadFileAsString());
        if (auto* o = v.getDynamicObject())
            applied = std::max (0, (int) o->getProperty ("applied"));
    }
    lastApplyOk = true;
    error = {};
    superseded = false;
    writeStatus();
    writeState();
    writeReference();
    claimPending.store (false);
}

void LiveLink::checkSuperseded()
{
    const auto sf = folder.getChildFile (kStatusFile);
    if (! sf.existsAsFile()) { writeStatus(); return; }
    auto v = juce::JSON::parse (sf.loadFileAsString());
    auto* o = v.getDynamicObject();
    if (o == nullptr) return; // being replaced
    const auto id = o->getProperty ("instance").toString();
    if (id.isNotEmpty() && id != instanceId)
    {
        superseded = true;
        enabled.store (false);
        stopTimer();
    }
}

void LiveLink::pollApply()
{
    const auto af = folder.getChildFile (kApplyFile);
    if (! af.existsAsFile()) return;

    juce::XmlDocument doc (af);
    auto xml = doc.getDocumentElement();
    juce::String err;
    if (xml == nullptr)
        err = "malformed apply file: " + doc.getLastParseError();
    else if (! xml->hasTagName (proc.apvts.state.getType()))
        err = "malformed apply file: unexpected root element <" + xml->getTagName() + ">";
    af.deleteFile();

    if (err.isEmpty())
    {
        for (auto* a : { "kfName", "kfCategory", "kfScope", "kfVersion" })
            xml->removeAttribute (a);
        if (auto* l = xml->getChildByName ("kfLive")) // never part of the applied state
            xml->removeChildElement (l, true);
        // Same path as PresetManager::applyXml (full scope).
        proc.apvts.replaceState (juce::ValueTree::fromXml (*xml));
        proc.getClickLayer().loadState (proc.apvts.state.getChildWithName ("click"));
        proc.getSubCurves().loadFromState();
    }

    lastApplyOk = err.isEmpty();
    error = err;
    ++applied;
    writeState();   // immediate, also after a failed apply (the file stays current)
    writeStatus();
}

void LiveLink::tick (juce::uint32 now)
{
    if (! enabled.load()) return;
    if (claimPending.load())
    {
        claim();
        lastPoll = now;
        lastRefVer = ref.getVersion();
        return;
    }

    if (dirty.exchange (false) || proc.getClickLayer().getStateVersion() != lastClickVer)
    {
        if (! pendingWrite) { pendingWrite = true; firstDirty = now; }
        lastChange = now;
        lastClickVer = proc.getClickLayer().getStateVersion();
    }
    if (pendingWrite && (now - lastChange >= (juce::uint32) quietMs || now - firstDirty >= (juce::uint32) maxWaitMs))
        writeState();

    if (ref.getVersion() != lastRefVer)
        writeReference();

    if (now - lastPoll >= (juce::uint32) pollMs)
    {
        lastPoll = now;
        checkSuperseded();
        if (enabled.load())
            pollApply();
    }
}
} // namespace kf
