#include "Params.h"
#include "core/GlobParams.h"
#include "synth/SubParams.h"
#include "click/ClickParams.h"
#include "fx/FxParams.h"
#include "trigger/Trigger.h"

namespace kf
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    addGlobParams (layout);
    addSubParams (layout);
    addClickParams (layout);
    addFxParams (layout);
    addTriggerParams (layout);
    return layout;
}
}
