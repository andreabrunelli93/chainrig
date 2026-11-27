// Strumento di prova dei pedali integrati, senza interfaccia né scheda audio:
//   pedal_lab_render "<identifier>" in.wav out.wav [--block 128] [gain=7 tone=5 ...]
// Legge il WAV (canale sinistro), lo passa nel pedale a blocchi, scrive un WAV float a 2 canali
// e stampa il tempo per blocco rispetto al tempo disponibile.
// Con --list elenca i pedali.

#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Pedals/Pedals.h"

using namespace juce;

// La finestra del pedale non serve qui (e porterebbe con sé tutta l'interfaccia)
AudioProcessorEditor* PedalProcessor::createEditor() { return nullptr; }

static double percentile (std::vector<double> v, double q)
{
    if (v.empty())
        return 0.0;
    std::sort (v.begin(), v.end());
    return v[(size_t) jlimit (0, (int) v.size() - 1, (int) (q * (double) v.size()))];
}

int main (int argc, char* argv[])
{
    StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (String::fromUTF8 (argv[i]));

    if (args.contains ("--list"))
    {
        for (auto& p : Pedals::all())
            std::cout << p.info.identifier << "  (" << p.info.name << ")\n";
        return 0;
    }

    if (args.size() < 3)
    {
        std::cerr << "uso: pedal_lab_render \"<identifier>\" in.wav out.wav [--block 128] [param=valore ...]\n";
        return 2;
    }

    auto* entry = Pedals::find (args[0]);
    if (entry == nullptr)
    {
        std::cerr << "pedale sconosciuto: " << args[0] << " (usa --list)\n";
        return 2;
    }

    int block = 128;
    if (const int i = args.indexOf ("--block"); i >= 0 && i + 1 < args.size())
        block = jmax (1, args[i + 1].getIntValue());

    AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (File::getCurrentWorkingDirectory().getChildFile (args[1])));
    if (reader == nullptr)
    {
        std::cerr << "non riesco a leggere " << args[1] << "\n";
        return 1;
    }

    const int length = (int) reader->lengthInSamples;
    const double rate = reader->sampleRate;
    AudioBuffer<float> audio (2, length);
    reader->read (&audio, 0, length, 0, true, false);
    audio.copyFrom (1, 0, audio, 0, 0, length);

    auto pedal = entry->create();
    pedal->setPlayConfigDetails (2, 2, rate, block);

    for (int i = 3; i < args.size(); ++i)
    {
        if (! args[i].contains ("="))
            continue;
        const auto id = args[i].upToFirstOccurrenceOf ("=", false, false);
        const auto value = args[i].fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        bool found = false;
        for (auto* k : pedal->getKnobs())
            if (k->getParameterID() == id)
            {
                k->set (value);
                found = true;
            }
        if (! found)
            std::cerr << "manopola sconosciuta: " << id << "\n";
    }

    pedal->prepareToPlay (rate, block);

    MidiBuffer midi;
    double total = 0.0, worst = 0.0;
    int blocks = 0, worstBlock = 0;
    std::vector<double> times;
    for (int start = 0; start < length; start += block)
    {
        const int n = jmin (block, length - start);
        AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, start, n);
        const auto t0 = Time::getHighResolutionTicks();
        pedal->processBlock (view, midi);
        const double seconds = Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0);
        total += seconds;
        times.push_back (seconds);
        if (seconds > worst)
        {
            worst = seconds;
            worstBlock = blocks;
        }
        ++blocks;
    }

    int bad = 0;
    for (int i = 0; i < length; ++i)
        if (! std::isfinite (audio.getSample (0, i)))
            ++bad;

    const File outFile = File::getCurrentWorkingDirectory().getChildFile (args[2]);
    outFile.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (outFile);
    auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
    {
        std::cerr << "non riesco a scrivere " << args[2] << "\n";
        return 1;
    }
    writer->writeFromAudioSampleBuffer (audio, 0, length);

    const double budget = block / rate;
    std::cout << entry->info.name << ": " << blocks << " blocchi da " << block << " a " << rate << " Hz, latenza "
              << pedal->getLatencySamples() << " campioni\n"
              << "  tempo medio " << String (total / blocks * 1e6, 1) << " us (" << String (100.0 * total / blocks / budget, 2)
              << "% del blocco), massimo " << String (worst * 1e6, 1) << " us (" << String (100.0 * worst / budget, 1)
              << "%, blocco " << worstBlock << ")\n"
              << "  99,9 percentile " << String (100.0 * percentile (times, 0.999) / budget, 1) << "% del blocco\n"
              << "  NaN/Inf: " << bad << "\n";
    if (const auto diagnostics = pedal->getDiagnostics(); diagnostics.isNotEmpty())
        std::cout << "  " << diagnostics << "\n";
    return bad == 0 ? 0 : 1;
}
