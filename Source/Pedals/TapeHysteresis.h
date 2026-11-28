#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

//==============================================================================
/** Magnetizzazione del nastro (Jiles-Atherton) risolta con Runge-Kutta 4, H lineare nel passo.
    Equazioni e parametri: Chowdhury, "Real-time physical modelling for analog tape machines",
    DAFx 2019 (scritto da zero dall'articolo). Il bias di registrazione non è simulato come segnale:
    lo rappresenta c (parte reversibile), c -> 1 = bias ideale (curva anisteretica).
    Usato dalla macchina a nastro e dall'eco a nastro. */
class TapeHysteresis
{
public:
    static constexpr double Ms = 3.5e5;      // magnetizzazione di saturazione
    static constexpr double aShape = 2.2e4;  // forma della curva anisteretica
    static constexpr double alpha = 1.6e-3;  // accoppiamento tra domini
    static constexpr double kPin = 2.7e4;    // larghezza del ciclo (pinning)

    /** Pendenza a piccolo segnale della parte reversibile (con l'accoppiamento): per compensare il livello. */
    static double smallSignalSlope (double c) noexcept
    {
        const double chi = Ms / (3.0 * aShape);
        return c * chi / (1.0 - c * alpha * chi);
    }

    void prepare (double rate) noexcept   { sampleRate = rate; reset(); }
    void reset() noexcept                 { magnetisation = lastField = 0.0; }
    void setReversibility (double newC) noexcept { c = newC; }
    double getReversibility() const noexcept     { return c; }

    /** Campo H -> magnetizzazione M. Se il campo salta troppo in un passo (H molto più grande della
        larghezza della curva, a) Runge-Kutta diverge: il passo viene diviso in sotto-passi. */
    double process (double h) noexcept
    {
        const double jump = std::abs (h - lastField);
        const int steps = jump > maxJump ? juce::jmin (32, (int) std::ceil (jump / maxJump)) : 1;
        if (steps == 1)
            return step (h, 1.0 / sampleRate);

        const double start = lastField;
        double m = magnetisation;
        for (int i = 1; i <= steps; ++i)
            m = step (start + (h - start) * (double) i / (double) steps, 1.0 / (sampleRate * steps));
        return m;
    }

    int corrections = 0;   // diagnostica

private:
    static constexpr double maxJump = 0.5 * aShape;

    double step (double h, double T) noexcept
    {
        const double hDot = (h - lastField) / T;
        const double hMid = 0.5 * (h + lastField);
        const double m = magnetisation;

        const double k1 = derivative (lastField, hDot, m);
        const double k2 = derivative (hMid, hDot, m + 0.5 * T * k1);
        const double k3 = derivative (hMid, hDot, m + 0.5 * T * k2);
        const double k4 = derivative (h, hDot, m + T * k3);
        double next = m + T / 6.0 * (k1 + 2.0 * k2 + 2.0 * k3 + k4);

        if (! std::isfinite (next) || std::abs (next) > 1.5 * Ms)
        {
            next = juce::jlimit (-Ms, Ms, std::isfinite (next) ? next : 0.0);
            ++corrections;
        }

        magnetisation = next;
        lastField = h;
        return next;
    }

    /** Funzione di Langevin L(x) = coth(x) - 1/x e la sua derivata, con un solo exp:
        coth x = (e + 1) / (e - 1), 1 / sinh² x = 4 e / (e - 1)², e = exp(2x).
        Vicino a zero (cancellazione) la serie di Taylor. */
    static void langevin (double x, double& value, double& slope) noexcept
    {
        if (std::abs (x) < 1e-2)
        {
            const double x2 = x * x;
            value = x / 3.0 - x * x2 / 45.0 + 2.0 * x * x2 * x2 / 945.0;
            slope = 1.0 / 3.0 - x2 / 15.0 + 2.0 * x2 * x2 / 189.0;
            return;
        }
        const double ax = juce::jmin (std::abs (x), 300.0);
        const double e = std::exp (2.0 * ax), em1 = e - 1.0, inv = 1.0 / ax;
        value = std::copysign ((e + 1.0) / em1 - inv, x);
        slope = inv * inv - 4.0 * e / (em1 * em1);
    }

    double derivative (double h, double hDot, double m) const noexcept
    {
        const double q = (h + alpha * m) / aShape;
        double value, slope;
        langevin (q, value, slope);
        const double diff = Ms * value - m;
        const double delta = hDot >= 0.0 ? 1.0 : -1.0;
        const double deltaM = diff * delta > 0.0 ? 1.0 : 0.0;   // solo se M va verso la curva anisteretica

        const double irreversible = (1.0 - c) * deltaM * diff / ((1.0 - c) * delta * kPin - alpha * diff);
        const double reversible = c * (Ms / aShape) * slope;
        return hDot * (irreversible + reversible) / (1.0 - c * alpha * (Ms / aShape) * slope);
    }

    double sampleRate = 96000.0, c = 0.94, magnetisation = 0.0, lastField = 0.0;
};
