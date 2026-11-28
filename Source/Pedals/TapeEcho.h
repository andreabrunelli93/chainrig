#pragma once

#include "PedalProcessor.h"
#include "TapeHysteresis.h"

//==============================================================================
/** Eco a nastro a tre testine di lettura, dallo schema a blocchi del costruttore:
      ingresso -> preamplificatore di registrazione (qui si somma il ritorno INTENSITY) -> enfasi ->
      nastro (isteresi, TapeHysteresis) -> anello di nastro -> testine 1, 2, 3 (ritardi 1x, 2x, 3x)
      -> MODE (quali testine) -> preamplificatore di lettura (perdite che dipendono dalla velocità)
      -> [ritorno INTENSITY preso qui, prima dei toni] -> ECHO VOLUME -> BASS / TREBLE -> + asciutto.
    REPEAT RATE cambia la velocità del motore (con l'inerzia del motore: il ritardo scivola).
    Niente riverbero a molla (sezione separata nel pedale vero). Valori e scelte: nota di lavoro locale. */
class TapeEcho final : public PedalProcessor
{
public:
    TapeEcho();

    static Info getInfo();

    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    struct OnePole   // primo ordine (bilineare)
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

    void updateFilters (double speedRatio, bool force);
    double readHead (double delaySamples) const noexcept;

    BuiltInParameter* input = nullptr;
    BuiltInParameter* repeatRate = nullptr;
    BuiltInParameter* intensity = nullptr;
    BuiltInParameter* echoVolume = nullptr;
    BuiltInParameter* bass = nullptr;
    BuiltInParameter* treble = nullptr;
    BuiltInParameter* wow = nullptr;
    BuiltInParameter* mode = nullptr;
    juce::SmoothedValue<float> inputSmoothed, intensitySmoothed, echoSmoothed, bassSmoothed, trebleSmoothed, wowSmoothed;

    double sampleRate = 96000.0;
    TapeHysteresis tape;
    OnePole preEmphasis, deEmphasis, lowCut, bassShelf, trebleShelf, dcBlock;
    Biquad headLoss;

    std::vector<float> loop;   // il nastro: magnetizzazione registrata
    int writePos = 0;

    double speed = 1.0;        // velocità relativa (1 = REPEAT RATE al massimo), con l'inerzia del motore
    double motorCoeff = 0.0, lastFilterSpeed = -1.0, lastBass = 1e9, lastTreble = 1e9;
    double feedback = 0.0;     // uscita delle testine al campione prima (ritorno INTENSITY)
    double wowPhase = 0.0, flutterPhase = 0.0, wowDrift = 0.0, driftTarget = 0.0;
    int untilDrift = 0, untilFilters = 0;
    juce::Random random { 201 };

    double peakPlayback = 0.0;
};
