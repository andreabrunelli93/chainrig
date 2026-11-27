#pragma once

#include "PedalProcessor.h"
#include "TapeHysteresis.h"

//==============================================================================
/** Saturazione di nastro con un modello fisico (non un circuito):
      enfasi di registrazione -> isteresi magnetica del nastro (Jiles-Atherton, Runge-Kutta 4)
      -> de-enfasi -> perdite della testina di lettura (distanza e traferro, dipendono dalla
      velocità) -> "head bump" -> wow & flutter (ritardo modulato) -> LEVEL.
    Equazioni e parametri del nastro: Chowdhury, "Real-time physical modelling for analog tape
    machines", DAFx 2019 (scritto da zero dall'articolo, niente codice copiato).
    Comandi: DRIVE (campo magnetico, compensato in livello), BIAS (reversibilità: poco bias = più
    isteresi, più sporco), WOW (wow e flutter), LEVEL, SPEED 7,5 / 15 / 30 pollici al secondo. */
class TapeMachine final : public PedalProcessor
{
public:
    TapeMachine();

    static Info getInfo();

    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    struct OnePole   // filtro del primo ordine (bilineare), forma diretta I
    {
        double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
        double process (double x) noexcept
        {
            const double y = b0 * x + b1 * x1 - a1 * y1;
            x1 = x; y1 = y;
            return y;
        }
        void reset() noexcept   { x1 = y1 = 0; }
    };

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept   { z1 = z2 = 0; }
    };

    static constexpr int firLength = 31;   // a 2x: risoluzione ~3 kHz, la curva delle perdite è liscia

    void setSpeed (int index);
    double readDelay (double delaySamples) const noexcept;

    BuiltInParameter* drive = nullptr;
    BuiltInParameter* bias = nullptr;
    BuiltInParameter* wow = nullptr;
    BuiltInParameter* level = nullptr;
    BuiltInParameter* speed = nullptr;
    juce::SmoothedValue<float> driveSmoothed, biasSmoothed, wowSmoothed, levelSmoothed;

    double sampleRate = 192000.0;
    int currentSpeed = -1;

    // isteresi
    TapeHysteresis tape;
    double fieldScale = 0.0, readScale = 0.0, outputGain = 0.0;   // dalle manopole

    // filtri
    OnePole preEmphasis, deEmphasis, dcBlock, lowCut;
    Biquad headBump;
    std::array<double, firLength> fir {}, firState {};
    int firPos = 0;

    // wow & flutter
    std::vector<float> delayLine;
    int writePos = 0;
    double wowPhase = 0.0, flutterPhase = 0.0, wowDrift = 0.0, driftTarget = 0.0;
    int untilDrift = 0;
    juce::Random random { 1963 };

    // diagnostica
    double peakMagnetisation = 0.0;
};
