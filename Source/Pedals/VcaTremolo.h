#pragma once

#include "PedalProcessor.h"

//==============================================================================
/** Tremolo a VCA con LFO a triangolo "squadrabile", dallo schema di fabbrica del pedale di
    riferimento: buffer a JFET -> VCA (amplificatore a transconduttanza, uscita in corrente) ->
    convertitore corrente-tensione invertente -> uscita. Il percorso audio è lineare: tutto il
    carattere è nel segnale di comando:
      - LFO: integratore + comparatore (triangolo), periodo 90-900 ms (specifiche del costruttore),
        RATE 100k lineare in serie a una resistenza fissa;
      - WAVE (100k logaritmico): guadagno di uno stadio che amplifica il triangolo fino a tosarlo
        sui rail -> trapezio -> quasi quadra;
      - DEPTH (100k lineare): quanto il guadagno scende nel minimo dell'LFO;
      - R8 100k x C4 0,1 µF sul comando del VCA: arrotonda i fronti (niente clic con l'onda quadra). */
class VcaTremolo final : public PedalProcessor
{
public:
    VcaTremolo();

    static Info getInfo();

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;

private:
    BuiltInParameter* rate = nullptr;
    BuiltInParameter* wave = nullptr;
    BuiltInParameter* depth = nullptr;
    juce::SmoothedValue<float> rateSmoothed, waveSmoothed, depthSmoothed;

    double sampleRate = 48000.0, triangle = -1.0, direction = 1.0, control = 1.0, controlCoeff = 0.0;
};
