#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

//==============================================================================
/** Linea di ritardo a "secchielli" (BBD, es. MN3007): N stadi spinti da un clock a due fasi danno un
    ritardo N / (2 f_clock). Qui: memoria circolare alla frequenza di lavoro, letta con interpolazione
    cubica (Hermite) al ritardo che segue il clock. I filtri anti-aliasing e di ricostruzione del
    pedale stanno fuori (CircuitModel) e tolgono le immagini del clock, che qui non nascono. */
class BbdDelay
{
public:
    void prepare (double sampleRate, double maxDelaySeconds, int stagesCount)
    {
        rate = sampleRate;
        stages = stagesCount;
        buffer.assign ((size_t) juce::nextPowerOfTwo ((int) std::ceil (maxDelaySeconds * sampleRate) + 8), 0.0f);
        mask = (int) buffer.size() - 1;
        write = 0;
    }

    void reset()  { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    /** Ritardo per una frequenza di clock (Hz). */
    double delayForClock (double clockHz) const noexcept   { return (double) stages / (2.0 * clockHz); }

    /** Scrive un campione e legge quello di `delaySeconds` fa. */
    float process (float input, double delaySeconds) noexcept
    {
        buffer[(size_t) write] = input;

        const double d = juce::jlimit (2.0, (double) mask - 4.0, delaySeconds * rate);
        const int whole = (int) d;
        const float t = (float) (d - whole);
        const auto at = [this] (int back) { return buffer[(size_t) ((write - back) & mask)]; };

        // Hermite su 4 punti attorno alla posizione (indietro nel tempo = indice maggiore)
        const float y0 = at (whole - 1), y1 = at (whole), y2 = at (whole + 1), y3 = at (whole + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        write = (write + 1) & mask;
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

private:
    std::vector<float> buffer;
    int mask = 0, write = 0, stages = 1024;
    double rate = 48000.0;
};

//==============================================================================
/** LFO a triangolo come nei pedali: integratore + comparatore a isteresi. L'uscita va da -1 a +1
    (l'ampiezza in volt la applica chi lo usa); la pendenza è costante, la frequenza si cambia senza salti. */
class TriangleLfo
{
public:
    void prepare (double sampleRate)   { rate = sampleRate; }
    void reset (double phaseValue = 0.0)  { value = phaseValue; rising = true; }

    double process (double frequencyHz) noexcept
    {
        const double step = 4.0 * frequencyHz / rate;   // da -1 a +1 in mezzo periodo
        value += rising ? step : -step;
        if (value >= 1.0) { value = 2.0 - value; rising = false; }
        if (value <= -1.0) { value = -2.0 - value; rising = true; }
        return value;
    }

private:
    double rate = 48000.0, value = 0.0;
    bool rising = true;
};
