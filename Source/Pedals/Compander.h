#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

//==============================================================================
/** Una sezione di compander tipo NE570/571, dalle equazioni del datasheet: un rivelatore a media
    (raddrizzatore a doppia semionda, resistenza interna 10k sul condensatore esterno) comanda una
    cella di guadagno.
      - compressore (cella nella retroazione): uscita ∝ radice del livello d'ingresso (2:1 in dB);
      - espansore (cella in serie): guadagno ∝ livello d'ingresso (1:2 in dB).
    Con lo stesso livello di riferimento le due sezioni in serie danno guadagno 1 a regime; il
    rumore aggiunto tra le due (il BBD) viene abbassato quando il segnale è basso. */
class CompanderSection
{
public:
    enum class Mode { compress, expand };

    void prepare (Mode m, double sampleRate, double rectifierCap, double referenceVolts)
    {
        mode = m;
        // 10k interni sul condensatore di raddrizzamento (NE570): costante di tempo per attacco e rilascio
        coeff = 1.0 - std::exp (-1.0 / (10e3 * rectifierCap * sampleRate));
        reference = referenceVolts;
        reset();
    }

    void reset()  { average = reference; }

    double process (double x) noexcept
    {
        if (mode == Mode::expand)
        {
            // il rivelatore guarda l'ingresso: guadagno = livello / riferimento
            average += (std::abs (x) - average) * coeff;
            return x * (average / reference);
        }

        // compressore: il rivelatore guarda l'uscita (cella in retroazione): y = x ref / media(|y|),
        // a regime media(|y|) = sqrt(media(|x|) ref). Guadagno massimo 40 dB (media minima ref / 100).
        const double y = x * reference / juce::jmax (reference * 0.01, average);
        average += (std::abs (y) - average) * coeff;
        return y;
    }

    double getAverage() const noexcept   { return average; }

private:
    Mode mode = Mode::compress;
    double coeff = 0.01, reference = 0.5, average = 0.5;
};
