#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

//==============================================================================
/** Registratore e misuratore dell'uscita (canali 1-2, cioè quello che senti).

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
            error = "No audio device is running.";
            return false;
        }

        auto folder = getDownloadsFolder();
        folder.createDirectory();

        const auto name = "ChainRig " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
        auto file = folder.getNonexistentChildFile (name, ".wav", false);

        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

        if (stream == nullptr)
        {
            error = "Could not create the file in " + folder.getFullPathName();
            return false;
        }

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (24));

        if (writer == nullptr)
        {
            error = "Could not write the WAV file.";
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

    /** Interfaccia: picco (lineare) del canale dall'ultima lettura, poi azzera. */
    float takePeak (int channel)             { return peaks[juce::jlimit (0, 1, channel)].exchange (0.0f); }

    /** Interfaccia: vero se almeno un campione ha raggiunto 0 dBFS dall'ultima lettura. */
    bool takeClipped()                       { return clipped.exchange (false); }

    /** Diagnostica (interfaccia): carico massimo di un blocco dall'ultima lettura (1.0 = tutto il tempo disponibile). */
    float takeMaxLoad()                      { return maxLoad.exchange (0.0f); }

    /** Livelli massimi dall'ultima lettura (lineari): ingresso della chitarra e uscita. */
    void setInputChannel (int ch)            { inputChannel = juce::jmax (0, ch); }
    float takeInputPeak()                    { return inputPeak.exchange (0.0f); }
    float takeOutputPeak()                   { return outputPeakDiag.exchange (0.0f); }
    int getOverloads() const                 { return overloads.load(); }
    int getLateCallbacks() const             { return lateCallbacks.load(); }

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
        // Livello d'ingresso (canale della chitarra), prima della catena
        if (const int ch = inputChannel.load(); ch < numInputs && inputs[ch] != nullptr)
        {
            const auto r = juce::FloatVectorOperations::findMinAndMax (inputs[ch], numSamples);
            const float p = juce::jmax (-r.getStart(), r.getEnd());
            float cur = inputPeak.load();
            while (p > cur && ! inputPeak.compare_exchange_weak (cur, p)) {}
        }

        // Diagnostica: quanto tempo prende la catena rispetto a quello disponibile per il blocco
        const auto startTicks = juce::Time::getHighResolutionTicks();
        const double rate = sampleRate.load();
        const double budget = rate > 0.0 ? (double) numSamples / rate : 0.0;

        if (lastStartTicks != 0 && budget > 0.0
             && (double) (startTicks - lastStartTicks) * secondsPerTick > budget * 1.8)
            ++lateCallbacks;   // il blocco è arrivato in ritardo: il sistema non ha chiamato in tempo

        lastStartTicks = startTicks;

        player.audioDeviceIOCallbackWithContext (inputs, numInputs, outputs, numOutputs, numSamples, context);

        if (budget > 0.0)
        {
            const float load = (float) ((double) (juce::Time::getHighResolutionTicks() - startTicks) * secondsPerTick / budget);

            float current = maxLoad.load();
            while (load > current && ! maxLoad.compare_exchange_weak (current, load)) {}

            if (load > 1.0f)
                ++overloads;   // la catena ha impiegato più del tempo del blocco: buco nell'audio
        }

        if (numOutputs <= 0 || outputs[0] == nullptr)
            return;

        const float* channels[2] = { outputs[0], (numOutputs > 1 && outputs[1] != nullptr) ? outputs[1] : outputs[0] };

        // Misuratore d'uscita: picco del blocco, tenuto finché l'interfaccia non lo legge
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (channels[ch], numSamples);
            const float blockPeak = juce::jmax (-range.getStart(), range.getEnd());

            float current = peaks[ch].load();
            while (blockPeak > current && ! peaks[ch].compare_exchange_weak (current, blockPeak)) {}

            float diag = outputPeakDiag.load();
            while (blockPeak > diag && ! outputPeakDiag.compare_exchange_weak (diag, blockPeak)) {}

            if (blockPeak >= 1.0f)
                clipped = true;
        }

        // Mai bloccare il thread audio: se start/stop tiene il lock, salta questo blocco
        const juce::ScopedTryLock sl (writerLock);

        if (! sl.isLocked() || activeWriter == nullptr)
            return;

        if (activeWriter->write (channels, numSamples))
            samplesWritten += numSamples;
        else
            ++droppedBlocks;   // coda piena: il disco non sta al passo
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        sampleRate = device->getCurrentSampleRate();
        lastStartTicks = 0;
        player.audioDeviceAboutToStart (device);
    }

    void audioDeviceStopped() override                         { player.audioDeviceStopped(); }
    void audioDeviceError (const juce::String& message) override { player.audioDeviceError (message); }

private:
    juce::AudioIODeviceCallback& player;
    juce::TimeSliceThread writerThread { "WAV recording" };

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::CriticalSection writerLock;
    juce::AudioFormatWriter::ThreadedWriter* activeWriter = nullptr;

    juce::File currentFile, lastSavedFile;
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<juce::int64> samplesWritten { 0 };
    std::atomic<int> droppedBlocks { 0 };

    std::atomic<float> peaks[2] { 0.0f, 0.0f };

    // diagnostica dei buchi audio
    const double secondsPerTick = 1.0 / (double) juce::Time::getHighResolutionTicksPerSecond();
    juce::int64 lastStartTicks = 0;           // solo thread audio
    std::atomic<float> maxLoad { 0.0f };
    std::atomic<int> overloads { 0 }, lateCallbacks { 0 };
    std::atomic<int> inputChannel { 0 };
    std::atomic<float> inputPeak { 0.0f }, outputPeakDiag { 0.0f };
    std::atomic<bool> clipped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
};
