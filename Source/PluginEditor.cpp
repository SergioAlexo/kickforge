#include "PluginEditor.h"

KickforgeEditor::KickforgeEditor (KickforgeProcessor& p)
    : AudioProcessorEditor (&p), generic (p)
{
    addAndMakeVisible (generic);
    setSize (500, 300);
}

void KickforgeEditor::resized() { generic.setBounds (getLocalBounds()); }
