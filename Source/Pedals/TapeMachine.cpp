#include "TapeMachine.h"
#include "Artwork.h"
#include "TapeHysteresis.h"

using namespace juce;

namespace
{
    constexpr double pi = MathConstants<double>::pi;

    // DRIVE: campo H per 1.0 digitale; 2,8 dB per tacca, 5 = 1e5 (saturazione morbida sui picchi
    // forti a -6 dBFS), 10 = satura già a -18 dBFS. Il livello è compensato (DRIVE non alza il volume
    // a basso livello: cambia solo quanto satura).
    inline double fieldGain (double knob) noexcept   { return 2.0e4 * std::pow (10.0, 2.8 * knob / 20.0); }

    // BIAS: con il bias ideale (segnale ad alta frequenza sommato in registrazione) il nastro segue
    // la curva anisteretica, cioè il modello con c -> 1. Il bias non è simulato come segnale: lo
    // rappresenta c, la parte reversibile. 0 = sotto-polarizzato (c 0,4: ciclo largo, perde acuti e
    // livello a basso volume, sporco), 5 = c 0,94 (regolato bene), 10 = c 0,994 (quasi ideale).
    // Sotto c 0,4 i cicli minori gonfiano i bassi di oltre 10 dB (misurato): troppo per una chitarra.
    inline double reversibility (double knob) noexcept { return 1.0 - 0.6 * std::pow (10.0, -knob / 5.0); }

    // velocità: pollici al secondo, enfasi (costante di tempo NAB/IEC) e "head bump"
    struct Speed { double ips, emphasisTau, bumpHz, bumpDb; };
    constexpr Speed speeds[] = { { 7.5, 50e-6, 26.0, 3.0 }, { 15.0, 50e-6, 52.0, 2.5 }, { 30.0, 17.5e-6, 105.0, 2.0 } };
    constexpr double emphasisMax = 4.0;   // +12 dB al massimo (scaffale)

    // testina di lettura: distanza nastro-testina e traferro (perdite exp(-k d) e sinc(k g / 2))
    constexpr double spacing = 1.05e-6;   // -3 dB a 10 kHz a 7,5 ips, a 20 kHz a 15 ips
    constexpr double gap = 2.0e-6;

    // wow & flutter (WOW a 10): deviazione d'intonazione di picco
    constexpr double wowDepth = 0.0025, wowHz = 0.7;
    constexpr double flutterDepth = 0.0008, flutterHz = 11.0;
    constexpr double maxDelaySeconds = 0.003;

    // scaffale del primo ordine: guadagno 1 in basso, `top` in alto, transizione a 1 / (2 pi tau)
    void designShelf (double tau, double top, double rate, double& b0, double& b1, double& a1)
    {
        // H(s) = (1 + s tau) / (1 + s tau / top), bilineare
        const double k = 2.0 * rate;
        const double n0 = 1.0 + k * tau, n1 = 1.0 - k * tau;
        const double d0 = 1.0 + k * tau / top, d1 = 1.0 - k * tau / top;
        b0 = n0 / d0; b1 = n1 / d0; a1 = d1 / d0;
    }
}

TapeMachine::Info TapeMachine::getInfo()
{
    Info i;
    i.identifier = "ChainRig Tape Machine";
    i.name = "Echoplex EP-3";   // nome scelto dall'utente; modello del nastro, identificatore neutro
    i.descriptiveName = "Physical tape machine model: magnetic hysteresis, head losses, wow and flutter (built-in)";
    i.category = "Tape";
    i.menuCategory = "Tape";
    i.uniqueId = 0x54415031;   // "TAP1"
    i.oversamplingOrder = 1;   // 2x: aliasing misurato -65 dB nel caso peggiore (2637 Hz, DRIVE 10, -6 dBFS), altrimenti sotto -98; 4x costava il doppio

    i.look.enclosureTop = Colour (0xff3b3f38);
    i.look.enclosureBottom = Colour (0xff23261f);
    i.look.ink = Colour (0xffe9dcc0);
    i.look.knob = Colour (0xffd9cfb8);
    i.look.pointer = Colour (0xff2a2520);
    i.look.led = Colour (0xff7dff6a);
    i.look.brushed = true;
    i.look.subtitle = "TAPE MACHINE";
    i.look.emblem = Artwork::tapeReels();
    // niente usura: anche a 0,1 il rumore dell'inchiostro mangiava il "3" del nome
    i.look.height = 790;
    return i;
}

TapeMachine::TapeMachine() : PedalProcessor (getInfo())
{
    drive = addKnob ("drive", "Drive", 5.0f);
    bias  = addKnob ("bias",  "Bias",  5.0f);
    wow   = addKnob ("wow",   "Wow",   2.0f);
    level = addKnob ("level", "Level", 5.0f);
    speed = addSelector ("speed", "Speed", { "7.5 ips", "15 ips", "30 ips" }, 1);
}

void TapeMachine::prepareCircuit (double rate)
{
    sampleRate = rate;
    for (auto [s, p] : { std::pair { &driveSmoothed, drive }, std::pair { &biasSmoothed, bias },
                         std::pair { &wowSmoothed, wow }, std::pair { &levelSmoothed, level } })
    {
        s->reset (rate, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }

    tape.prepare (rate);
    fieldScale = readScale = outputGain = 0.0;   // ricalcolati al primo campione

    // blocco della continua a 5 Hz (l'isteresi lascia una magnetizzazione residua)
    {
        const double k = std::tan (pi * 5.0 / rate);
        dcBlock.b0 = 1.0 / (1.0 + k); dcBlock.b1 = -dcBlock.b0; dcBlock.a1 = (k - 1.0) / (k + 1.0);
        dcBlock.reset();
    }

    delayLine.assign ((size_t) (maxDelaySeconds * rate) + 8, 0.0f);
    writePos = 0;
    wowPhase = flutterPhase = wowDrift = driftTarget = 0.0;
    untilDrift = 0;

    currentSpeed = -1;
    setSpeed (roundToInt (speed->get()));
}

void TapeMachine::setSpeed (int index)
{
    index = jlimit (0, 2, index);
    if (index == currentSpeed)
        return;
    currentSpeed = index;
    const auto& s = speeds[index];
    const double v = s.ips * 0.0254;   // m/s

    designShelf (s.emphasisTau, emphasisMax, sampleRate, preEmphasis.b0, preEmphasis.b1, preEmphasis.a1);
    designShelf (s.emphasisTau / emphasisMax, 1.0 / emphasisMax, sampleRate, deEmphasis.b0, deEmphasis.b1, deEmphasis.a1);
    preEmphasis.reset();
    deEmphasis.reset();

    // perdite della testina: FIR a fase lineare dalla risposta exp(-k d) sinc(k g / 2), k = 2 pi f / v
    {
        constexpr int half = firLength / 2, grid = 2048;
        std::array<double, firLength> h {};
        for (int i = 0; i <= grid; ++i)
        {
            const double f = 0.5 * sampleRate * i / grid;
            const double k = 2.0 * pi * f / v;
            const double x = k * gap / 2.0;
            const double mag = std::exp (-k * spacing) * (x < 1e-9 ? 1.0 : std::sin (x) / x);
            const double w = (i == 0 || i == grid) ? 0.5 : 1.0;
            for (int n = 0; n < firLength; ++n)
                h[(size_t) n] += w * mag * std::cos (2.0 * pi * f / sampleRate * (n - half));
        }
        double sum = 0.0;
        for (int n = 0; n < firLength; ++n)
        {
            h[(size_t) n] *= 0.5 - 0.5 * std::cos (2.0 * pi * (n + 1) / (firLength + 1));   // Hann
            sum += h[(size_t) n];
        }
        for (int n = 0; n < firLength; ++n)
            fir[(size_t) n] = h[(size_t) n] / sum;   // guadagno 1 in continua
        firState.fill (0.0);
        firPos = 0;
    }

    // head bump: picco (RBJ) e passa-alto del primo ordine a metà frequenza
    {
        const double w0 = 2.0 * pi * s.bumpHz / sampleRate, A = std::pow (10.0, s.bumpDb / 40.0);
        const double alphaQ = std::sin (w0) / (2.0 * 1.2);
        const double a0 = 1.0 + alphaQ / A;
        headBump.b0 = (1.0 + alphaQ * A) / a0;
        headBump.b1 = -2.0 * std::cos (w0) / a0;
        headBump.b2 = (1.0 - alphaQ * A) / a0;
        headBump.a1 = headBump.b1;
        headBump.a2 = (1.0 - alphaQ / A) / a0;
        headBump.reset();

        const double k = std::tan (pi * 0.5 * s.bumpHz / sampleRate);
        lowCut.b0 = 1.0 / (1.0 + k); lowCut.b1 = -lowCut.b0; lowCut.a1 = (k - 1.0) / (k + 1.0);
        lowCut.reset();
    }
}

double TapeMachine::readDelay (double d) const noexcept
{
    // Hermite a 4 punti sul ritardo frazionario (d >= 1 campione)
    const int size = (int) delayLine.size();
    const double pos = (double) writePos - d;
    const int i = (int) std::floor (pos);
    const double t = pos - i;
    auto at = [this, size] (int idx) { return (double) delayLine[(size_t) ((idx % size + size) % size)]; };
    const double y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void TapeMachine::processCircuit (float* samples, int n)
{
    driveSmoothed.setTargetValue (drive->get());
    biasSmoothed.setTargetValue (bias->get());
    wowSmoothed.setTargetValue (wow->get());
    levelSmoothed.setTargetValue (level->get());
    setSpeed (roundToInt (speed->get()));

    const int size = (int) delayLine.size();

    for (int i = 0; i < n; ++i)
    {
        // pow solo mentre le manopole si muovono
        if (driveSmoothed.isSmoothing() || fieldScale <= 0.0)
            fieldScale = fieldGain (driveSmoothed.getNextValue());
        if (biasSmoothed.isSmoothing() || readScale <= 0.0)
        {
            tape.setReversibility (reversibility (biasSmoothed.getNextValue()));
            readScale = 0.0;
        }
        if (readScale <= 0.0 || driveSmoothed.isSmoothing())
            readScale = 1.0 / (TapeHysteresis::smallSignalSlope (tape.getReversibility()) * fieldScale);
        if (levelSmoothed.isSmoothing() || outputGain <= 0.0)
            outputGain = Decibels::decibelsToGain (-12.0 + 2.4 * levelSmoothed.getNextValue());
        const double g = fieldScale;

        // registrazione: enfasi degli acuti, campo, nastro
        const double h = preEmphasis.process ((double) samples[i]) * g;
        const double m = tape.process (h);
        peakMagnetisation = jmax (peakMagnetisation, std::abs (m) / TapeHysteresis::Ms);

        // lettura: livello compensato (pendenza iniziale), de-enfasi, perdite della testina, head bump
        double y = deEmphasis.process (m * readScale);
        firState[(size_t) firPos] = y;
        double acc = 0.0;
        for (int k = 0, j = firPos; k < firLength; ++k, j = (j == 0 ? firLength - 1 : j - 1))
            acc += fir[(size_t) k] * firState[(size_t) j];
        firPos = (firPos + 1) % firLength;
        y = lowCut.process (headBump.process (acc));

        // wow & flutter: ritardo modulato. Wow con una deriva casuale lenta della velocità
        const double depth = wowSmoothed.getNextValue() / 10.0;
        if (--untilDrift <= 0)
        {
            driftTarget = random.nextDouble() * 2.0 - 1.0;
            untilDrift = (int) (sampleRate * 0.5);
        }
        wowDrift += (driftTarget - wowDrift) * (1.0 / (sampleRate * 0.4));
        wowPhase += 2.0 * pi * wowHz * (1.0 + 0.3 * wowDrift) / sampleRate;
        flutterPhase += 2.0 * pi * flutterHz / sampleRate;
        if (wowPhase > 2.0 * pi) wowPhase -= 2.0 * pi;
        if (flutterPhase > 2.0 * pi) flutterPhase -= 2.0 * pi;

        // ampiezza del ritardo = deviazione / (2 pi f); ritardo sempre >= 0 (a WOW 0 nessun ritardo)
        const double wowAmp = depth * wowDepth / (2.0 * pi * wowHz) * sampleRate;
        const double flutterAmp = depth * flutterDepth / (2.0 * pi * flutterHz) * sampleRate;
        const double d = wowAmp * (1.0 + std::sin (wowPhase)) + flutterAmp * (1.0 + std::sin (flutterPhase));

        delayLine[(size_t) writePos] = (float) y;
        if (d > 1e-6)
            y = readDelay (jlimit (1.0, (double) size - 4.0, d + 1.0));
        writePos = (writePos + 1) % size;

        y = dcBlock.process (y);
        samples[i] = (float) (y * outputGain);
    }
}

String TapeMachine::getDiagnostics() const
{
    return "magnetizzazione massima " + String (peakMagnetisation * 100.0, 1) + "% di Ms, correzioni " + String (tape.corrections);
}
