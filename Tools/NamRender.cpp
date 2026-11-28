// Fa passare un WAV in una cattura NAM, offline, in volt veri (per confrontarla con un pedale integrato):
//   nam_render model.nam in.wav out.wav [--volts 2.5]
// Il WAV d'ingresso è in unità del pedale: 1.0 = --volts V di picco (come TransparentDrive::inputVolts).
// Con una cattura calibrata (input/output level in dBu) l'ingresso viene portato al livello giusto per la
// cattura e l'uscita è scritta nella stessa unità (1.0 = --volts V), così il guadagno si legge in dB di tensione.
// Il WAV deve essere alla frequenza della cattura (di solito 48 kHz).

#include <juce_audio_formats/juce_audio_formats.h>
#include <NAM/get_dsp.h>
#include <filesystem>

using namespace juce;

static double dbuToPeakVolts (double dbu)   // dBu RMS di una sinusoide -> volt di picco
{
    return 0.7746 * std::pow (10.0, dbu / 20.0) * std::sqrt (2.0);
}

int main (int argc, char* argv[])
{
    StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (String::fromUTF8 (argv[i]));

    if (args.size() < 3)
    {
        std::cerr << "uso: nam_render model.nam in.wav out.wav [--volts 2.5]\n";
        return 2;
    }

    double volts = 2.5;
    if (const int i = args.indexOf ("--volts"); i >= 0 && i + 1 < args.size())
        volts = args[i + 1].getDoubleValue();

    const auto cwd = File::getCurrentWorkingDirectory();
    nam::dspData data;
    std::unique_ptr<nam::DSP> dsp;
    try
    {
        dsp = nam::get_dsp (std::filesystem::path (cwd.getChildFile (args[0]).getFullPathName().toStdString()), data);
    }
    catch (const std::exception& e)
    {
        std::cerr << "cattura non valida: " << e.what() << "\n";
        return 1;
    }

    AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (cwd.getChildFile (args[1])));
    if (reader == nullptr)
    {
        std::cerr << "non riesco a leggere " << args[1] << "\n";
        return 1;
    }

    const double rate = reader->sampleRate, expected = dsp->GetExpectedSampleRate();
    if (expected > 0 && std::abs (expected - rate) > 1.0)
    {
        std::cerr << "la cattura vuole " << expected << " Hz, il WAV è a " << rate << " Hz\n";
        return 1;
    }

    // livelli: senza calibrazione si assume 0 dBFS = 1 V di picco (e lo si dice)
    const bool hasIn = dsp->HasInputLevel(), hasOut = dsp->HasOutputLevel();
    const double inFs = hasIn ? dbuToPeakVolts (dsp->GetInputLevel()) : 1.0;
    const double outFs = hasOut ? dbuToPeakVolts (dsp->GetOutputLevel()) : 1.0;
    std::cout << "cattura: " << (hasIn ? String (dsp->GetInputLevel(), 2) + " dBu in" : String ("ingresso non calibrato"))
              << ", " << (hasOut ? String (dsp->GetOutputLevel(), 2) + " dBu out" : String ("uscita non calibrata"))
              << ", " << expected << " Hz"
              << (dsp->HasLoudness() ? ", loudness " + String (dsp->GetLoudness(), 1) + " dB" : String()) << "\n"
              << "  0 dBFS = " << String (inFs, 3) << " V in, " << String (outFs, 3) << " V out\n";

    const int length = (int) reader->lengthInSamples;
    AudioBuffer<float> audio (1, length);
    reader->read (&audio, 0, length, 0, true, false);

    const int block = 512;
    dsp->Reset (rate, block);
    std::vector<double> in ((size_t) block), out ((size_t) block);
    AudioBuffer<float> result (2, length);

    for (int start = 0; start < length; start += block)
    {
        const int n = jmin (block, length - start);
        for (int i = 0; i < n; ++i)
            in[(size_t) i] = audio.getSample (0, start + i) * volts / inFs;
        double* inPtr = in.data();
        double* outPtr = out.data();
        dsp->process (&inPtr, &outPtr, n);
        for (int i = 0; i < n; ++i)
        {
            const float v = (float) (out[(size_t) i] * outFs / volts);
            result.setSample (0, start + i, v);
            result.setSample (1, start + i, v);
        }
    }

    const File outFile = cwd.getChildFile (args[2]);
    outFile.deleteFile();
    std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (outFile);
    WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
        return 1;
    writer->writeFromAudioSampleBuffer (result, 0, length);
    return 0;
}
