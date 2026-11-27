#include "TapeEcho.h"
#include "Taper.h"

using namespace juce;

namespace
{
    constexpr double pi = MathConstants<double>::pi;

    // testina 1: 177 ms (REPEAT RATE al minimo) … 69 ms (al massimo); testine 2 e 3 a 2x e 3x
    // (testine equidistanti). La velocità del motore è lineare nella manopola.
    constexpr double head1Slow = 0.177, head1Fast = 0.069;
    constexpr double maxDelaySeconds = 3.0 * head1Slow * 1.05 + 0.01;
    constexpr double motorTau = 0.25;   // inerzia del motore e del nastro: il ritardo scivola

    // MODE: testine attive (le combinazioni di testine del pedale, senza riverbero)
    constexpr bool modes[][3] = { { true, false, false }, { false, true, false }, { false, false, true },
                                  { false, true, true }, { true, true, false }, { true, true, true } };

    // registrazione: campo H per 1.0 digitale (come DRIVE della macchina a nastro: 5 = 1e5), bias ben regolato
    inline double fieldGain (double knob) noexcept   { return 2.0e4 * std::pow (10.0, 2.8 * knob / 20.0); }
    constexpr double reversibility = 0.94;
    constexpr double emphasisTau = 50e-6, emphasisMax = 4.0;

    // lettura: banda che scende con la velocità (perdita di distanza ∝ f / v). Scelta: -3 dB a 8 kHz
    // alla velocità massima (circa 6 ips con la stessa testina della macchina a nastro), 3,1 kHz alla minima.
    constexpr double bandwidthFast = 8000.0;
    constexpr double lowCutHz = 80.0;     // ripetizioni un po' più sottili in basso (scelta)

    // INTENSITY: guadagno dell'anello. Con una testina l'anello arriva a 1 a INTENSITY 7 (poi auto-
    // oscillazione, contenuta dalla saturazione del nastro), 1,6 al massimo.
    inline double loopGain (double knob) noexcept    { return 1.59 * std::pow (knob / 10.0, 1.3); }

    // rumore del nastro: rapporto S/N 60 dB (specifiche) su un livello nominale di 0,25
    constexpr double hissRms = 0.25 * 1e-3;

    // BASS e TREBLE (solo sulle ripetizioni): scaffali del primo ordine ±12 dB a 100 Hz e 3 kHz (scelta)
    constexpr double toneRangeDb = 12.0, bassHz = 100.0, trebleHz = 3000.0;

    // amplificatori di registrazione e di lettura: saturano prima del nastro quando l'anello corre
    // (auto-oscillazione). Limite morbido, lineare fino a circa metà (scelta).
    constexpr double recordLimit = 1.2, playbackLimit = 0.9;
    inline double softLimit (double x, double limit) noexcept
    {
        const double t = x / limit;
        return limit * t / std::sqrt (std::sqrt (1.0 + t * t * t * t));
    }

    // wow & flutter (WOW a 10): deviazione d'intonazione di picco (nastro consumato)
    constexpr double wowDepth = 0.004, wowHz = 0.9;
    constexpr double flutterDepth = 0.0012, flutterHz = 9.0;

    void designShelf (double tau, double top, double rate, double& b0, double& b1, double& a1)
    {
        // H(s) = (1 + s tau) / (1 + s tau / top): 1 in basso, top in alto
        const double k = 2.0 * rate;
        const double n0 = 1.0 + k * tau, n1 = 1.0 - k * tau, d0 = 1.0 + k * tau / top, d1 = 1.0 - k * tau / top;
        b0 = n0 / d0; b1 = n1 / d0; a1 = d1 / d0;
    }

    // scaffale del primo ordine: guadagno g sopra (alti) o sotto (bassi) la frequenza f, 1 dall'altra parte.
    //   alti:  H(s) = (s sqrt(g) + w) / (s / sqrt(g) + w)
    //   bassi: H(s) = (s + w sqrt(g)) / (s + w / sqrt(g))
    // bilineare con la frequenza f esatta
    void designToneShelf (bool high, double f, double gainDb, double rate, double& b0, double& b1, double& a1)
    {
        const double root = std::sqrt (Decibels::decibelsToGain (gainDb));
        const double w = 2.0 * pi * f, k = w / std::tan (w / (2.0 * rate));
        const double A = high ? root : 1.0, B = high ? w : w * root;          // numeratore A s + B
        const double C = high ? 1.0 / root : 1.0, D = high ? w : w / root;    // denominatore C s + D
        const double a0 = C * k + D;
        b0 = (A * k + B) / a0; b1 = (B - A * k) / a0; a1 = (D - C * k) / a0;
    }
}

TapeEcho::Info TapeEcho::getInfo()
{
    Info i;
    i.identifier = "ChainRig Tape Echo";
    i.name = "Space Echo RE-201";   // nome scelto dall'utente; identificatore neutro
    i.descriptiveName = "Three-head tape echo with tape saturation, wow and flutter (built-in)";
    i.category = "Delay";
    i.menuCategory = "Delay";
    i.uniqueId = 0x54454331;   // "TEC1"
    i.oversamplingOrder = 1;   // 2x per l'isteresi (come la macchina a nastro)

    i.look.enclosureTop = Colour (0xff2b2a27);
    i.look.enclosureBottom = Colour (0xff171614);
    i.look.ink = Colour (0xffc9e6b8);
    i.look.knob = Colour (0xff0f0f0f);
    i.look.pointer = Colour (0xffc9e6b8);
    i.look.led = Colour (0xffff4a3a);
    i.look.brushed = true;
    i.look.subtitle = "TAPE ECHO";
    i.look.width = 760;
    i.look.height = 720;
    i.look.knobPlaces = { { 0.14f, 64, 88 }, { 0.38f, 64, 88 }, { 0.62f, 64, 88 }, { 0.86f, 64, 88 },
                          { 0.26f, 236, 88 }, { 0.5f, 236, 88 }, { 0.74f, 236, 88 } };
    i.look.nameTop = 410.0f;
    return i;
}

TapeEcho::TapeEcho() : PedalProcessor (getInfo())
{
    input      = addKnob ("input",     "Input",     5.0f);
    repeatRate = addKnob ("rate",      "Rate",      5.0f);
    intensity  = addKnob ("intensity", "Intensity", 4.0f);
    echoVolume = addKnob ("echo",      "Echo",      6.0f);
    bass       = addKnob ("bass",      "Bass",      5.0f);
    treble     = addKnob ("treble",    "Treble",    5.0f);
    wow        = addKnob ("wow",       "Wow",       3.0f);
    mode       = addSelector ("mode", "Heads", { "1", "2", "3", "2+3", "1+2", "1+2+3" }, 0);
}

void TapeEcho::prepareCircuit (double rate)
{
    sampleRate = rate;
    for (auto [s, p] : { std::pair { &inputSmoothed, input }, std::pair { &intensitySmoothed, intensity },
                         std::pair { &echoSmoothed, echoVolume }, std::pair { &bassSmoothed, bass },
                         std::pair { &trebleSmoothed, treble }, std::pair { &wowSmoothed, wow } })
    {
        s->reset (rate, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }

    tape.prepare (rate);
    tape.setReversibility (reversibility);
    designShelf (emphasisTau, emphasisMax, rate, preEmphasis.b0, preEmphasis.b1, preEmphasis.a1);
    designShelf (emphasisTau / emphasisMax, 1.0 / emphasisMax, rate, deEmphasis.b0, deEmphasis.b1, deEmphasis.a1);
    {
        const double k = std::tan (pi * lowCutHz / rate);
        lowCut.b0 = 1.0 / (1.0 + k); lowCut.b1 = -lowCut.b0; lowCut.a1 = (k - 1.0) / (k + 1.0);
        const double kd = std::tan (pi * 5.0 / rate);
        dcBlock.b0 = 1.0 / (1.0 + kd); dcBlock.b1 = -dcBlock.b0; dcBlock.a1 = (kd - 1.0) / (kd + 1.0);
    }
    for (auto* f : { &preEmphasis, &deEmphasis, &lowCut, &bassShelf, &trebleShelf, &dcBlock })
        f->reset();
    headLoss.reset();

    loop.assign ((size_t) (maxDelaySeconds * rate) + 8, 0.0f);
    writePos = 0;
    motorCoeff = 1.0 - std::exp (-1.0 / (motorTau * rate));
    speed = jmap ((double) repeatRate->get() / 10.0, head1Fast / head1Slow, 1.0);
    feedback = 0.0;
    wowPhase = flutterPhase = wowDrift = driftTarget = 0.0;
    untilDrift = untilFilters = 0;
    lastFilterSpeed = -1.0;
    lastBass = lastTreble = 1e9;
    updateFilters (speed, true);
}

void TapeEcho::updateFilters (double speedRatio, bool force)
{
    if (force || std::abs (speedRatio - lastFilterSpeed) > 1e-3)
    {
        lastFilterSpeed = speedRatio;
        // passa-basso di Butterworth del secondo ordine alla banda della testina per questa velocità
        const double fc = jmin (bandwidthFast * speedRatio, 0.45 * sampleRate);
        const double k = std::tan (pi * fc / sampleRate), q = MathConstants<double>::sqrt2 / 2.0;
        const double norm = 1.0 / (1.0 + k / q + k * k);
        headLoss.b0 = k * k * norm; headLoss.b1 = 2.0 * headLoss.b0; headLoss.b2 = headLoss.b0;
        headLoss.a1 = 2.0 * (k * k - 1.0) * norm; headLoss.a2 = (1.0 - k / q + k * k) * norm;
    }

    const double b = toneRangeDb * (bassSmoothed.getCurrentValue() - 5.0) / 5.0;
    const double t = toneRangeDb * (trebleSmoothed.getCurrentValue() - 5.0) / 5.0;
    if (force || std::abs (b - lastBass) > 1e-3)
    {
        lastBass = b;
        designToneShelf (false, bassHz, b, sampleRate, bassShelf.b0, bassShelf.b1, bassShelf.a1);
    }
    if (force || std::abs (t - lastTreble) > 1e-3)
    {
        lastTreble = t;
        designToneShelf (true, trebleHz, t, sampleRate, trebleShelf.b0, trebleShelf.b1, trebleShelf.a1);
    }
}

double TapeEcho::readHead (double d) const noexcept
{
    // Hermite a 4 punti (d >= 1 campione)
    const int size = (int) loop.size();
    const double pos = (double) writePos - d;
    const int i = (int) std::floor (pos);
    const double t = pos - i;
    auto at = [this, size] (int idx) { return (double) loop[(size_t) ((idx % size + size) % size)]; };
    const double y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void TapeEcho::processCircuit (float* samples, int n)
{
    inputSmoothed.setTargetValue (input->get());
    intensitySmoothed.setTargetValue (intensity->get());
    echoSmoothed.setTargetValue (echoVolume->get());
    bassSmoothed.setTargetValue (bass->get());
    trebleSmoothed.setTargetValue (treble->get());
    wowSmoothed.setTargetValue (wow->get());

    const auto& heads = modes[jlimit (0, 5, roundToInt (mode->get()))];
    const double headScale = 1.0 / std::sqrt ((double) (heads[0] + heads[1] + heads[2]));
    const double targetSpeed = jmap ((double) repeatRate->get() / 10.0, head1Fast / head1Slow, 1.0);
    const int size = (int) loop.size();

    for (int i = 0; i < n; ++i)
    {
        speed += (targetSpeed - speed) * motorCoeff;
        if (--untilFilters <= 0)
        {
            updateFilters (speed, false);
            untilFilters = 32;
        }
        bassSmoothed.getNextValue();
        trebleSmoothed.getNextValue();

        const double dry = samples[i];

        // registrazione: ingresso + ritorno, enfasi, campo, nastro; il livello letto è compensato
        const double g = fieldGain (inputSmoothed.getNextValue());
        const double record = softLimit (dry + loopGain (intensitySmoothed.getNextValue()) * feedback, recordLimit);
        const double m = tape.process (preEmphasis.process (record) * g);
        loop[(size_t) writePos] = (float) (m / (TapeHysteresis::smallSignalSlope (reversibility) * g));

        // velocità del nastro con wow (deriva casuale lenta) e flutter: ritardo = distanza / velocità
        const double depth = wowSmoothed.getNextValue() / 10.0;
        if (--untilDrift <= 0)
        {
            driftTarget = random.nextDouble() * 2.0 - 1.0;
            untilDrift = (int) (sampleRate * 0.6);
        }
        wowDrift += (driftTarget - wowDrift) * (1.0 / (sampleRate * 0.5));
        wowPhase += 2.0 * pi * wowHz * (1.0 + 0.3 * wowDrift) / sampleRate;
        flutterPhase += 2.0 * pi * flutterHz / sampleRate;
        if (wowPhase > 2.0 * pi) wowPhase -= 2.0 * pi;
        if (flutterPhase > 2.0 * pi) flutterPhase -= 2.0 * pi;
        const double modulation = depth * (wowDepth / (2.0 * pi * wowHz) * std::sin (wowPhase)
                                         + flutterDepth / (2.0 * pi * flutterHz) * std::sin (flutterPhase)) * sampleRate;

        // testine: ritardo base della testina 1 alla velocità attuale, 2x e 3x per le altre
        const double head1 = head1Fast / speed * sampleRate;
        double playback = 0.0;
        for (int h = 0; h < 3; ++h)
            if (heads[h])
                playback += readHead (jlimit (2.0, (double) size - 4.0, (h + 1) * head1 + modulation));
        playback *= headScale;   // più testine: somma a potenza costante
        writePos = (writePos + 1) % size;

        // lettura: de-enfasi, perdite della testina (dipendono dalla velocità), rumore del nastro
        playback = deEmphasis.process (playback);
        playback = lowCut.process (headLoss.process (playback + hissRms * 1.732 * (random.nextDouble() * 2.0 - 1.0)));
        playback = softLimit (playback, playbackLimit);
        feedback = playback;   // INTENSITY prende il segnale qui, prima dei toni
        peakPlayback = jmax (peakPlayback, std::abs (playback));

        // ECHO VOLUME, BASS e TREBLE sulle ripetizioni, poi l'asciutto
        const double echo = trebleShelf.process (bassShelf.process (playback)) * 1.5 * echoSmoothed.getNextValue() / 10.0;
        samples[i] = (float) dcBlock.process (dry + echo);
    }
}

String TapeEcho::getDiagnostics() const
{
    return "picco alle testine " + String (Decibels::gainToDecibels (peakPlayback), 1) + " dBFS, correzioni del nastro "
         + String (tape.corrections);
}
