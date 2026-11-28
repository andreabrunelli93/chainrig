#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <cmath>
#include <vector>

//==============================================================================
/** Pitch shifter a frammenti su una linea di ritardo (progetto originale, nessun codice altrui).

    Un "tap" legge la linea con passo `ratio` (2 = un'ottava sopra): il suo ritardo cambia di
    (1 - ratio) campioni per campione. Quando sta per uscire dall'intervallo [minDelay, maxDelay]
    nasce un tap nuovo dall'altra parte dell'intervallo e i due si incrociano (coseno rialzato).
    Il punto di partenza del tap nuovo si sceglie cercando dove la forma d'onda combacia con quella
    del tap vecchio (correlazione normalizzata, come nel SOLA): prima grossolana su una copia
    sottocampionata di 4, poi fine a piena frequenza. Così le giunture cadono "in fase" e non si
    sente il ronzio a frequenza fissa dei pitch shifter a finestre fisse; non serve un rilevatore
    dell'altezza e con gli accordi funziona comunque in modo accettabile.

    ratio = 1: il tap non si muove (ritardo puro, nessuna modulazione); se il ritardo è grande,
    un solo incrocio lo riporta vicino al minimo, così il pedale a riposo ha poca latenza.
    Prima della linea un passa-basso (Butterworth 4° ordine) a 0,45 fs / ratio evita che leggere
    più veloce ripieghi gli acuti. */
class GrainShifter
{
public:
    void prepare (double rate)
    {
        sampleRate = rate;
        minDelay = 0.0012 * rate;
        maxDelay = 0.030 * rate;
        search   = std::ceil (0.0125 * rate);          // un periodo del Mi basso (82 Hz) e poco più
        maxFade  = 0.006 * rate;
        fineWindow = juce::jmax (32, (int) (0.005 * rate));
        coarseWindow = juce::jmax (16, (int) (0.008 * rate) / decimation);

        const int needed = (int) (maxDelay + search + 0.010 * rate) + fineWindow + 16;
        size = juce::nextPowerOfTwo (needed);
        mask = size - 1;
        coarseSize = size / decimation;
        coarseMask = coarseSize - 1;
        buffer.assign ((size_t) size, 0.0f);
        coarse.assign ((size_t) coarseSize, 0.0f);

        // passa-basso per la copia sottocampionata: 2,5 kHz fisso (l'ottava bassa della chitarra conta)
        decimationFilter.setLowPass (2500.0 / rate, 0.7071);
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        std::fill (coarse.begin(), coarse.end(), 0.0f);
        writePos = 0;
        taps[0] = { (double) -minDelay - 0.5 * search };
        current = 0;
        fading = false;
        fadeCount = 0;
        lastFilterRatio = -1.0;
        for (auto* f : { &antiAlias1, &antiAlias2, &decimationFilter })
            f->clear();
    }

    /** Un campione. ratio = rapporto di frequenza (0,125 … 4). */
    float process (float input, double ratio) noexcept
    {
        updateAntiAlias (ratio);
        const float filtered = (float) antiAlias2.process (antiAlias1.process (input));

        buffer[(size_t) (writePos & mask)] = filtered;
        const double low = decimationFilter.process (input);
        if ((writePos & (decimation - 1)) == 0)
            coarse[(size_t) ((writePos / decimation) & coarseMask)] = (float) low;

        const double now = (double) writePos;
        auto& cur = taps[(size_t) current];
        const double delay = now - cur.pos;
        const double slope = 1.0 - ratio;    // variazione del ritardo per campione

        if (! fading)
        {
            // vita minima di un tap nuovo (parte al più vicino possibile al bordo da cui si allontana):
            // entrata e uscita devono starci entrambe, altrimenti il tap vecchio arriva al punto di
            // scrittura prima della fine dell'incrocio e per un attimo si legge alla velocità originale
            double remaining = 1e12, shortestLife = 1e12;
            if (slope < -1e-9)       { remaining = (delay - minDelay) / -slope;  shortestLife = (maxDelay - search - minDelay) / -slope; }
            else if (slope > 1e-9)   { remaining = (maxDelay - delay) / slope;   shortestLife = (maxDelay - minDelay - search) / slope; }

            const double wanted = juce::jlimit (16.0, maxFade, 0.45 * shortestLife);
            const bool unisonTooLate = std::abs (slope) <= 1e-9 && delay > minDelay + search + 1.0;

            if (remaining <= wanted || unisonTooLate)
            {
                fadeLength = juce::jlimit (8.0, maxFade, remaining);   // finisce quando il tap vecchio arriva al bordo
                startFade (cur.pos, slope < -1e-9);
            }
        }

        float out;
        if (fading)
        {
            const double g = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * fadeCount / fadeLength);
            const auto& next = taps[(size_t) (1 - current)];
            out = (float) ((1.0 - g) * read (cur.pos) + g * read (next.pos));
            if (++fadeCount >= fadeLength)
            {
                current = 1 - current;
                fading = false;
            }
        }
        else
        {
            out = read (cur.pos);
        }

        for (auto& t : taps)
            t.pos = juce::jlimit (now + 1.0 - (double) (size - fineWindow - 8), now - 3.0, t.pos + ratio);

        ++writePos;
        return out;
    }

    int splices = 0;   // diagnostica

private:
    struct Tap { double pos = 0.0; };   // posizione di lettura assoluta (campioni)

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void setLowPass (double normalisedFreq, double q)
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jlimit (1e-4, 0.49, normalisedFreq);
            const double alpha = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + alpha;
            b0 = b2 = (1.0 - c) * 0.5 / a0;
            b1 = (1.0 - c) / a0;
            a1 = -2.0 * c / a0;
            a2 = (1.0 - alpha) / a0;
        }
        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0; }
    };

    void updateAntiAlias (double ratio) noexcept
    {
        if (std::abs (ratio - lastFilterRatio) < 0.01 * ratio)
            return;
        lastFilterRatio = ratio;
        const double cutoff = juce::jmin (18000.0, 0.45 * sampleRate / juce::jmax (1.0, ratio)) / sampleRate;
        antiAlias1.setLowPass (cutoff, 0.5412);   // Butterworth 4° ordine = due biquad
        antiAlias2.setLowPass (cutoff, 1.3066);
    }

    float sample (int64_t i) const noexcept   { return buffer[(size_t) (i & mask)]; }

    float read (double pos) const noexcept   // Hermite a 4 punti
    {
        const auto i = (int64_t) std::floor (pos);
        const float t = (float) (pos - (double) i);
        const float xm1 = sample (i - 1), x0 = sample (i), x1 = sample (i + 1), x2 = sample (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    /** Nuovo tap dall'altra parte dell'intervallo, nel punto in cui combacia con quello attuale.
        towardsOld = true (si sale di altezza): il tap nuovo parte da un ritardo grande. */
    void startFade (double fromPos, bool towardsOld) noexcept
    {
        const double now = (double) writePos;
        const double fromDelay = now - fromPos;
        const double lo = towardsOld ? maxDelay - search : minDelay;
        const double hi = towardsOld ? maxDelay : minDelay + search;

        // spostamento D (intero) del tap nuovo rispetto al vecchio: pos nuovo = fromPos - D
        const int dLo = (int) std::ceil (lo - fromDelay), dHi = (int) std::floor (hi - fromDelay);

        // 1) grossolana sulla copia sottocampionata (passi di 4 campioni)
        const auto base = (int64_t) std::floor (fromPos / decimation);
        int best = (dLo + dHi) / 2;
        double bestScore = -1e30;
        for (int d = dLo - (dLo % decimation); d <= dHi; d += decimation)
        {
            if (d < dLo) continue;
            const int dd = d / decimation;
            double c = 0.0, e = 1e-12;
            for (int j = 1; j <= coarseWindow; ++j)
            {
                const double a = coarse[(size_t) ((base - j) & coarseMask)];
                const double b = coarse[(size_t) ((base - dd - j) & coarseMask)];
                c += a * b;
                e += b * b;
            }
            const double score = c / std::sqrt (e);
            if (score > bestScore) { bestScore = score; best = d; }
        }

        // 2) fine a piena frequenza intorno al migliore, poi frazione di campione con una parabola sul
        //    picco: a 2 ottave sopra ci sono più di 200 giunture al secondo e mezzo campione di errore
        //    (1,2° a 330 Hz) diventava una modulazione di fase udibile (residuo -38 dB su una sinusoide)
        const auto i0 = (int64_t) std::floor (fromPos);
        auto correlation = [&] (int d)
        {
            double c = 0.0, e = 1e-12;
            for (int j = 1; j <= fineWindow; ++j)
            {
                const double a = sample (i0 - j), b = sample (i0 - d - j);
                c += a * b;
                e += b * b;
            }
            return c / std::sqrt (e);
        };

        int fine = best;
        double fineScore = -1e30;
        for (int d = juce::jmax (dLo, best - decimation + 1); d <= juce::jmin (dHi, best + decimation - 1); ++d)
        {
            const double score = correlation (d);
            if (score > fineScore) { fineScore = score; fine = d; }
        }

        double offset = (double) fine;
        if (fine > dLo && fine < dHi)
        {
            const double before = correlation (fine - 1), after = correlation (fine + 1);
            const double curvature = before - 2.0 * fineScore + after;
            if (curvature < 0.0)
                offset += juce::jlimit (-0.5, 0.5, 0.5 * (before - after) / curvature);
        }

        taps[(size_t) (1 - current)].pos = fromPos - offset;
        fading = true;
        fadeCount = 0;
        ++splices;
    }

    static constexpr int decimation = 4;

    double sampleRate = 48000.0, minDelay = 0, maxDelay = 0, search = 0, maxFade = 0, fadeLength = 1, lastFilterRatio = -1;
    int fineWindow = 256, coarseWindow = 96, size = 0, mask = 0, coarseSize = 0, coarseMask = 0;
    std::vector<float> buffer, coarse;
    int64_t writePos = 0;
    std::array<Tap, 2> taps;
    int current = 0, fadeCount = 0;
    bool fading = false;
    Biquad antiAlias1, antiAlias2, decimationFilter;
};
