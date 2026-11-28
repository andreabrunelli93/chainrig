#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

//==============================================================================
/** Ramo di un rivelatore a transistor: sorgente (con la sua resistenza) -> condensatore in serie ->
    base di un NPN, con resistenza e diodo-morsetto verso massa. Il morsetto tiene i picchi negativi
    a circa -0,6 V, così la base supera la soglia del transistor quando il picco-picco del segnale
    supera ~1,2 V; allora il collettore assorbe corrente (esponenziale) dal condensatore dell'inviluppo.

    Integrazione a Eulero implicito; la tensione di base si trova con Newton dentro un intervallo
    (la funzione è monotona), come il solutore dei diodi di CircuitModel. */
class DetectorBranch
{
public:
    struct Parts
    {
        double sourceOhms = 10e3;     // resistenza d'uscita di chi pilota il ramo
        double capacitor = 10e-9;     // condensatore in serie
        double baseOhms = 1e6;        // resistenza dalla base a massa
        double diodeIs = 2.52e-9, diodeN = 1.752;   // diodo-morsetto (1N914)
        double transistorIs = 6e-15, beta = 500.0;  // NPN ad alto guadagno
    };

    void prepare (const Parts& p, double sampleRate)
    {
        parts = p;
        T = 1.0 / sampleRate;
        reset();
    }

    void reset()  { capVoltage = base = lastDrive = 0.0; slope = 0.0; }

    /** Un campione. source = tensione che pilota il ramo; restituisce la corrente di collettore (A). */
    double process (double source) noexcept
    {
        // corrente nel condensatore: i = (source - vcap_prec - vb) / (Rs + T/C)  (Eulero implicito)
        const double series = parts.sourceOhms + T / parts.capacitor;
        const double drive = source - capVoltage;
        const double vtD = parts.diodeN * thermal, vtQ = thermal;

        // f(vb) = i_C(vb) + i_diodo(vb) - vb/R - i_base(vb): decrescente in vb
        double lo = -1.5, hi = 1.5;
        // stima iniziale: tensione di prima spostata lungo la pendenza dv/d(drive) di allora
        double v = juce::jlimit (lo, hi, base + (drive - lastDrive) * slope);
        double iC = 0.0, eQ = 1.0, df = -1.0;

        for (int it = 0; it < 40; ++it)
        {
            const double eD = std::exp (-v / vtD);
            eQ = std::exp (v / vtQ);
            iC = (drive - v) / series;
            const double f = iC + parts.diodeIs * (eD - 1.0) - v / parts.baseOhms
                           - parts.transistorIs / parts.beta * (eQ - 1.0);
            df = -1.0 / series - parts.diodeIs * eD / vtD - 1.0 / parts.baseOhms
                            - parts.transistorIs / parts.beta * eQ / vtQ;

            if (f > 0.0) lo = v; else hi = v;

            double next = v - f / df;
            if (next <= lo || next >= hi)
                next = 0.5 * (lo + hi);

            const bool done = std::abs (next - v) < 1e-6;   // 1 µV
            v = next;
            ++iterations;
            if (done)
                break;
        }

        eQ = std::exp (v / vtQ);
        iC = (drive - v) / series;
        capVoltage += iC * T / parts.capacitor;
        base = v;
        lastDrive = drive;
        slope = (1.0 / series) / -df;
        return parts.transistorIs * (eQ - 1.0);
    }

    double getBaseVoltage() const noexcept   { return base; }
    juce::int64 iterations = 0;

private:
    static constexpr double thermal = 0.02585;
    Parts parts;
    double T = 1.0 / 48000.0, capVoltage = 0.0, base = 0.0, lastDrive = 0.0, slope = 0.0;
};

//==============================================================================
/** Condensatore dell'inviluppo caricato da una resistenza verso l'alimentazione e scaricato dai
    collettori dei rivelatori; un inseguitore di emettitore ne porta la tensione alla resistenza
    (potenziometro + fissa) che dà la corrente di comando Iabc di un OTA. */
class EnvelopeHold
{
public:
    void prepare (double supplyVolts, double chargeOhms, double capacitor, double followerBeta, double sampleRate)
    {
        supply = supplyVolts;
        R = chargeOhms;
        C = capacitor;
        beta = followerBeta;
        T = 1.0 / sampleRate;
        voltage = supply;   // a riposo il condensatore è carico
    }

    /** collectorCurrent: corrente assorbita dai rivelatori; loadCurrent: corrente data all'OTA (Iabc). */
    double process (double collectorCurrent, double loadCurrent) noexcept
    {
        // i transistor non possono portare il collettore sotto la saturazione
        const double sink = collectorCurrent * juce::jlimit (0.0, 1.0, (voltage - 0.1) / 0.2) + loadCurrent / beta;
        voltage = (voltage + T / C * (supply / R - sink)) / (1.0 + T / (C * R));
        voltage = juce::jlimit (0.0, supply, voltage);
        return voltage;
    }

    double getVoltage() const noexcept   { return voltage; }

private:
    double supply = 9.0, R = 150e3, C = 10e-6, beta = 500.0, T = 1.0 / 48000.0, voltage = 9.0;
};
