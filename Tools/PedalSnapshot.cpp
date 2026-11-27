// Disegna la finestra di un pedale integrato in un PNG, senza aprire l'app (per controllarne l'aspetto):
//   pedal_snapshot "<identifier>" out.png [gain=7 ...] [--scale 2]

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/Pedals/Pedals.h"
#include "../Source/Look.h"

using namespace juce;

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;

    StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (String::fromUTF8 (argv[i]));

    if (args.size() < 2)
    {
        std::cerr << "uso: pedal_snapshot \"<identifier>\" out.png [param=valore ...] [--scale 2]\n";
        return 2;
    }

    auto* entry = Pedals::find (args[0]);
    if (entry == nullptr)
    {
        std::cerr << "pedale sconosciuto: " << args[0] << "\n";
        return 2;
    }

    float scale = 2.0f;
    if (const int i = args.indexOf ("--scale"); i >= 0 && i + 1 < args.size())
        scale = args[i + 1].getFloatValue();

    auto pedal = entry->create();
    for (auto& a : args)
        if (a.contains ("="))
            for (auto* k : pedal->getKnobs())
                if (k->getParameterID() == a.upToFirstOccurrenceOf ("=", false, false))
                    k->set (a.fromFirstOccurrenceOf ("=", false, false).getFloatValue());

    StageLookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        std::unique_ptr<AudioProcessorEditor> editor (pedal->createEditor());
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

        const File file = File::getCurrentWorkingDirectory().getChildFile (args[1]);
        file.deleteFile();
        FileOutputStream out (file);
        PNGImageFormat png;
        if (! out.openedOk() || ! png.writeImageToStream (image, out))
        {
            std::cerr << "non riesco a scrivere " << args[1] << "\n";
            return 1;
        }
        std::cout << args[1] << ": " << editor->getWidth() << " x " << editor->getHeight() << " (x" << scale << ")\n";
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
    return 0;
}
