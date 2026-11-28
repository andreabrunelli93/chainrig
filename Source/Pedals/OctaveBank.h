#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
/** Generatore d'ottava polifonico a banco di filtri (progetto originale).

    Il segnale passa in circa 60 filtri passa-banda complessi (tre poli complessi in cascata, risposta
    solo sulle frequenze positive: l'uscita di ogni banda è un segnale analitico z = |z| e^{jφ}).
    Con una nota per banda si lavora sulla fase:
      - prima si toglie la fase introdotta dalla banda stessa, calcolata in forma chiusa dalla frequenza
        istantanea (mediata su 2 ms): così tutte le bande che vedono la stessa nota hanno la stessa fase
        e le loro ottave si sommano invece di cancellarsi;
      - ottava sopra:  |z| e^{j2ψ};
      - ottava sotto:  |z| e^{jψ/2}: radice principale del fasore, con il segno scelto ogni campione in
                       modo da restare continuo con il campione prima, e allineato tra bande vicine che
                       seguono la stessa nota (la radice ha due soluzioni: bande con segni opposti si
                       cancellerebbero);
    e si sommano le parti reali, ciascuna pesata da quanto la banda è un massimo rispetto alle vicine.
    Un accordo funziona perché le note finiscono in bande diverse; dove due note o due armoniche cadono
    nella stessa banda nasce intermodulazione (misurata: vedi minWidth).

    Bande: larghezza max(25 Hz, 6% della frequenza; sopra 1 kHz fino al 12% a 3 kHz), passo 3/4 della
    larghezza, da 50 Hz a 5 kHz.
    Attacco (fino a -3 dB): ottava sotto 5-9 ms, ottava sopra 9-14 ms. Guadagni delle bande scelti perché
    la somma delle potenze sia piatta. */
class OctaveBank
{
public:
    void prepare (double rate)
    {
        sampleRate = rate;
        bands.clear();

        for (double f = 50.0; f < juce::jmin (5000.0, 0.4 * rate); )
        {
            // sopra 1 kHz ogni banda contiene comunque più armoniche: bande più larghe (12% a 3 kHz), meno CPU
            const double width = juce::jmax (minWidth, relWidth * f, 2.0 * relWidth * f - 60.0);
            Band b;
            b.centre = f;
            const double halfWidth = juce::MathConstants<double>::pi * width / rate;   // metà banda, radianti
            b.r = 1.0 - juce::jlimit (1e-4, 0.5, halfWidth / 0.5098);                    // 3 poli: -3 dB totali
            bands.push_back (b);
            f += 0.75 * width;
        }
        normaliseGains();

        // stato in array separati (una voce per banda): il ciclo principale non ha salti e il compilatore
        // lo esegue su più bande insieme (SIMD). Prima, con una struttura per banda: 3,7% del blocco
        count = (int) bands.size();
        for (auto* v : { &ar, &ai, &input, &inputGain, &downWeight, &tolerance })
            v->assign ((size_t) count, 0.0f);
        for (int k = 0; k < count; ++k)
        {
            const auto& b = bands[(size_t) k];
            const double w = juce::MathConstants<double>::twoPi * b.centre / rate;
            ar[(size_t) k] = (float) (b.r * std::cos (w));
            ai[(size_t) k] = (float) (b.r * std::sin (w));
            input[(size_t) k] = (float) (1.0 - b.r);
            inputGain[(size_t) k] = (float) ((1.0 - b.r) * b.gain);

            // ottava sotto solo fino a ~1,2 kHz (sfuma fino a 2 kHz): sopra, in ogni banda ci sono più
            // armoniche e dimezzarne la fase dà solo rumore (dente di sega a 110 Hz: -66 dB -> -33 dB)
            const double t = juce::jlimit (0.0, 1.0, (b.centre - 1200.0) / 800.0);
            downWeight[(size_t) k] = (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * t));

            // stessa nota: frequenze istantanee entro il 5% del centro della banda (rad/campione), meno di
            // un semitono (6%). Con il 2% all'attacco delle note basse le bande restavano disallineate per
            // ~30 ms (-9 dB), finché i transitori dei filtri stretti non si spegnevano
            tolerance[(size_t) k] = (float) (0.05 * w);
        }

        freqSmoothing = (float) (1.0 - std::exp (-1.0 / (smoothingSeconds * rate)));
        reset();
    }

    void reset()
    {
        for (auto* v : { &y0r, &y0i, &y1r, &y1i, &y2r, &y2i, &lastR, &lastI, &qr, &qi, &pi, &up, &down })
            v->assign ((size_t) count, 0.0f);
        pr.assign ((size_t) count, 1.0f);
        mag.assign ((size_t) count + 2, 0.0f);   // con uno zero ai due capi (vicine delle bande estreme)
    }

    /** Un campione: restituisce ottava sotto e ottava sopra. */
    void process (float x, float& outDown, float& outUp) noexcept
    {
        const float fs = freqSmoothing;
        float* m_ = mag.data() + 1;
        const float* __restrict ar_ = ar.data();
        const float* __restrict ai_ = ai.data();
        const float* __restrict input_ = input.data();
        const float* __restrict inputGain_ = inputGain.data();
        const float* __restrict downWeight_ = downWeight.data();
        float* __restrict y0r_ = y0r.data();
        float* __restrict y0i_ = y0i.data();
        float* __restrict y1r_ = y1r.data();
        float* __restrict y1i_ = y1i.data();
        float* __restrict y2r_ = y2r.data();
        float* __restrict y2i_ = y2i.data();
        float* __restrict lastR_ = lastR.data();
        float* __restrict lastI_ = lastI.data();
        float* __restrict qr_ = qr.data();
        float* __restrict qi_ = qi.data();
        float* __restrict pr_ = pr.data();
        float* __restrict pi_ = pi.data();
        float* __restrict up_ = up.data();
        float* __restrict down_ = down.data();

        float* __restrict mk = m_;
        // array tutti distinti: si può vettorizzare (senza questa indicazione clang non lo dimostra da solo)
       #if defined (__clang__)
        #pragma clang loop vectorize(assume_safety)
       #elif defined (_MSC_VER)
        #pragma loop(ivdep)
       #endif
        for (int k = 0; k < count; ++k)
        {
            // tre poli complessi in cascata
            const float a_r = ar_[k], a_i = ai_[k], g = input_[k];
            float r0 = a_r * y0r_[k] - a_i * y0i_[k] + inputGain_[k] * x;
            float i0 = a_r * y0i_[k] + a_i * y0r_[k];
            y0r_[k] = r0; y0i_[k] = i0;
            float r1 = a_r * y1r_[k] - a_i * y1i_[k] + g * r0;
            float i1 = a_r * y1i_[k] + a_i * y1r_[k] + g * i0;
            y1r_[k] = r1; y1i_[k] = i1;
            const float zr = a_r * y2r_[k] - a_i * y2i_[k] + g * r1;
            const float zi = a_r * y2i_[k] + a_i * y2r_[k] + g * i1;
            y2r_[k] = zr; y2i_[k] = zi;

            // z · conj(z prima), mediato su 2 ms (pesato con la potenza): con due armoniche nella stessa
            // banda la frequenza istantanea salta e la correzione di fase diventerebbe rumore
            const float q_r = qr_[k] + fs * ((zr * lastR_[k] + zi * lastI_[k]) - qr_[k]);
            const float q_i = qi_[k] + fs * ((zi * lastR_[k] - zr * lastI_[k]) - qi_[k]);
            qr_[k] = q_r; qi_[k] = q_i;
            lastR_[k] = zr; lastI_[k] = zi;

            const float m2 = zr * zr + zi * zi + 1e-30f;
            const float invM = 1.0f / std::sqrt (m2);
            const float m = m2 * invM;
            mk[k] = m;

            // fase della banda tolta prima di raddoppiare o dimezzare: una nota tra due bande arriva con
            // fasi diverse nelle due (fino a ~120°), e raddoppiandole le due ottave si cancellavano
            // (-11/-15 dB misurati). Con la frequenza istantanea v (fasore di q) la fase di tre poli è
            // nota: e^{-jθ} = (D / |D|)^3, D = 1 - conj(v) · a
            const float qInv = 1.0f / std::sqrt (q_r * q_r + q_i * q_i + 1e-30f);
            const float vr = q_r * qInv, vi = -q_i * qInv;   // conj(v)
            const float dr = 1.0f - (vr * a_r - vi * a_i), di = -(vr * a_i + vi * a_r);
            const float dInv = 1.0f / std::sqrt (dr * dr + di * di + 1e-30f);
            const float cr = dr * dInv, ci = di * dInv;
            const float c2r = cr * cr - ci * ci, c2i = 2.0f * cr * ci;
            const float c3r = c2r * cr - c2i * ci, c3i = c2r * ci + c2i * cr;
            const float u0r = zr * invM, u0i = zi * invM;
            const float ur = u0r * c3r - u0i * c3i, ui = u0r * c3i + u0i * c3r;

            up_[k] = m * (ur * ur - ui * ui);

            // radice del fasore u: (u + 1) / |u + 1|, col segno che la rende continua; vicino a u = -1
            // la radice è ±j e si tiene quella più vicina alla precedente
            const float p_r = pr_[k], p_i = pi_[k];
            float wr = ur + 1.0f, wi = ui;
            const float n2 = wr * wr + wi * wi;
            const float nInv = 1.0f / std::sqrt (n2 + 1e-30f);
            const bool valid = n2 > 1e-12f;
            wr = valid ? wr * nInv : 0.0f;
            wi = valid ? wi * nInv : (p_i >= 0.0f ? 1.0f : -1.0f);
            const float sign = (wr * p_r + wi * p_i) < 0.0f ? -1.0f : 1.0f;
            wr *= sign;
            wi *= sign;
            pr_[k] = wr; pi_[k] = wi;
            down_[k] = m * wr * downWeight_[k];
        }

        // segno dell'ottava sotto tra bande: la radice ha due soluzioni e ogni banda ne sceglie una per
        // conto suo. Due bande vicine che portano la stessa nota (stessa frequenza istantanea) con segni
        // opposti si cancellano (buco di -24 dB misurato a 117 Hz): la seconda prende il segno della prima.
        // Succede all'attacco della nota, poi restano allineate perché seguono la stessa fase
        for (int k = 1; k < count; ++k)
        {
            if (m_[k] < 1e-9f || m_[k - 1] < 1e-9f)
                continue;
            const float cr = qr[(size_t) k] * qr[(size_t) k - 1] + qi[(size_t) k] * qi[(size_t) k - 1];
            const float ci = qi[(size_t) k] * qr[(size_t) k - 1] - qr[(size_t) k] * qi[(size_t) k - 1];
            if (cr > 0.0f && std::abs (ci) < tolerance[(size_t) k] * cr
                && pr[(size_t) k] * pr[(size_t) k - 1] + pi[(size_t) k] * pi[(size_t) k - 1] < 0.0f)
            {
                pr[(size_t) k] = -pr[(size_t) k];
                pi[(size_t) k] = -pi[(size_t) k];
                down[(size_t) k] = -down[(size_t) k];
            }
        }

        // peso di picco: una banda conta tanto più quanto è un massimo rispetto alle vicine (una banda tra
        // due note le vede entrambe e le sue ottave sarebbero soprattutto intermodulazione)
        float sumDown = 0.0f, sumUp = 0.0f;
        for (int k = 0; k < count; ++k)
        {
            const float m = m_[k];
            const float w = m / std::max (m, std::max (m_[k - 1], m_[k + 1]));
            sumUp += w * up[(size_t) k];
            sumDown += w * down[(size_t) k];
        }

        outDown = sumDown * levelCompensation;
        outUp = sumUp * levelCompensation;
    }

    int getNumBands() const noexcept   { return count; }

    /** Diagnostica: potenza della somma delle bande (dB) a una frequenza; piatta = guadagni giusti. */
    double powerResponseDb (double f) const
    {
        return 10.0 * std::log10 (power (juce::MathConstants<double>::twoPi * f / sampleRate) + 1e-30);
    }

private:
    struct Band { double centre = 0, r = 0, gain = 1; };   // progetto (prepare)

    double bandPower (const Band& b, double w) const
    {
        const double d = w - juce::MathConstants<double>::twoPi * b.centre / sampleRate;
        const double one = (1.0 - b.r) * (1.0 - b.r) / (1.0 - 2.0 * b.r * std::cos (d) + b.r * b.r);
        return b.gain * b.gain * one * one * one;
    }

    double power (double w) const
    {
        double p = 0.0;
        for (const auto& b : bands)
            p += bandPower (b, w);
        return p;
    }

    void normaliseGains()
    {
        for (int iteration = 0; iteration < 40; ++iteration)
        {
            std::vector<double> p;
            for (const auto& b : bands)
                p.push_back (power (juce::MathConstants<double>::twoPi * b.centre / sampleRate));
            for (size_t k = 0; k < bands.size(); ++k)
                bands[k].gain /= std::pow (p[k], 0.25);
        }
    }

    // larghezza delle bande: scelta misurando due note insieme (intermodulazione dell'ottava sopra:
    // quinta 196+294 Hz -26 dB con 25 Hz / 6%, -5 dB con 50 Hz / 12%) contro latenza e CPU
    static constexpr double minWidth = 25.0, relWidth = 0.06;
    static constexpr double smoothingSeconds = 0.002;   // 8 ms: ottava sopra a -3 dB dopo 20-25 ms dall'attacco; 2 ms: 9-14 ms
    static constexpr float levelCompensation = 1.38f;   // +2,8 dB: ottave misurate a -2,8 dB ±0,5 da 82 a 1319 Hz

    double sampleRate = 48000.0;
    float freqSmoothing = 0.01f;
    int count = 0;
    std::vector<Band> bands;
    std::vector<float> ar, ai, input, inputGain, downWeight, tolerance,
                       y0r, y0i, y1r, y1i, y2r, y2i, lastR, lastI, qr, qi, pr, pi, up, down, mag;
};
