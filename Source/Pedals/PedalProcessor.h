#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../BuiltInParameter.h"
#include "Artwork.h"

//==============================================================================
/** Base dei pedali integrati (modelli di circuiti analogici). Fa il lavoro ripetitivo di
    AudioPluginInstance: descrizione, manopole come parametri, stato XML, oversampling.
    Suona il canale sinistro (la chitarra) e lo manda su entrambe le uscite, come il posto NAM.

    Ogni pedale scrive solo prepareCircuit() e processCircuit(), che lavorano alla frequenza
    sovracampionata, in volt: 1.0 digitale = 1 V di picco all'ingresso del pedale. */
class PedalProcessor : public juce::AudioPluginInstance
{
public:
    /** Aspetto della finestra del pedale: il corpo, l'inchiostro delle scritte, le manopole. */
    struct Look
    {
        juce::Colour enclosureTop    { 0xff3a3d42 }, enclosureBottom { 0xff26282c };
        juce::Colour ink             { 0xffd8d4cb };   // scritte incise sul corpo
        juce::Colour knob            { 0xff141414 }, pointer { 0xfff0ece2 };
        juce::Colour led             { 0xffff3b2f };
        bool brushed = false;                         // righe orizzontali da metallo spazzolato
        juce::String subtitle;                        // riga piccola sotto il nome
        Artwork::Drawing emblem;                      // disegno inciso tra manopole e nome
        float wear = 0.0f;                            // 0 nuovo ... 1 molto consumato

        /** Posizione di una manopola: centro orizzontale (0-1 della larghezza del corpo), distanza del
            bordo alto dal corpo in pixel, diametro in pixel. Vuoto = tutte in fila, uguali. */
        struct KnobPlace { float x = 0.5f, top = 86.0f, size = 112.0f; };
        std::vector<KnobPlace> knobPlaces;            // nell'ordine delle manopole (addKnob)
        bool footPlate = false;                       // placca rettangolare grande sotto il nome

        // misure della finestra (0 = automatiche) e posizione del nome dal bordo alto del corpo
        int width = 0, height = 0;
        float nameTop = 0.0f;
    };

    /** Selettore a più posizioni (es. RATIO 2:1 / 4:1 / 10:1): parametro 0 … n-1. */
    struct Selector
    {
        BuiltInParameter* parameter = nullptr;
        juce::StringArray choices;
        int getIndex() const   { return juce::roundToInt (parameter->get()); }
    };

    struct Info
    {
        juce::String identifier;       // non cambiarlo mai dopo un rilascio: i preset lo usano
        juce::String name;             // compare nel posto
        juce::String descriptiveName;  // decide anche il tipo del posto (Engine::guessSlotKind)
        juce::String category;
        juce::String menuCategory;     // sottomenu "Built-in effects"
        int uniqueId = 0;              // 4 caratteri; mai cambiarlo dopo un rilascio
        int oversamplingOrder = 2;     // 2^order: 2 = 4x
        bool stereoOutput = false;     // true: processCircuitStereo (ingresso mono, due uscite)
        Look look;
    };

    explicit PedalProcessor (Info);

    static juce::PluginDescription describe (const Info&);
    const Info& getInfo() const                               { return info; }
    const juce::Array<BuiltInParameter*>& getKnobs() const   { return knobs; }
    const juce::Array<BuiltInParameter*>& getSwitches() const { return switches; }
    const std::vector<Selector>& getSelectors() const         { return selectors; }

    /** Tutti i comandi (manopole, interruttori, selettori): per gli strumenti di prova. */
    juce::Array<BuiltInParameter*> getControls() const;

    /** Per le prove offline (pedal_lab_render): statistiche del modello, es. iterazioni del solutore. */
    virtual juce::String getDiagnostics() const               { return {}; }

    //==============================================================================
    const juce::String getName() const override              { return info.name; }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = describe (info); }

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    double getTailLengthSeconds() const override             { return 0.0; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    juce::AudioProcessorEditor* createEditor() override;     // PedalEditor.cpp
    bool hasEditor() const override                          { return true; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

protected:
    /** Manopola 0-10 come sul pedale. */
    BuiltInParameter* addKnob (const juce::String& id, const juce::String& name, float defaultValue);

    /** Interruttore acceso/spento (nella finestra: un pulsante con la spia sotto le manopole). */
    BuiltInParameter* addSwitch (const juce::String& id, const juce::String& name, bool defaultOn);

    /** Selettore a più posizioni (nella finestra: un pulsante che passa alla posizione dopo). */
    BuiltInParameter* addSelector (const juce::String& id, const juce::String& name,
                                   const juce::StringArray& choices, int defaultIndex);

    /** Thread audio fermo. rate = frequenza sovracampionata. */
    virtual void prepareCircuit (double rate) = 0;
    /** Thread audio: n campioni mono alla frequenza sovracampionata, in volt, sul posto. */
    virtual void processCircuit (float* samples, int n) = 0;

    /** Solo con Info::stereoOutput: `left` contiene l'ingresso (mono) e riceve l'uscita sinistra,
        `right` riceve l'uscita destra. */
    virtual void processCircuitStereo (float* left, float* right, int n)
    {
        processCircuit (left, n);
        std::copy (left, left + n, right);
    }

private:
    Info info;
    juce::Array<BuiltInParameter*> knobs, switches;
    std::vector<Selector> selectors;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    int preparedBlock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalProcessor)
};
