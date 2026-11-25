#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

//==============================================================================
/** Registratore dell'uscita (canali 1-2, cioè quello che senti).

    Sta tra la scheda audio e l'AudioProcessorPlayer: inoltra ogni callback al player
    e poi copia il buffer d'uscita in un file WAV 24 bit. La scrittura su disco avviene
    su un thread a parte (ThreadedWriter), quindi il thread audio non si blocca mai.
    start()/stop() vanno chiamati dal thread dell'interfaccia. */
class Recorder final : public juce::AudioIODeviceCallback
{
public:
    explicit Recorder (juce::AudioIODeviceCallback& playerToWrap) : player (playerToWrap)
    {
        writerThread.startThread();
    }

    ~Recorder() override
    {
        stop();
        writerThread.stopThread (2000);
    }

    /** Cartella Download dell'utente (Mac e Windows). */
    static juce::File getDownloadsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    }

    bool start (juce::String& error)
    {
        stop();

        const double rate = sampleRate.load();

        if (rate <= 0.0)
        {
            error = "Nessuna scheda audio attiva.";
            return false;
        }

        auto folder = getDownloadsFolder();
        folder.createDirectory();

        const auto name = "ChainRig " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
        auto file = folder.getNonexistentChildFile (name, ".wav", false);

        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

        if (stream == nullptr)
        {
            error = "Non riesco a creare il file in " + folder.getFullPathName();
            return false;
        }

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (24));

        if (writer == nullptr)
        {
            error = "Non riesco a scrivere il file WAV.";
            file.deleteFile();
            return false;
        }

        threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 65536);
        currentFile = file;
        samplesWritten = 0;
        droppedBlocks = 0;

        const juce::ScopedLock sl (writerLock);
        activeWriter = threadedWriter.get();
        return true;
    }

    /** Ferma e chiude il file. Restituisce il file appena salvato (vuoto se non registrava). */
    juce::File stop()
    {
        {
            const juce::ScopedLock sl (writerLock);
            activeWriter = nullptr;
        }

        threadedWriter.reset();   // svuota la coda e chiude il WAV

        if (currentFile != juce::File())
            lastSavedFile = currentFile;

        currentFile = juce::File();
        return lastSavedFile;
    }

    bool isRecording() const                 { return threadedWriter != nullptr; }
    juce::File getCurrentFile() const        { return currentFile; }
    juce::File getLastSavedFile() const      { return lastSavedFile; }
    int getDroppedBlocks() const             { return droppedBlocks.load(); }

    double getSeconds() const
    {
        const double rate = sampleRate.load();
        return rate > 0.0 ? (double) samplesWritten.load() / rate : 0.0;
    }

    //==============================================================================
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override
    {
        player.audioDeviceIOCallbackWithContext (inputs, numInputs, outputs, numOutputs, numSamples, context);

        if (numOutputs <= 0 || outputs[0] == nullptr)
            return;

        // Mai bloccare il thread audio: se start/stop tiene il lock, salta questo blocco
        const juce::ScopedTryLock sl (writerLock);

        if (! sl.isLocked() || activeWriter == nullptr)
            return;

        const float* channels[2] = { outputs[0], (numOutputs > 1 && outputs[1] != nullptr) ? outputs[1] : outputs[0] };

        if (activeWriter->write (channels, numSamples))
            samplesWritten += numSamples;
        else
            ++droppedBlocks;   // coda piena: il disco non sta al passo
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        sampleRate = device->getCurrentSampleRate();
        player.audioDeviceAboutToStart (device);
    }

    void audioDeviceStopped() override                         { player.audioDeviceStopped(); }
    void audioDeviceError (const juce::String& message) override { player.audioDeviceError (message); }

private:
    juce::AudioIODeviceCallback& player;
    juce::TimeSliceThread writerThread { "Registrazione WAV" };

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::CriticalSection writerLock;
    juce::AudioFormatWriter::ThreadedWriter* activeWriter = nullptr;

    juce::File currentFile, lastSavedFile;
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<juce::int64> samplesWritten { 0 };
    std::atomic<int> droppedBlocks { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
};
