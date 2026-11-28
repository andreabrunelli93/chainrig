#pragma once

#include "PedalProcessor.h"

//==============================================================================
/** Compressore a VCA "da studio": progetto originale con i comandi del pedale di riferimento (lo
    schema non è pubblico, quindi non è la replica di un circuito). Struttura:
      ingresso (INPUT) -> VCA -> MIX con il pulito -> TONE (tilt a 500 Hz) -> OUTPUT
    Rivelatore in retroazione (guarda l'uscita del VCA, come il pedale), raddrizzatore a doppia
    semionda, legge in dB con soglia netta e RATIO 2:1 / 4:1 / 10:1, ATTACK 50 µs - 50 ms e
    RELEASE 50 ms - 1 s sulla riduzione di guadagno. La retroazione è risolta in forma implicita a
    ogni campione (stabile anche con attacco velocissimo e 10:1). SC HPF: passa-alto a 100 Hz solo
    sul rivelatore. Valori, fonti e scelte: nota di lavoro locale. */
class VcaCompressor final : public PedalProcessor
{
public:
    VcaCompressor();

    static Info getInfo();

    /** Riduzione di guadagno attuale in dB (per le prove e un eventuale misuratore). */
    float getGainReductionDb() const noexcept   { return gainReductionDb.load(); }
    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    /** Biquad (forma diretta II trasposta). */
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

    void updateTone (double tiltDb);

    BuiltInParameter* input = nullptr;
    BuiltInParameter* output = nullptr;
    BuiltInParameter* attack = nullptr;
    BuiltInParameter* release = nullptr;
    BuiltInParameter* mix = nullptr;
    BuiltInParameter* tone = nullptr;
    BuiltInParameter* ratio = nullptr;
    BuiltInParameter* sidechainFilter = nullptr;
    juce::SmoothedValue<float> inputSmoothed, outputSmoothed, mixSmoothed, toneSmoothed;

    Biquad sidechainHighPass, tilt;
    double sampleRate = 48000.0, reduction = 0.0, lastTilt = 1e9;
    int untilToneUpdate = 0;

    std::atomic<float> gainReductionDb { 0.0f };
    double maxReduction = 0.0;
};
