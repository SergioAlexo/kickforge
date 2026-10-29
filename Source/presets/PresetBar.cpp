#include "PresetBar.h"

namespace kf
{
PresetBar::PresetBar (KickforgeProcessor& p)
    : proc (p), manager (p), dragSource (p, [this] { return manager.getCurrentName(); })
{
    for (auto* b : { &prevButton, &nextButton, &saveButton, &menuButton, &nameButton })
        addAndMakeVisible (*b);
    addAndMakeVisible (dragSource);

    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    saveButton.setTooltip ("Save (overwrites a user preset, otherwise asks for a name)");
    menuButton.setTooltip ("Preset menu");
    nameButton.setTooltip ("Browse presets");

    prevButton.onClick = [this] { manager.previous(); };
    nextButton.onClick = [this] { manager.next(); };
    saveButton.onClick = [this] { if (! manager.saveCurrent()) saveAs (PresetScope::full); };
    menuButton.onClick = [this] { showMenu(); };
    nameButton.onClick = [this] { showMenu(); };

    manager.addChangeListener (this);
    startTimerHz (4);
    updateName();
}

PresetBar::~PresetBar() { manager.removeChangeListener (this); }

void PresetBar::updateName()
{
    const auto t = manager.getCurrentName() + (manager.isDirty() ? " *" : "");
    if (nameButton.getButtonText() != t) nameButton.setButtonText (t);
}

void PresetBar::paint (juce::Graphics&) {}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    const int h = r.getHeight();
    dragSource.setBounds (r.removeFromRight (juce::jmax (36, h * 2)));
    r.removeFromRight (4);
    menuButton.setBounds (r.removeFromRight (h));
    r.removeFromRight (2);
    saveButton.setBounds (r.removeFromRight (juce::jmax (44, h * 2)));
    r.removeFromRight (4);
    prevButton.setBounds (r.removeFromLeft (h));
    r.removeFromLeft (2);
    nextButton.setBounds (r.removeFromRight (h));
    r.removeFromRight (2);
    nameButton.setBounds (r);
}

void PresetBar::promptName (const juce::String& title, const juce::String& initial,
                            std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> sw (w);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([sw, done] (int result) {
        if (result == 1 && sw != nullptr)
            done (sw->getTextEditorContents ("name"));
    }), true);
}

void PresetBar::saveAs (PresetScope scope)
{
    const int cur = manager.getCurrentIndex();
    const auto cat = cur >= 0 ? manager.all()[(size_t) cur].category : juce::String ("User");
    const auto suffix = scope == PresetScope::full ? juce::String() : juce::String (" (") + presetScopeName (scope) + ")";
    juce::Component::SafePointer<PresetBar> self (this);
    promptName ("Save preset as (Category/Name or just Name)", manager.getCurrentName() + suffix,
                [self, scope, cat] (juce::String text) {
        if (self == nullptr) return;
        juce::String name = text.trim(), category = cat == "Init" || cat.isEmpty() ? juce::String ("User") : cat;
        if (name.contains ("/"))
        {
            category = name.upToFirstOccurrenceOf ("/", false, false);
            name = name.fromFirstOccurrenceOf ("/", false, false);
        }
        if (self->manager.getCurrentIndex() >= 0 && self->manager.all()[(size_t) self->manager.getCurrentIndex()].factory
            && category == cat && cat != "User" && scope == PresetScope::full)
            category = "User"; // never mix user presets into factory category names
        self->manager.save (name, category, scope);
    });
}

void PresetBar::showSearch()
{
    juce::Component::SafePointer<PresetBar> self (this);
    promptName ("Search presets", {}, [self] (juce::String text) {
        if (self == nullptr) return;
        auto hits = self->manager.filter (text);
        juce::PopupMenu m;
        if (hits.empty()) m.addItem (-1, "No matches", false);
        for (int i : hits)
            m.addItem (1000 + i, self->manager.all()[(size_t) i].category + " / " + self->manager.all()[(size_t) i].name);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (self->nameButton),
                         [self] (int r) { if (self != nullptr && r >= 1000) self->manager.load (r - 1000); });
    });
}

void PresetBar::importKick()
{
    if (importer == nullptr) importer = std::make_unique<KickImporter> (proc);
    if (importer->isBusy()) return;
    chooser = std::make_unique<juce::FileChooser> (
        "Import a kick sample or loop (strongest hit is analysed)",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<PresetBar> self (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self] (const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (self == nullptr || ! f.existsAsFile()) return;
        self->importer->start (f, ImportOptions {}, [self] (const ImportSummary& s) {
            if (self == nullptr) return;
            juce::AlertWindow::showMessageBoxAsync (s.ok ? juce::MessageBoxIconType::InfoIcon
                                                         : juce::MessageBoxIconType::WarningIcon,
                                                    "Import kick", s.message);
        });
    });
}

void PresetBar::exportWav()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Export kick as WAV",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
            .getChildFile (juce::File::createLegalFileName (manager.getCurrentName()) + ".wav"),
        "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        RenderOptions o;
        o.sampleRate = 48000.0;
        writeWav24 (f.withFileExtension (".wav"), renderKick (proc, o), o.sampleRate);
    });
}

void PresetBar::showMenu()
{
    juce::PopupMenu m;
    const int cur = manager.getCurrentIndex();
    const bool curUser = cur >= 0 && ! manager.all()[(size_t) cur].factory;

    m.addItem (1, "Init preset");
    m.addSeparator();
    m.addItem (2, "Save", true);
    m.addItem (3, "Save as...");
    juce::PopupMenu part;
    part.addItem (4, "Sub only...");
    part.addItem (5, "Click / samples only...");
    part.addItem (6, "FX only...");
    m.addSubMenu ("Save part", part);
    m.addItem (7, "Rename...", curUser);
    m.addItem (8, "Delete", curUser);
    m.addSeparator();
    m.addItem (9, "Favourite", cur >= 0, manager.isFavourite (cur));
    m.addItem (11, "Show favourites only", true, showFavouritesOnly);
    m.addItem (10, "Search...");
    m.addSeparator();

    for (const auto& cat : manager.categories())
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < manager.all().size(); ++i)
        {
            const auto& p = manager.all()[i];
            if (p.category != cat) continue;
            if (showFavouritesOnly && ! p.favourite) continue;
            juce::String label = p.name;
            if (p.favourite) label = juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 ")) + label;
            if (p.scope != PresetScope::full) label << " [" << presetScopeName (p.scope) << "]";
            sub.addItem (1000 + (int) i, label, true, (int) i == cur);
        }
        if (sub.getNumItems() > 0) m.addSubMenu (cat, sub, true, nullptr, false, 0);
    }

    m.addSeparator();
    m.addItem (12, "Import preset file...");
    m.addItem (13, "Export preset file...", cur >= 0);
    m.addItem (14, "Export kick as WAV...");
    m.addItem (17, "Import kick...");
    m.addItem (15, "Open user preset folder");
    m.addItem (16, "Rescan presets");

    juce::Component::SafePointer<PresetBar> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton), [self, cur] (int r) {
        if (self == nullptr || r == 0) return;
        auto& pm = self->manager;
        if (r >= 1000) { pm.load (r - 1000); return; }
        switch (r)
        {
            case 1:  pm.loadInit(); break;
            case 2:  if (! pm.saveCurrent()) self->saveAs (PresetScope::full); break;
            case 3:  self->saveAs (PresetScope::full); break;
            case 4:  self->saveAs (PresetScope::sub); break;
            case 5:  self->saveAs (PresetScope::click); break;
            case 6:  self->saveAs (PresetScope::fx); break;
            case 7:
                self->promptName ("Rename preset", pm.getCurrentName(), [self, cur] (juce::String n) {
                    if (self != nullptr) self->manager.rename (cur, n);
                });
                break;
            case 8:
                juce::AlertWindow::showAsync (
                    juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                        .withTitle ("Delete preset").withMessage ("Move \"" + pm.getCurrentName() + "\" to the trash?")
                        .withButton ("Delete").withButton ("Cancel"),
                    [self, cur] (int res) { if (self != nullptr && res == 1) self->manager.remove (cur); });
                break;
            case 9:  pm.setFavourite (cur, ! pm.isFavourite (cur)); break;
            case 10: self->showSearch(); break;
            case 11: self->showFavouritesOnly = ! self->showFavouritesOnly; break;
            case 12:
                self->chooser = std::make_unique<juce::FileChooser> ("Import preset", juce::File(), "*.kfpreset");
                self->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                            [self] (const juce::FileChooser& fc) {
                    if (self != nullptr && fc.getResult().existsAsFile()) self->manager.importFile (fc.getResult());
                });
                break;
            case 13:
                self->chooser = std::make_unique<juce::FileChooser> (
                    "Export preset",
                    juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                        .getChildFile (juce::File::createLegalFileName (pm.getCurrentName()) + ".kfpreset"),
                    "*.kfpreset");
                self->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                                | juce::FileBrowserComponent::warnAboutOverwriting,
                                            [self, cur] (const juce::FileChooser& fc) {
                    if (self != nullptr && fc.getResult() != juce::File())
                        self->manager.exportFile (cur, fc.getResult().withFileExtension (".kfpreset"));
                });
                break;
            case 17: self->importKick(); break;
            case 14: self->exportWav(); break;
            case 15: pm.getUserDir().createDirectory(); pm.getUserDir().revealToUser(); break;
            case 16: pm.rescan(); break;
            default: break;
        }
    });
}
}
