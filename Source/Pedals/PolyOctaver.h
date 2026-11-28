#pragma once

#include "PedalProcessor.h"
#include "OctaveBank.h"

//==============================================================================
/** Octaver polifonico (progetto originale "nello stile" di un octaver polifonico digitale: il pedale
    di riferimento è digitale, non c'è un circuito da copiare). Tre volumi indipendenti come il
    pedale di riferimento (pulito, ottava sotto, ottava sopra) più FILTER, un passa-basso sulle sole
    ottave (dal modello più grande della stessa serie). Motore: OctaveBank. */
class PolyOctaver final : public PedalProcessor
{
public:
    PolyOctaver();

    static Info getInfo();

    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    void updateFilter (double knob);

    BuiltInParameter* dry = nullptr;
    BuiltInParameter* sub = nullptr;
    BuiltInParameter* up = nullptr;
    BuiltInParameter* filter = nullptr;
    juce::SmoothedValue<float> drySmoothed, subSmoothed, upSmoothed;

    OctaveBank bank;
    double sampleRate = 48000.0, lastFilter = -1.0;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;   // passa-basso delle ottave
};
