#include "NamProcessor.h"
#include "Look.h"
#include "PluginScanner.h"

// ResamplingContainer viene da iPlug2 e si aspetta queste due definizioni
namespace iplug { constexpr double PI = 3.14159265358979323846; }
#ifndef DEFAULT_BLOCK_SIZE
 #define DEFAULT_BLOCK_SIZE 512
#endif

#include <NAM/get_dsp.h>
#include <NAM/slimmable.h>
#include <ResamplingContainer/ResamplingContainer.h>
#include <filesystem>

using namespace juce;

namespace
{
    constexpr double normalizedLoudnessDb = -18.0;   // come il plugin NAM: catture diverse, volume simile
    constexpr double maxImpulseSeconds = 10.0;

    using Resampler = ::dsp::ResamplingContainer<double, 1, 12>;

    std::filesystem::path toPath (const File& f)
    {
       #if JUCE_WINDOWS
        return std::filesystem::path (f.getFullPathName().toWideCharPointer());
       #else
        return std::filesystem::path (f.getFullPathName().toStdString());
       #endif
    }

    String jsonString (const nlohmann::json& j, const char* key)
    {
        if (j.is_object() && j.contains (key) && j[key].is_string())
            return String::fromUTF8 (j[key].get<std::string>().c_str());
        return {};
    }
}

//==============================================================================
struct NamProcessor::Model
{
    std::unique_ptr<nam::DSP> dsp;
    std::unique_ptr<Resampler> resampler;   // nullptr se la scheda va già alla frequenza della cattura
    Resampler::BlockProcessFunc func;
    float normalizeGain = 1.0f;

    /** Thread audio: mono, n campioni. */
    void process (double* in, double* out, int n)
    {
        if (resampler != nullptr)
            resampler->ProcessBlock (&in, &out, n, func);
        else
            dsp->process (&in, &out, n);
    }
};

//==============================================================================
NamProcessor::NamProcessor()
    : AudioPluginInstance (BusesProperties().withInput  ("In",  AudioChannelSet::stereo(), true)
                                            .withOutput ("Out", AudioChannelSet::stereo(), true))
{
    const auto add = [this] (NamParameter* p)
    {
        addHostedParameter (std::unique_ptr<HostedParameter> (p));
        return p;
    };

    inputGain  = add (new NamParameter ("input",  "Input",  { -24.0f, 24.0f, 0.1f }, 0.0f, "dB"));
    outputGain = add (new NamParameter ("output", "Output", { -40.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    normalize  = add (new NamParameter ("normalize", "Normalize", { 0.0f, 1.0f, 1.0f }, 1.0f, {}, true));
    irEnabled  = add (new NamParameter ("ir", "IR", { 0.0f, 1.0f, 1.0f }, 1.0f, {}, true));

    startTimer (200);
}

NamProcessor::~NamProcessor()
{
    stopTimer();
    delete current;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

PluginDescription NamProcessor::getDescription()
{
    PluginDescription d;
    d.name = "NAM";
    d.descriptiveName = "Neural Amp Modeler capture + IR (built-in)";
    d.pluginFormatName = formatName;
    d.category = "Amp";
    d.manufacturerName = "ChainRig";
    d.version = JUCE_APPLICATION_VERSION_STRING;
    d.fileOrIdentifier = identifier;
    d.uniqueId = d.deprecatedUid = 0x4e414d31;   // "NAM1"
    d.isInstrument = false;
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
    return d;
}

void NamProcessor::fillInPluginDescription (PluginDescription& d) const
{
    d = getDescription();
}

File NamProcessor::getTonesFolder()
{
    return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("ChainRig").getChildFile ("Tones");
}

bool NamProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    return layout.getMainInputChannelSet() == AudioChannelSet::stereo()
        && layout.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

//==============================================================================
void NamProcessor::prepareModel (Model& m) const
{
    if (preparedRate <= 0.0 || preparedBlock <= 0)
        return;   // lo farà prepareToPlay

    const double expected = m.dsp->GetExpectedSampleRate() > 0.0 ? m.dsp->GetExpectedSampleRate() : 48000.0;

    if (m.dsp->SupportsArbitrarySampleRate() || std::abs (expected - preparedRate) < 1.0)
    {
        m.resampler.reset();
        m.dsp->Reset (preparedRate, preparedBlock);
    }
    else
    {
        // la cattura è stata fatta a 48 kHz e la scheda va (per esempio) a 44,1: si suona a 48 in mezzo a due ricampionatori
        m.resampler = std::make_unique<Resampler> (expected);
        m.resampler->Reset (preparedRate, preparedBlock);
        m.dsp->Reset (expected, (int) std::ceil (preparedBlock * expected / preparedRate) + 1);
        auto* model = m.dsp.get();
        m.func = [model] (double** in, double** out, int n) { model->process (in, out, n); };
    }
}

void NamProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    preparedRate = sampleRate;
    preparedBlock = maximumBlockSize;

    inDouble.setSize (1, maximumBlockSize);
    outDouble.setSize (1, maximumBlockSize);
    fadeDouble.setSize (2, maximumBlockSize);

    convolution.prepare ({ sampleRate, (uint32) maximumBlockSize, 1 });
    convolution.reset();

    // il thread audio è fermo: si possono toccare anche la cattura attiva e quella in arrivo
    if (current != nullptr)
        prepareModel (*current);

    if (auto* p = pending.exchange (nullptr))
    {
        prepareModel (*p);
        delete pending.exchange (p);
    }

    for (auto* s : { &inGainSmoothed, &outGainSmoothed })
        s->reset (sampleRate, 0.03);

    inGainSmoothed.setCurrentAndTargetValue (Decibels::decibelsToGain (inputGain->get()));
    outGainSmoothed.setCurrentAndTargetValue (Decibels::decibelsToGain (outputGain->get()));
    irMix.reset (sampleRate, 0.02);
    irMix.setCurrentAndTargetValue (irActive.load() && irEnabled->isOn() ? 1.0f : 0.0f);
}

void NamProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int n = jmin (buffer.getNumSamples(), inDouble.getNumSamples());
    if (n <= 0 || buffer.getNumChannels() < 1)
        return;

    // una cattura nuova (o nessuna) solo quando quella vecchia è già stata raccolta
    bool switching = false;
    Model* next = current;

    if (retired.load() == nullptr)
    {
        if (auto* p = pending.exchange (nullptr))
        {
            next = p;
            switching = true;
        }
        else if (clearPending.exchange (false) && current != nullptr)
        {
            next = nullptr;
            switching = true;
        }
    }

    // ingresso: canale sinistro (la chitarra), con il gain d'ingresso
    inGainSmoothed.setTargetValue (Decibels::decibelsToGain (inputGain->get()));
    auto* in = inDouble.getWritePointer (0);
    const auto* src = buffer.getReadPointer (0);
    for (int i = 0; i < n; ++i)
        in[i] = (double) (src[i] * inGainSmoothed.getNextValue());

    const auto run = [this, n] (Model* m, double* inData, double* out)
    {
        if (m == nullptr)
        {
            std::copy (inData, inData + n, out);   // senza cattura: passa il segnale com'è
            return;
        }

        m->process (inData, out, n);

        if (normalize->isOn() && ! exactlyEqual (m->normalizeGain, 1.0f))
            for (int i = 0; i < n; ++i)
                out[i] *= m->normalizeGain;
    };

    auto* out = outDouble.getWritePointer (0);

    if (switching)
    {
        // dissolvenza incrociata di un blocco: niente scatti cambiando cattura.
        // Ognuna ha la sua copia dell'ingresso (process non promette di lasciarlo intatto)
        auto* freshIn  = fadeDouble.getWritePointer (0);
        auto* freshOut = fadeDouble.getWritePointer (1);
        std::copy (in, in + n, freshIn);
        run (current, in, out);
        run (next, freshIn, freshOut);

        for (int i = 0; i < n; ++i)
        {
            const double t = (i + 1) / (double) n;
            out[i] = out[i] * (1.0 - t) + freshOut[i] * t;
        }

        retired.store (current);
        current = next;
    }
    else
    {
        run (current, in, out);
    }

    // in float, sul canale sinistro
    auto* left = buffer.getWritePointer (0);
    for (int i = 0; i < n; ++i)
        left[i] = (float) out[i];

    // IR (cono e cassa), con una breve dissolvenza quando la si accende o spegne
    irMix.setTargetValue (irActive.load() && irEnabled->isOn() ? 1.0f : 0.0f);

    if (irMix.isSmoothing() || irMix.getTargetValue() > 0.0f)
    {
        if (buffer.getNumChannels() > 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, n);   // il destro tiene il segnale senza IR

        float* channels[] = { left };
        juce::dsp::AudioBlock<float> block (channels, 1, (size_t) n);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));

        if (buffer.getNumChannels() > 1)
        {
            const auto* dry = buffer.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float mix = irMix.getNextValue();
                left[i] = dry[i] + (left[i] - dry[i]) * mix;
            }
        }
    }

    // uscita: gain e stesso segnale su entrambi i canali
    outGainSmoothed.setTargetValue (Decibels::decibelsToGain (outputGain->get()));
    for (int i = 0; i < n; ++i)
        left[i] *= outGainSmoothed.getNextValue();

    for (int c = 1; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);
}

void NamProcessor::timerCallback()
{
    // la cattura sostituita viene cancellata qui, mai sul thread audio
    delete retired.exchange (nullptr);
}

//==============================================================================
void NamProcessor::publish (std::unique_ptr<Model> model)
{
    clearPending = false;
    delete pending.exchange (model.release());   // se la precedente non era ancora stata presa, non serve più
}

bool NamProcessor::loadModel (const File& file, String& error)
{
    auto model = std::make_unique<Model>();
    nam::dspData data;

    try
    {
        nam::DspLoadOptions options;
        options.prewarm = false;   // il prewarm lo fa Reset() in prepareModel
        model->dsp = nam::get_dsp (toPath (file), data, options);
    }
    catch (const std::exception& e)
    {
        error = String::fromUTF8 (e.what());
        PluginScanner::log ("NAM    could not load " + file.getFileName() + ": " + error);
        return false;
    }

    if (model->dsp == nullptr)
    {
        error = "The file is not a NAM capture.";
        return false;
    }

    ModelInfo info;
    info.name = jsonString (data.metadata, "name");
    if (info.name.isEmpty())
        info.name = file.getFileNameWithoutExtension();

    info.gear = jsonString (data.metadata, "gear_model");
    if (info.gear.isEmpty())
        info.gear = (jsonString (data.metadata, "gear_make") + " " + jsonString (data.metadata, "gear_model")).trim();

    info.modeledBy = jsonString (data.metadata, "modeled_by");
    info.architecture = String (data.architecture);

    if (data.metadata.is_object() && data.metadata.contains ("t3k_id") && data.metadata["t3k_id"].is_number_integer())
        info.toneId = data.metadata["t3k_id"].get<int64>();

    info.sampleRate = model->dsp->GetExpectedSampleRate();
    info.hasLoudness = model->dsp->HasLoudness();
    info.loudness = info.hasLoudness ? model->dsp->GetLoudness() : 0.0;

    if (info.hasLoudness)
        model->normalizeGain = Decibels::decibelsToGain ((float) (normalizedLoudnessDb - info.loudness));

    // A2: la stessa cattura in due grandezze. Lite pesa meno sulla CPU (utile sui portatili lenti)
    if (auto* slim = dynamic_cast<nam::SlimmableModel*> (model->dsp.get()))
    {
        info.slimmable = true;
        slim->SetSlimmableSize (lite ? 0.0 : 1.0);
    }

    const auto startMs = Time::getMillisecondCounterHiRes();
    prepareModel (*model);
    const bool resampled = model->resampler != nullptr;
    publish (std::move (model));

    PluginScanner::log ("NAM    " + file.getFileName() + ": " + String (data.architecture)
                        + (info.slimmable ? (lite ? " (A2 lite)" : " (A2 full)") : String())
                        + ", " + String (info.sampleRate, 0) + " Hz" + (resampled ? " resampled to " + String (preparedRate, 0) : String())
                        + (info.hasLoudness ? ", loudness " + String (info.loudness, 1) + " dB" : String())
                        + ", prepared in " + String (Time::getMillisecondCounterHiRes() - startMs, 1) + " ms");

    modelFile = file;
    modelInfo = info;
    modelLoaded = true;
    stateChanged();
    return true;
}

void NamProcessor::clearModel()
{
    delete pending.exchange (nullptr);
    clearPending = true;
    modelFile = File();
    modelInfo = {};
    modelLoaded = false;
    stateChanged();
}

void NamProcessor::setLite (bool shouldBeLite)
{
    if (lite == shouldBeLite)
        return;

    lite = shouldBeLite;

    // si ricarica il file: la cattura attiva appartiene al thread audio
    if (modelLoaded && modelInfo.slimmable)
    {
        String error;
        loadModel (modelFile, error);
    }
    else
    {
        stateChanged();
    }
}

bool NamProcessor::loadImpulseResponse (const File& file, String& error)
{
    AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        error = "The file is not an audio file that ChainRig can read.";
        return false;
    }

    const auto length = (int) jmin (reader->lengthInSamples, (int64) (reader->sampleRate * maxImpulseSeconds));
    AudioBuffer<float> ir (1, length);
    reader->read (&ir, 0, length, 0, true, false);   // solo il primo canale: la catena è mono fino all'uscita

    // Convolution carica e passa alla nuova IR con una dissolvenza, senza fermare l'audio
    convolution.loadImpulseResponse (std::move (ir), reader->sampleRate, juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
    irActive = true;
    irFile = file;
    irLoaded = true;
    stateChanged();
    return true;
}

void NamProcessor::clearImpulseResponse()
{
    irActive = false;
    irFile = File();
    irLoaded = false;
    stateChanged();
}

void NamProcessor::stateChanged()
{
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));

    if (onFilesChanged != nullptr)
        onFilesChanged();
}

File NamProcessor::findMovedFile (const File& original)
{
    if (original.existsAsFile())
        return original;

    // preset da un altro computer: stesso nome di file nella cartella Tones
    const auto folder = getTonesFolder();
    if (! folder.isDirectory())
        return {};

    const auto found = folder.findChildFiles (File::findFiles, true, original.getFileName(), File::FollowSymlinks::no);
    return found.isEmpty() ? File() : found.getFirst();
}

//==============================================================================
void NamProcessor::getStateInformation (MemoryBlock& dest)
{
    XmlElement xml ("CHAINRIG_NAM");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("model", modelFile.getFullPathName());
    xml.setAttribute ("toneId", String (modelInfo.toneId));
    xml.setAttribute ("ir", irFile.getFullPathName());
    xml.setAttribute ("lite", lite);
    xml.setAttribute ("input", (double) inputGain->get());
    xml.setAttribute ("output", (double) outputGain->get());
    xml.setAttribute ("normalize", normalize->isOn());
    xml.setAttribute ("irEnabled", irEnabled->isOn());
    copyXmlToBinary (xml, dest);
}

void NamProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("CHAINRIG_NAM"))
        return;

    inputGain->set  ((float) xml->getDoubleAttribute ("input", 0.0));
    outputGain->set ((float) xml->getDoubleAttribute ("output", 0.0));
    normalize->set  (xml->getBoolAttribute ("normalize", true) ? 1.0f : 0.0f);
    irEnabled->set  (xml->getBoolAttribute ("irEnabled", true) ? 1.0f : 0.0f);
    lite = xml->getBoolAttribute ("lite", false);

    const File savedModel (xml->getStringAttribute ("model"));
    const File savedIr (xml->getStringAttribute ("ir"));
    String error;

    if (savedModel == File())
        clearModel();
    else if (const auto f = findMovedFile (savedModel); f == File() || ! loadModel (f, error))
    {
        // il file manca: resta il percorso, così l'interfaccia può dire quale cattura serve
        clearModel();
        modelFile = savedModel;
        modelInfo.name = savedModel.getFileNameWithoutExtension();
        modelInfo.toneId = xml->getStringAttribute ("toneId").getLargeIntValue();
    }

    if (savedIr == File())
        clearImpulseResponse();
    else if (const auto f = findMovedFile (savedIr); f == File() || ! loadImpulseResponse (f, error))
    {
        clearImpulseResponse();
        irFile = savedIr;
    }

    stateChanged();
}

//==============================================================================
//==============================================================================
namespace
{
    /** Riquadro con titolo, come le card dell'interfaccia principale. */
    void drawCard (Graphics& g, Rectangle<int> r, const String& title)
    {
        g.setColour (Palette::card);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (Palette::border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
        g.setColour (Palette::textFaint);
        g.setFont (Fonts::display (13.0f));
        g.drawText (title, r.reduced (18, 12).removeFromTop (16), Justification::topLeft);
    }
}

class NamEditor final : public AudioProcessorEditor,
                        private Timer
{
public:
    explicit NamEditor (NamProcessor& p) : AudioProcessorEditor (p), nam (p)
    {
        for (auto* b : std::initializer_list<Component*> { &loadModelButton, &loadIrButton, &clearModelButton, &clearIrButton })
            addAndMakeVisible (b);

        loadModelButton.onClick = [this] { choose (true); };
        loadIrButton.onClick    = [this] { choose (false); };
        clearModelButton.onClick = [this] { nam.clearModel(); };
        clearIrButton.onClick    = [this] { nam.clearImpulseResponse(); };
        clearModelButton.setTitle ("Remove capture");
        clearIrButton.setTitle ("Remove IR");

        for (auto* t : { &normalizeToggle, &liteToggle, &irToggle })
        {
            t->setClickingTogglesState (true);
            addAndMakeVisible (t);
        }

        normalizeToggle.onClick = [this] { nam.normalize->set (normalizeToggle.getToggleState() ? 1.0f : 0.0f); };
        irToggle.onClick        = [this] { nam.irEnabled->set (irToggle.getToggleState() ? 1.0f : 0.0f); };
        liteToggle.onClick      = [this] { nam.setLite (liteToggle.getToggleState()); };

        setupKnob (inputKnob, *nam.inputGain, 0.0);
        setupKnob (outputKnob, *nam.outputGain, 0.0);
        outputKnob.setSkewFactorFromMidPoint (0.0);   // 0 dB al centro, anche se la scala va da -40 a +12

        nam.onFilesChanged = [safe = Component::SafePointer<NamEditor> (this)]
        {
            if (safe != nullptr)
                safe->refresh();
        };

        refresh();
        startTimerHz (10);   // parametri cambiati da fuori (preset, MIDI)
        setSize (760, 500);
    }

    ~NamEditor() override
    {
        nam.onFilesChanged = nullptr;
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Palette::panel);

        drawCard (g, modelCard, "CAPTURE");
        drawCard (g, irCard, "CAB IR");
        drawCard (g, levelCard, "LEVELS");

        const auto& info = nam.getModelInfo();

        // cattura: nome grande, sotto apparecchio e autore
        {
            auto r = modelCard.reduced (18, 12);
            r.removeFromTop (22);
            auto text = r.withTrimmedRight (330);

            g.setFont (Fonts::text (22.0f, true));
            g.setColour (nam.isModelMissing() ? Palette::danger : (nam.getModelFile() == File() ? Palette::textFaint : Palette::text));
            g.drawFittedText (nam.getModelFile() == File() ? String ("No capture loaded") : info.name,
                              text.removeFromTop (30), Justification::centredLeft, 1);

            String detail;
            if (nam.isModelMissing())
                detail = "File not found: put " + nam.getModelFile().getFileName() + " in Documents/ChainRig/Tones";
            else if (nam.getModelFile() != File())
            {
                StringArray parts;
                if (info.gear.isNotEmpty())      parts.add (info.gear);
                if (info.modeledBy.isNotEmpty()) parts.add ("by " + info.modeledBy);
                parts.add (info.slimmable ? "A2" : info.architecture);
                if (info.sampleRate > 0)         parts.add (String (info.sampleRate / 1000.0, 1) + " kHz");
                detail = parts.joinIntoString (String::fromUTF8 ("  ·  "));
            }
            else
                detail = "Load a .nam file (A1 or A2)";

            g.setFont (Fonts::text (15.0f));
            g.setColour (nam.isModelMissing() ? Palette::danger.withAlpha (0.8f) : Palette::textDim);
            g.drawFittedText (detail, text.removeFromTop (22), Justification::centredLeft, 1);
        }

        // IR
        {
            auto r = irCard.reduced (18, 12);
            r.removeFromTop (22);
            auto text = r.withTrimmedRight (330);
            const auto file = nam.getImpulseResponseFile();

            g.setFont (Fonts::text (19.0f, true));
            g.setColour (nam.isImpulseResponseMissing() ? Palette::danger
                                                        : file == File() ? Palette::textFaint : Palette::text);
            g.drawFittedText (file == File() ? String ("No IR (use one if the capture is amp only)")
                                             : file.getFileNameWithoutExtension()
                                               + (nam.isImpulseResponseMissing() ? String (" (file not found)") : String()),
                              text.removeFromTop (30), Justification::centredLeft, 1);
        }

        // valori delle manopole
        g.setFont (Fonts::mono (15.0f));
        g.setColour (Palette::text);
        for (auto [knob, label] : { std::pair { &inputKnob, "INPUT" }, std::pair { &outputKnob, "OUTPUT" } })
        {
            const auto b = knob->getBounds();
            g.setColour (Palette::textFaint);
            g.setFont (Fonts::display (12.0f));
            g.drawText (label, b.getX() - 30, b.getBottom() + 2, b.getWidth() + 60, 16, Justification::centred);
            g.setColour (Palette::text);
            g.setFont (Fonts::mono (15.0f));
            g.drawText (String (knob->getValue(), 1) + " dB", b.getX() - 30, b.getBottom() + 18, b.getWidth() + 60, 18,
                        Justification::centred);
        }

        g.setFont (Fonts::text (12.0f));
        g.setColour (Palette::textFaint);
        g.drawText ("Neural Amp Modeler (MIT)", getLocalBounds().reduced (20, 8).removeFromBottom (16),
                    Justification::centredRight);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (20);
        area.removeFromBottom (12);

        modelCard = area.removeFromTop (130);
        area.removeFromTop (12);
        irCard = area.removeFromTop (130);
        area.removeFromTop (12);
        levelCard = area;

        const auto placeButtons = [] (Rectangle<int> card, Component& load, Component& clear, Component* toggleA, Component* toggleB)
        {
            auto r = card.reduced (18, 14);
            r.removeFromTop (20);
            auto row = r.removeFromTop (48).removeFromRight (310);
            clear.setBounds (row.removeFromRight (48));
            row.removeFromRight (8);
            load.setBounds (row);

            auto toggles = r.removeFromBottom (34).removeFromRight (310);
            if (toggleB != nullptr)
            {
                toggleB->setBounds (toggles.removeFromRight (150));
                toggles.removeFromRight (10);
            }
            if (toggleA != nullptr)
                toggleA->setBounds (toggles.removeFromRight (150));
        };

        placeButtons (modelCard, loadModelButton, clearModelButton, &normalizeToggle, &liteToggle);
        placeButtons (irCard, loadIrButton, clearIrButton, &irToggle, nullptr);

        auto knobs = levelCard.reduced (18, 12);
        knobs.removeFromTop (14);
        knobs.removeFromBottom (36);
        const int size = jmin (knobs.getHeight(), 84);
        inputKnob.setBounds (knobs.getCentreX() - 160 - size / 2, knobs.getY(), size, size);
        outputKnob.setBounds (knobs.getCentreX() + 160 - size / 2, knobs.getY(), size, size);
    }

private:
    void setupKnob (Slider& knob, NamParameter& param, double defaultValue)
    {
        knob.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        knob.setRange (param.range.start, param.range.end, 0.1);
        knob.setDoubleClickReturnValue (true, defaultValue);
        knob.setMouseDragSensitivity (300);
        knob.setTitle (param.getName (32));
        knob.setValue (param.get(), dontSendNotification);
        knob.onDragStart  = [&param] { param.beginChangeGesture(); };
        knob.onDragEnd    = [&param] { param.endChangeGesture(); };
        knob.onValueChange = [this, &knob, &param]
        {
            param.set ((float) knob.getValue());
            repaint();
        };
        addAndMakeVisible (knob);
    }

    void refresh()
    {
        const auto& info = nam.getModelInfo();
        liteToggle.setVisible (info.slimmable);
        liteToggle.setToggleState (nam.isLite(), dontSendNotification);
        clearModelButton.setEnabled (nam.getModelFile() != File());
        clearIrButton.setEnabled (nam.getImpulseResponseFile() != File());
        normalizeToggle.setEnabled (info.hasLoudness);
        irToggle.setEnabled (nam.getImpulseResponseFile() != File());
        timerCallback();
        repaint();
    }

    void timerCallback() override
    {
        normalizeToggle.setToggleState (nam.normalize->isOn(), dontSendNotification);
        irToggle.setToggleState (nam.irEnabled->isOn(), dontSendNotification);

        bool changed = false;
        for (auto [knob, param] : { std::pair { &inputKnob, nam.inputGain }, std::pair { &outputKnob, nam.outputGain } })
            if (! knob->isMouseButtonDown() && ! approximatelyEqual (knob->getValue(), (double) param->get()))
            {
                knob->setValue (param->get(), dontSendNotification);
                changed = true;
            }

        if (changed)
            repaint();
    }

    void choose (bool model)
    {
        const auto start = model ? nam.getModelFile() : nam.getImpulseResponseFile();
        auto folder = start.getParentDirectory();
        if (! folder.isDirectory())
            folder = NamProcessor::getTonesFolder().isDirectory() ? NamProcessor::getTonesFolder()
                                                                  : File::getSpecialLocation (File::userHomeDirectory);

        chooser = std::make_unique<FileChooser> (model ? "Choose a NAM capture" : "Choose an impulse response (IR)",
                                                 folder, model ? "*.nam" : "*.wav;*.aif;*.aiff;*.flac");

        // la finestra del plugin è sempre in primo piano: la finestra dei file finirebbe dietro
        auto* window = getTopLevelComponent();
        const bool wasOnTop = window != nullptr && window->isAlwaysOnTop();
        if (wasOnTop)
            window->setAlwaysOnTop (false);

        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [safe = Component::SafePointer<NamEditor> (this), model, wasOnTop] (const FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            if (auto* w = safe->getTopLevelComponent(); w != nullptr && wasOnTop)
                w->setAlwaysOnTop (true);

            const auto file = fc.getResult();
            if (file == File())
                return;

            String error;
            const bool ok = model ? safe->nam.loadModel (file, error) : safe->nam.loadImpulseResponse (file, error);

            if (! ok)
                AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                            .withTitle (model ? "Could not load the capture" : "Could not load the IR")
                                            .withMessage (file.getFileName() + "\n\n" + error)
                                            .withButton ("OK", 1), nullptr);
        });
    }

    NamProcessor& nam;
    std::unique_ptr<FileChooser> chooser;
    Rectangle<int> modelCard, irCard, levelCard;

    TextButton loadModelButton { "LOAD CAPTURE..." }, loadIrButton { "LOAD IR..." };
    StageButton clearModelButton { "", Icons::Ui::close }, clearIrButton { "", Icons::Ui::close };
    TextButton normalizeToggle { "NORMALIZE" }, liteToggle { "LITE (LESS CPU)" }, irToggle { "IR ON" };
    Slider inputKnob, outputKnob;
};

AudioProcessorEditor* NamProcessor::createEditor()
{
    return new NamEditor (*this);
}
