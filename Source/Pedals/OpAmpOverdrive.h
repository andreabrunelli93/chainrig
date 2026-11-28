#pragma once

#include "PedalProcessor.h"
#include "CircuitModel.h"

//==============================================================================
/** Overdrive a op-amp con i diodi nella retroazione: inseguitore d'ingresso, stadio non invertente con
    passa-alto a 720 Hz sul ramo verso Vref e DRIVE in retroazione (guadagno 12-107), clipping morbido
    (uscita = ingresso + tensione sui diodi), passa-basso a 723 Hz, tono attivo, volume, inseguitore
    d'uscita. Valori, fonti e verifiche: nota di lavoro locale del circuito. */
class OpAmpOverdrive final : public PedalProcessor
{
public:
    OpAmpOverdrive();

    static Info getInfo();
    static constexpr double inputVolts = 2.5;

    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    void applyKnobs (bool force);

    BuiltInParameter* drive = nullptr;
    BuiltInParameter* tone = nullptr;
    BuiltInParameter* level = nullptr;
    juce::SmoothedValue<float> driveSmoothed, toneSmoothed, levelSmoothed;

    CircuitModel<4, 1, 1> clipper;   // ingresso, inseguitore, stadio di clipping
    CircuitModel<2, 1, 1> toneStage; // passa-basso e tono
    CircuitModel<3, 1, 1> out;       // volume, inseguitore, uscita
    int driveR = 0, toneA = 0, toneB = 0, levelA = 0, levelB = 0, untilUpdate = 0;
    juce::int64 newtonIterations = 0, newtonSamples = 0;
};
