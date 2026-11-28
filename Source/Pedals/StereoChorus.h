#pragma once

#include "PedalProcessor.h"
#include "CircuitModel.h"
#include "BbdDelay.h"
#include "Compander.h"

//==============================================================================
/** Chorus stereo a BBD con compander (modo chorus): asciutto su entrambe le uscite, bagnato in
    controfase tra sinistra e destra. Percorso bagnato: guadagno d'ingresso, filtro, compressore 2:1,
    anti-aliasing, BBD 1024 stadi (con il suo rumore e il suo limite di livello), ricostruzione,
    espansore 1:2, INTENSITY. Clock da un VCO lineare comandato da un LFO a comparatori (tratti
    esponenziali) attraverso WIDTH e un filtro RC.
    Valori, fonti e scelte: nota di lavoro locale del circuito. */
class StereoChorus final : public PedalProcessor
{
public:
    StereoChorus();

    static Info getInfo();
    static constexpr double inputVolts = 2.5;

    juce::String getDiagnostics() const override;

protected:
    void prepareCircuit (double rate) override;
    void processCircuit (float* samples, int n) override;
    void processCircuitStereo (float* left, float* right, int n) override;

private:
    BuiltInParameter* speed = nullptr;
    BuiltInParameter* width = nullptr;
    BuiltInParameter* intensity = nullptr;
    BuiltInParameter* gain = nullptr;
    BuiltInParameter* stereo = nullptr;   // spento: le due uscite uguali (asciutto + bagnato), sicuro in una catena mono
    juce::SmoothedValue<float> speedSmoothed, widthSmoothed, intensitySmoothed, gainSmoothed;

    CircuitModel<2, 1, 1> input;     // ingresso e buffer
    CircuitModel<2, 1, 1> preFilter; // filtro prima del compressore (Sallen-Key)
    CircuitModel<2, 1, 1> antiAlias; // Sallen-Key prima del BBD
    CircuitModel<3, 1, 1> recon;     // Sallen-Key + RC dopo il BBD
    CompanderSection compressor, expander;
    BbdDelay bbd;

    double sampleRate = 48000.0, lfoValue = -1.0, lfoTarget = 2.0, control = 0.0;
    juce::Random noise { 1976 };
    int path = 0;   // solo prove (CHAINRIG_LAB_PATH): 1 asciutto, 2 bagnato, 3 bagnato fermo, 4 bagnato fermo lineare
    double minDelay = 1.0, maxDelay = 0.0;
};
