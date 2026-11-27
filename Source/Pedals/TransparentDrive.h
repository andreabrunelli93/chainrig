#pragma once

#include "PedalProcessor.h"
#include "CircuitModel.h"

//==============================================================================
/** "Centurion Klone": overdrive trasparente con miscela di pulito e clipping al germanio, comandato
    da un doppio potenziometro del gain; treble attivo e alimentazione a 18 V negli stadi d'uscita.

    Modello del circuito (metodo DK, op-amp ideali) diviso dove il circuito ha uscite di op-amp:
      A  buffer d'ingresso, rete FF-1 e stadio di guadagno (lineare)
      -> saturazione dell'op-amp a 9 V (circa ±3 V)
      B  clipping (coppia di diodi al germanio), ramo pulito e sommatore
      -> saturazione degli op-amp a 18 V (circa ±12 V)
      C  treble attivo, D  condensatore d'uscita e volume.
    Valori dei componenti e verifiche: nota di lavoro locale del circuito. */
class TransparentDrive final : public PedalProcessor
{
public:
    TransparentDrive();

    static Info getInfo();

    /** Per le prove: volt di picco all'ingresso del pedale per 1.0 digitale. */
    static constexpr double inputVolts = 2.5;

    juce::String getDiagnostics() const override
    {
        return "Newton: " + juce::String ((double) newtonIterations / juce::jmax (1.0, (double) newtonSamples), 2)
               + " iterazioni medie, massimo " + juce::String (newtonMax);
    }

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    void applyKnobs (bool force);

    BuiltInParameter* gain = nullptr;
    BuiltInParameter* treble = nullptr;
    BuiltInParameter* output = nullptr;
    juce::SmoothedValue<float> gainSmoothed, trebleSmoothed, outputSmoothed;

    CircuitModel<6, 1, 3> input;     // A
    CircuitModel<7, 3, 1> clipper;   // B
    CircuitModel<1, 1, 1> tone;      // C
    CircuitModel<1, 1, 1> out;       // D
    int gainA1 = 0, gainB1 = 0, gainA2 = 0, gainB2 = 0, trebleA = 0, trebleB = 0, volumeA = 0, volumeB = 0;
    int untilUpdate = 0;
    juce::int64 newtonIterations = 0, newtonSamples = 0;
    int newtonMax = 0;
};
