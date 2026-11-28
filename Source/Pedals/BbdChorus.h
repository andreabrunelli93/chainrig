#pragma once

#include "PedalProcessor.h"
#include "CircuitModel.h"
#include "BbdDelay.h"

//==============================================================================
/** Chorus a BBD: enfasi degli acuti, filtro anti-aliasing (RC + Sallen-Key con inseguitore), linea BBD
    a 1024 stadi con clock modulato da un LFO a triangolo, filtro di ricostruzione, miscela con
    l'asciutto e de-enfasi. Comandi: RATE e DEPTH.
    Valori, fonti e scelte (legge del clock): nota di lavoro locale del circuito. */
class BbdChorus final : public PedalProcessor
{
public:
    BbdChorus();

    static Info getInfo();
    static constexpr double inputVolts = 2.5;

    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    double rateHz (double knob) const noexcept;

    BuiltInParameter* rate = nullptr;
    BuiltInParameter* depth = nullptr;
    juce::SmoothedValue<float> rateSmoothed, depthSmoothed;

    CircuitModel<2, 1, 1> emphasis;   // ingresso + enfasi (IC1A)
    CircuitModel<4, 1, 1> antiAlias;  // C5, R12/C6, Sallen-Key con Q1
    CircuitModel<3, 1, 1> recon;      // R18/C9, Sallen-Key con Q2
    CircuitModel<2, 2, 1> mixer;      // miscela + de-enfasi (IC1B) + condensatore d'uscita
    BbdDelay bbd;
    TriangleLfo lfo;

    double sampleRate = 48000.0, control = 0.0;
    int path = 0;   // solo prove (CHAINRIG_LAB_PATH): 0 tutto, 1 solo asciutto, 2 solo bagnato, 3 bagnato senza modulazione
    double minDelay = 1.0, maxDelay = 0.0;
};
