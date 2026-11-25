#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>
#include <vector>

//==============================================================================
/** Primo nodo della catena: lascia passare l'audio, ne copia una parte per
    l'accordatore e, quando l'accordatore è attivo, ammutolisce l'uscita
    con una rampa (niente click).

    Il thread audio scrive solo in una FIFO lock-free; l'analisi del pitch
    avviene nel thread dell'interfaccia. */
class TunerProcessor final : public juce::AudioProcessor
{
public:
    TunerProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Out", juce::AudioChannelSet::stereo(), true))
    {}

    std::atomic<bool> muted { false };      // uscita silenziata (accordatore aperto)
    std::atomic<bool> capturing { false };  // copia i campioni per l'analisi

    double getSampleRateForAnalysis() const noexcept { return currentRate.load(); }

    /** Thread dell'interfaccia: preleva fino a maxSamples campioni. */
    int pull (float* dest, int maxSamples)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxSamples, s1, n1, s2, n2);
        if (n1 > 0) std::copy_n (fifoData.data() + s1, n1, dest);
        if (n2 > 0) std::copy_n (fifoData.data() + s2, n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    //==============================================================================
    const juce::String getName() const override              { return "Tuner"; }
    void prepareToPlay (double sr, int) override             { currentRate.store (sr); }
    void releaseResources() override                         {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in = l.getMainInputChannelSet();
        return in == l.getMainOutputChannelSet() && ! in.isDisabled() && in.size() <= 2;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();

        if (capturing.load (std::memory_order_relaxed) && buffer.getNumChannels() > 0)
        {
            int s1, n1, s2, n2;
            fifo.prepareToWrite (n, s1, n1, s2, n2);   // se la FIFO è piena scarta: va bene
            const float* src = buffer.getReadPointer (0);
            if (n1 > 0) std::copy_n (src, n1, fifoData.data() + s1);
            if (n2 > 0) std::copy_n (src + n1, n2, fifoData.data() + s2);
            fifo.finishedWrite (n1 + n2);
        }

        const float target = muted.load (std::memory_order_relaxed) ? 0.0f : 1.0f;
        if (! juce::exactlyEqual (target, lastGain) || juce::exactlyEqual (target, 0.0f))
            buffer.applyGainRamp (0, n, lastGain, target);
        lastGain = target;
    }

    double getTailLengthSeconds() const override             { return 0.0; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    juce::AudioProcessorEditor* createEditor() override      { return nullptr; }
    bool hasEditor() const override                          { return false; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override   {}
    void setStateInformation (const void*, int) override     {}

private:
    static constexpr int fifoSize = 32768;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData = std::vector<float> ((size_t) fifoSize, 0.0f);
    std::atomic<double> currentRate { 48000.0 };
    float lastGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerProcessor)
};

//==============================================================================
/** Rilevamento della fondamentale con l'algoritmo YIN.
    Restituisce la frequenza in Hz oppure 0 se il segnale è troppo debole o incerto. */
inline float detectPitchYin (const float* x, int numSamples, double sampleRate)
{
    const int W = numSamples / 2;
    const int maxTau = juce::jmin (W - 1, (int) (sampleRate / 60.0));     // ~ fino al Si basso
    const int minTau = juce::jmax (2, (int) (sampleRate / 1500.0));

    if (maxTau <= minTau)
        return 0.0f;

    // Soglia di volume: niente analisi su rumore/silenzio
    double energy = 0.0;
    for (int i = 0; i < numSamples; ++i)
        energy += (double) x[i] * x[i];
    if (std::sqrt (energy / numSamples) < 0.002)
        return 0.0f;

    std::vector<float> d ((size_t) maxTau + 1, 0.0f);

    for (int tau = 1; tau <= maxTau; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < W; ++j)
        {
            const float delta = x[j] - x[j + tau];
            sum += delta * delta;
        }
        d[(size_t) tau] = sum;
    }

    // Differenza normalizzata cumulativa
    d[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        running += d[(size_t) tau];
        d[(size_t) tau] = running > 0.0f ? d[(size_t) tau] * (float) tau / running : 1.0f;
    }

    constexpr float threshold = 0.12f;
    int best = -1;

    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        if (d[(size_t) tau] < threshold)
        {
            while (tau + 1 <= maxTau && d[(size_t) tau + 1] < d[(size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
        return 0.0f;

    // Interpolazione parabolica per precisione sotto il campione
    float refined = (float) best;
    if (best > 1 && best < maxTau)
    {
        const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        if (std::abs (denom) > 1.0e-9f)
            refined += 0.5f * (a - c) / denom;
    }

    return (float) (sampleRate / refined);
}
