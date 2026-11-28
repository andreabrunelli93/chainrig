#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

//==============================================================================
/** Curve dei potenziometri: posizione della manopola (0-10) -> frazione della resistenza (0-1). */
namespace Taper
{
    /** Logaritmico (A): 10% della resistenza a metà corsa. */
    inline double audio (double knob) noexcept
    {
        constexpr double b = 81.0;
        return (std::pow (b, juce::jlimit (0.0, 1.0, knob / 10.0)) - 1.0) / (b - 1.0);
    }

    /** Curva misurata: Hermite monotona tra i punti (x = manopola, y = frazione, m = pendenze già
        limitate con Fritsch-Carlson, così la curva non torna mai indietro). */
    template <size_t N>
    double measured (const double (&x)[N], const double (&y)[N], const double (&m)[N], double knob) noexcept
    {
        knob = juce::jlimit (x[0], x[N - 1], knob);
        size_t i = 0;
        while (i + 2 < N && knob > x[i + 1])
            ++i;

        const double h = x[i + 1] - x[i], t = (knob - x[i]) / h, t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * y[i] + (t3 - 2 * t2 + t) * h * m[i]
             + (-2 * t3 + 3 * t2) * y[i + 1] + (t3 - t2) * h * m[i + 1];
    }
}
