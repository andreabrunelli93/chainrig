#pragma once

#include "PedalProcessor.h"
#include "CircuitModel.h"

//==============================================================================
/** Distorsione a quattro stadi di op-amp con diodi verso massa, tone stack passivo e controllo del
    "contour" (taglio dei medi), copiata dalla netlist (`.claude/circuits/shredmaster.cir`):
      A  IC1A non invertente: GAIN (100k lineare) con il cursore sull'uscita, una parte nella
         retroazione (2x … 32x sopra ~1 kHz) e il resto in serie allo stadio dopo;
      B  IC1B invertente, -680k / (10k + resto del GAIN) (6x … 68x), 47 pF;
      C  10k + coppia di 1N914 verso massa, tone stack BASS (100k log a reostato) / TREBLE (25k
         lineare), IC2A non invertente (+15 dB sotto ~330 Hz);
      D  CONTOUR (T a ponte, 100k lineare) e IC2B invertente con 100k || 1 nF (passa-basso 1,6 kHz:
         il pedale originale è scuro);
      E  VOLUME (100k log), 220 nF, 1M.
    Ogni uscita di op-amp satura sui rail (TL072 a 9 V: circa ±3 V intorno a Vref). */
class OpAmpDistortion final : public PedalProcessor
{
public:
    OpAmpDistortion();

    static Info getInfo();
    static constexpr double inputVolts = 2.5;

    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    void applyKnobs (bool force);

    BuiltInParameter* gain = nullptr;
    BuiltInParameter* bass = nullptr;
    BuiltInParameter* contour = nullptr;
    BuiltInParameter* treble = nullptr;
    BuiltInParameter* volume = nullptr;
    juce::SmoothedValue<float> gainSmoothed, bassSmoothed, contourSmoothed, trebleSmoothed, volumeSmoothed;

    CircuitModel<3, 1, 1> stageA;     // ingresso e IC1A
    CircuitModel<2, 1, 1> stageB;     // resto del GAIN, C4, R5, IC1B
    CircuitModel<6, 1, 1> stageC;     // diodi, tone stack, IC2A
    CircuitModel<5, 1, 1> stageD;     // contour, IC2B
    CircuitModel<1, 1, 1> stageE;     // volume e uscita

    int gainA = 0, gainB = 0, trebleTop = 0, trebleBottom = 0, bassR = 0, contourA = 0, contourB = 0,
        volumeA = 0, volumeB = 0, untilUpdate = 0;
    double newtonIterations = 0.0;
    juce::int64 newtonSamples = 0;
};
