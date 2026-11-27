#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

//==============================================================================
/** Parametro di un posto integrato (AudioPluginInstance accetta solo HostedAudioProcessorParameter).
    get() è nel valore reale (dB, posizione della manopola 0-10, oppure 0/1 per gli interruttori). */
class BuiltInParameter final : public juce::HostedAudioProcessorParameter
{
public:
    BuiltInParameter (const juce::String& parameterId, const juce::String& parameterName,
                      juce::NormalisableRange<float> r, float defaultRealValue, const juce::String& unit, bool boolean = false)
        : range (r), id (parameterId), name (parameterName), label (unit),
          defaultValue (r.convertTo0to1 (defaultRealValue)), isBool (boolean), value (defaultValue) {}

    float get() const                    { return range.convertFrom0to1 (value.load()); }
    bool isOn() const                    { return value.load() >= 0.5f; }
    void set (float realValue)           { setValueNotifyingHost (range.convertTo0to1 (range.snapToLegalValue (realValue))); }
    float getDefaultRealValue() const    { return range.convertFrom0to1 (defaultValue); }

    juce::String getParameterID() const override        { return id; }
    float getValue() const override                     { return value.load(); }
    void setValue (float v) override                    { value = v; }
    float getDefaultValue() const override              { return defaultValue; }
    juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
    juce::String getLabel() const override              { return label; }
    juce::String getText (float v, int) const override
    {
        return isBool ? juce::String (v >= 0.5f ? "On" : "Off") : juce::String (range.convertFrom0to1 (v), 1);
    }
    float getValueForText (const juce::String& text) const override
    {
        if (isBool)
            return text.equalsIgnoreCase ("on") || text.getIntValue() != 0 ? 1.0f : 0.0f;
        return range.convertTo0to1 (range.snapToLegalValue (text.getFloatValue()));
    }
    bool isBoolean() const override                     { return isBool; }
    bool isDiscrete() const override                    { return isBool; }
    int getNumSteps() const override                    { return isBool ? 2 : juce::AudioProcessor::getDefaultNumParameterSteps(); }

    const juce::NormalisableRange<float> range;

private:
    const juce::String id, name, label;
    const float defaultValue;
    const bool isBool;
    std::atomic<float> value;
};
