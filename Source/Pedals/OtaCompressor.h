#pragma once

#include "PedalProcessor.h"
#include "CircuitModel.h"
#include "EnvelopeDetector.h"

//==============================================================================
/** Compressore a OTA in retroazione: inseguitore d'ingresso, OTA (I = Iabc tanh(v / 2VT)) con
    enfasi degli acuti all'ingresso e de-enfasi sul carico, separatore di fase, rivelatore a onda
    intera a transistor che scarica il condensatore dell'inviluppo (rilascio 150k x 10 µF) e comanda
    la corrente Iabc. Comandi: SUSTAIN (500k lineare in serie a 27k) e LEVEL (50k logaritmico), più BLEND
    (come nelle versioni moderne con miscela: segnale pulito e compresso sommati prima di LEVEL).
    Valori, fonti e verifiche: nota di lavoro locale del circuito. */
class OtaCompressor final : public PedalProcessor
{
public:
    OtaCompressor();

    static Info getInfo();
    static constexpr double inputVolts = 2.5;   // come gli altri pedali: 1.0 digitale = 2,5 V di picco

    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    void applyKnobs (bool force);

    BuiltInParameter* sustain = nullptr;
    BuiltInParameter* level = nullptr;
    BuiltInParameter* blend = nullptr;   // aggiunta (non c'è nel pedale originale): pulito / compresso
    juce::SmoothedValue<float> sustainSmoothed, levelSmoothed, blendSmoothed;

    CircuitModel<4, 1, 2> input;   // ingresso fino ai due pin dell'OTA
    CircuitModel<1, 1, 1> load;    // carico dell'OTA (150k || 1n verso Vb)
    CircuitModel<1, 1, 1> out;     // condensatore d'uscita e LEVEL
    DetectorBranch fromCollector, fromEmitter;
    EnvelopeHold envelope;

    int levelA = 0, levelB = 0, untilUpdate = 0;
    double sustainOhms = 250e3, iabc = 0.0;

    // diagnostica (solo per le prove)
    double iabcMin = 1.0, iabcSum = 0.0;
    juce::int64 samplesSeen = 0;
};
