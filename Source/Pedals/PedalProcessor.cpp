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

bool PedalProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    return layout.getMainInputChannelSet() == AudioChannelSet::stereo()
        && layout.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void PedalProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    preparedBlock = jmax (1, maximumBlockSize);

    // filtri IIR polifase: latenza di pochi campioni, adatta al vivo
    oversampling = std::make_unique<dsp::Oversampling<float>> (1, (size_t) info.oversamplingOrder,
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

    // a blocchi non più lunghi di quello preparato (l'oversampling ha i buffer di quella misura)
    for (int start = 0; start < buffer.getNumSamples(); start += preparedBlock)
    {
        const int n = jmin (preparedBlock, buffer.getNumSamples() - start);
        float* left = buffer.getWritePointer (0, start);
        float* channels[] = { left };
        dsp::AudioBlock<float> block (channels, 1, (size_t) n);

        auto up = oversampling->processSamplesUp (block);
        processCircuit (up.getChannelPointer (0), (int) up.getNumSamples());
        oversampling->processSamplesDown (block);
    }

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
}
