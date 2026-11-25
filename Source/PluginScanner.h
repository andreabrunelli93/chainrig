#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/** Ricerca dei plugin in un processo separato.

    Ogni file di plugin viene aperto da una seconda copia di ChainRig avviata con
        --scan-plugin <formato> <file> <file-risultato>
    Se il plugin si blocca o va in crash, muore solo quella copia: il programma
    principale aspetta al massimo `timeoutMs`, mette il file tra gli esclusi
    e passa al successivo. */
namespace PluginScanner
{
    inline constexpr const char* argument = "--scan-plugin";
    inline constexpr int timeoutMs = 30000;

    /** Vero se questo processo è stato avviato solo per esaminare un plugin. */
    inline bool isScannerProcess()
    {
        return juce::JUCEApplicationBase::getCommandLineParameterArray().contains (argument);
    }

    /** Processo figlio: esamina il file e scrive le PluginDescription in XML.
        Restituisce il codice di uscita (0 = esaminato, anche se non contiene plugin). */
    inline int runScannerProcess()
    {
        const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
        const int index = args.indexOf (argument);

        if (index < 0 || index + 3 >= args.size())
            return 2;

        const auto formatName = args[index + 1];
        const auto fileOrIdentifier = args[index + 2];
        const juce::File resultFile (args[index + 3]);

        juce::AudioPluginFormatManager formatManager;
        juce::addDefaultFormatsToManager (formatManager);

        juce::XmlElement root ("PLUGINS");

        for (auto* format : formatManager.getFormats())
        {
            if (format->getName() != formatName)
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile (found, fileOrIdentifier);

            for (auto* desc : found)
                if (desc != nullptr)
                    root.addChildElement (desc->createXml().release());
        }

        // Il file viene scritto solo alla fine: se manca, il figlio è morto a metà
        return root.writeTo (resultFile) ? 0 : 3;
    }

    //==============================================================================
    /** Lato programma principale: KnownPluginList lo chiama dal thread della ricerca. */
    /** Registro della ricerca, accanto alle impostazioni (scan.log). */
    inline juce::File getLogFile()
    {
       #if JUCE_MAC
        auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("Application Support");
       #else
        auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #endif
        return folder.getChildFile ("ChainRig").getChildFile ("scan.log");
    }

    inline void log (const juce::String& line)
    {
        const auto file = getLogFile();
        file.getParentDirectory().createDirectory();
        file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S  ") + line + "\n");
    }

    class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
    {
    public:
        bool findPluginTypesFor (juce::AudioPluginFormat& format,
                                 juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            const auto start = juce::Time::getMillisecondCounter();
            const bool ok = scan (format, result, fileOrIdentifier);
            const auto seconds = juce::String ((juce::Time::getMillisecondCounter() - start) / 1000.0, 1);

            log ((ok ? juce::String ("OK    ") : juce::String ("ESCLUSO")) + "  " + seconds + " s  "
                 + juce::String (result.size()) + " plugin  " + fileOrIdentifier
                 + (lastProblem.isNotEmpty() ? "  (" + lastProblem + ")" : juce::String()));
            return ok;
        }

        void scanFinished() override
        {
            log ("--- fine ricerca ---");
        }

    private:
        juce::String lastProblem;

        bool fail (const juce::String& why)
        {
            lastProblem = why;
            return false;
        }

        bool scan (juce::AudioPluginFormat& format,
                   juce::OwnedArray<juce::PluginDescription>& result,
                   const juce::String& fileOrIdentifier)
        {
            lastProblem = {};
            juce::TemporaryFile resultFile (".xml");

            const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            juce::StringArray command { exe.getFullPathName(), argument, format.getName(),
                                        fileOrIdentifier, resultFile.getFile().getFullPathName() };

            juce::ChildProcess child;

            if (! child.start (command, 0))
                return fail ("impossibile avviare " + exe.getFullPathName());

            // Attende a piccoli passi per poter interrompere se l'utente preme Annulla
            for (int waited = 0; child.isRunning(); waited += 100)
            {
                const bool cancelled = shouldExit() || juce::Thread::currentThreadShouldExit();

                if (cancelled || waited >= timeoutMs)
                {
                    child.kill();

                    if (cancelled)
                    {
                        lastProblem = "annullato";
                        return true;       // annullato: niente esclusione
                    }

                    return fail ("bloccato oltre " + juce::String (timeoutMs / 1000) + " s");
                }

                juce::Thread::sleep (100);
            }

            if (const auto code = child.getExitCode(); code != 0)
                return fail ("uscito con codice " + juce::String (code));

            auto xml = juce::parseXML (resultFile.getFile());

            if (xml == nullptr || ! xml->hasTagName ("PLUGINS"))
                return fail ("nessun risultato, probabile crash");   // crash a metà (su macOS un segnale può dare codice 0)

            for (auto* e : xml->getChildIterator())
            {
                juce::PluginDescription desc;

                if (desc.loadFromXml (*e))
                    result.add (new juce::PluginDescription (desc));
            }

            if (result.isEmpty())
                lastProblem = "nessun plugin nel file";

            return true;
        }
    };
}
