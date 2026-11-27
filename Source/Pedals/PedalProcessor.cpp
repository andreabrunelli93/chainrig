#include "PedalProcessor.h"

using namespace juce;

PedalProcessor::PedalProcessor (Info i)
    : AudioPluginInstance (BusesProperties().withInput  ("In",  AudioChannelSet::stereo(), true)
                                            .withOutput ("Out", AudioChannelSet::stereo(), true)),
      info (std::move (i))
{
}

PluginDescription PedalProcessor::describe (const Info& i)
{
    PluginDescription d;
    d.name = i.name;
    d.descriptiveName = i.descriptiveName;
    d.pluginFormatName = "Built-in";
    d.category = i.category;
    d.manufacturerName = "ChainRig";
    d.version = JUCE_APPLICATION_VERSION_STRING;
    d.fileOrIdentifier = i.identifier;
    d.uniqueId = d.deprecatedUid = i.uniqueId;
    d.isInstrument = false;
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
    return d;
}

BuiltInParameter* PedalProcessor::addKnob (const String& id, const String& name, float defaultValue)
{
    auto* p = new BuiltInParameter (id, name, { 0.0f, 10.0f, 0.01f }, defaultValue, {});
    addHostedParameter (std::unique_ptr<HostedParameter> (p));
    knobs.add (p);
    return p;
}

BuiltInParameter* PedalProcessor::addSwitch (const String& id, const String& name, bool defaultOn)
{
    auto* p = new BuiltInParameter (id, name, { 0.0f, 1.0f, 1.0f }, defaultOn ? 1.0f : 0.0f, {}, true);
    addHostedParameter (std::unique_ptr<HostedParameter> (p));
    switches.add (p);
    return p;
}

BuiltInParameter* PedalProcessor::addSelector (const String& id, const String& name,
                                              const StringArray& choices, int defaultIndex)
{
    jassert (choices.size() > 1);
    auto* p = new BuiltInParameter (id, name, { 0.0f, (float) (choices.size() - 1), 1.0f }, (float) defaultIndex, {});
    addHostedParameter (std::unique_ptr<HostedParameter> (p));
    selectors.push_back ({ p, choices });
    return p;
}

Array<BuiltInParameter*> PedalProcessor::getControls() const
{
    auto all = knobs;
    all.addArray (switches);
    for (auto& s : selectors)
        all.add (s.parameter);
    return all;
}

bool PedalProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    return layout.getMainInputChannelSet() == AudioChannelSet::stereo()
        && layout.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void PedalProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    preparedBlock = jmax (1, maximumBlockSize);

    // filtri IIR polifase: latenza di pochi campioni, adatta al vivo
    oversampling = std::make_unique<dsp::Oversampling<float>> (info.stereoOutput ? 2 : 1, (size_t) info.oversamplingOrder,
                                                               dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                               true, false);
    oversampling->initProcessing ((size_t) preparedBlock);
    setLatencySamples (roundToInt (oversampling->getLatencyInSamples()));

    prepareCircuit (sampleRate * (double) oversampling->getOversamplingFactor());
}

void PedalProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    if (oversampling == nullptr || buffer.getNumChannels() < 1)
        return;

    // pedali stereo: ingresso mono (sinistro) su entrambi i canali, poi il pedale scrive le due uscite
    const bool stereo = info.stereoOutput && buffer.getNumChannels() > 1;
    if (stereo)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    // a blocchi non più lunghi di quello preparato (l'oversampling ha i buffer di quella misura)
    for (int start = 0; start < buffer.getNumSamples(); start += preparedBlock)
    {
        const int n = jmin (preparedBlock, buffer.getNumSamples() - start);
        float* channels[] = { buffer.getWritePointer (0, start), stereo ? buffer.getWritePointer (1, start) : nullptr };
        dsp::AudioBlock<float> block (channels, stereo ? 2u : 1u, (size_t) n);

        auto up = oversampling->processSamplesUp (block);
        if (stereo)
            processCircuitStereo (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples());
        else
            processCircuit (up.getChannelPointer (0), (int) up.getNumSamples());
        oversampling->processSamplesDown (block);
    }

    if (! stereo)
        for (int c = 1; c < buffer.getNumChannels(); ++c)
            buffer.copyFrom (c, 0, buffer, 0, 0, buffer.getNumSamples());
}

void PedalProcessor::getStateInformation (MemoryBlock& dest)
{
    XmlElement xml ("CHAINRIG_PEDAL");
    xml.setAttribute ("id", info.identifier);
    xml.setAttribute ("version", 1);
    for (auto* k : knobs)
        xml.setAttribute (k->getParameterID(), (double) k->get());
    for (auto* sw : switches)
        xml.setAttribute (sw->getParameterID(), sw->isOn());
    for (auto& s : selectors)
        xml.setAttribute (s.parameter->getParameterID(), s.getIndex());
    copyXmlToBinary (xml, dest);
}

void PedalProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("CHAINRIG_PEDAL"))
        return;

    // manopole mancanti (preset vecchi): valore predefinito; sconosciute: ignorate
    for (auto* k : knobs)
        k->set ((float) xml->getDoubleAttribute (k->getParameterID(), k->getDefaultRealValue()));
    for (auto* sw : switches)
        sw->set (xml->getBoolAttribute (sw->getParameterID(), sw->getDefaultRealValue() >= 0.5f) ? 1.0f : 0.0f);
    for (auto& s : selectors)
        s.parameter->set ((float) xml->getIntAttribute (s.parameter->getParameterID(),
                                                        roundToInt (s.parameter->getDefaultRealValue())));
}
