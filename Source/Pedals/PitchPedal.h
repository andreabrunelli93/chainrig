#pragma once

#include "PedalProcessor.h"
#include "GrainShifter.h"

//==============================================================================
/** Pitch shifter a pedale (progetto originale "nello stile" di un pedale whammy: quello vero è digitale,
    non c'è un circuito da copiare). TREADLE da tallone (intonazione originale) a punta (intervallo del
    modo scelto); se arriva il pedale d'espressione MIDI (Expression.h) comanda lui al posto della
    manopola. Solo segnale trasformato, come i modi whammy del pedale di riferimento. Motore: GrainShifter. */
class PitchPedal final : public PedalProcessor
{
public:
    PitchPedal();

    static Info getInfo();
    static const juce::StringArray& modeNames();

    float getKnobDisplayValue (int index) const override;
    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    float currentTreadle() const noexcept;

    BuiltInParameter* treadle = nullptr;
    BuiltInParameter* mode = nullptr;

    GrainShifter shifter;
    double sampleRate = 48000.0, semitones = 0.0, smoothCoeff = 0.0;
};
