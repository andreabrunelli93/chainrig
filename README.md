# ChainRig

Host per suonare dal vivo: catena di 10 posti per plugin VST3, 4 pulsanti touch,
accordatore, comandi da pedaliera MIDI e preset. Fatto con JUCE 9 per Windows
(funziona anche su Mac e Linux).

## Cosa fa

- **10 posti in fila**: tocchi un posto e scegli un plugin dalla lista, che contiene solo i plugin scelti in **Scegli plugin**. Dallo stesso menu puoi aprire l'interfaccia del plugin, metterlo in bypass, sostituirlo, spostarlo o rimuoverlo. Puoi anche **trascinare** un posto: su un posto vuoto ci si sposta, su uno occupato si inserisce e gli altri scorrono. I pedali seguono i plugin. I posti vuoti vengono saltati.
- **Nome e tipo di ogni posto**: dal menu del posto puoi scegliere **Rinomina...** (per esempio "Twin clean" al posto di "TONE3000"; se lo lasci vuoto torna il nome del plugin) e **Tipo**: Amplificatore, Cassa / IR, Drive, Compressore, Equalizzatore, Modulazione, Delay, Riverbero, Altro effetto. Il tipo decide l'icona e l'etichetta del posto. Con "Automatico" viene ricavato dalla categoria e dal nome del plugin. Nome e tipo vengono salvati nel preset.
- **4 pulsanti grandi**: ognuno accende e spegne i posti che gli assegni. Esempio: BOOST controlla il posto 2. Un posto assegnato a un pulsante suona solo quando quel pulsante è acceso.
- **Accordatore**: lavora sull'ingresso, prima della catena, e silenzia l'uscita mentre accordi. Mostra la nota con il nome inglese e quello italiano, i cent e la frequenza.
- **Preset**: un file `.chainrig` per preset, salvato in `Documenti\ChainRig\Preset`. Ogni preset contiene i plugin con tutte le loro impostazioni, i bypass e le assegnazioni dei pulsanti. Si cambiano con `<` e `>` oppure da MIDI.
- **Bassa latenza**: usa i driver ASIO, inclusi in JUCE e attivi di default. Nella barra in alto vedi la latenza stimata e il carico della CPU.

## Compilare su Windows

Hai due strade.

**A) Sul Surface o su un PC Windows**
1. Installa **Visual Studio 2022 Community** con il carico di lavoro *"Sviluppo di applicazioni desktop con C++"*. Include già CMake.
2. Installa **Git**.
3. Apri il prompt *"Developer PowerShell for VS 2022"* nella cartella del progetto ed esegui:
   ```
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```
   La prima volta scarica JUCE da solo, quindi ci mette qualche minuto.
4. Trovi il programma in `build\ChainRig_artefacts\Release\ChainRig.exe`.

**B) Senza installare niente, con GitHub**
Carica la cartella in un repository GitHub, anche privato. Il workflow in `.github/workflows` compila l'eseguibile per Windows a ogni push. Lo scarichi da *Actions → ultima esecuzione → Artifacts → ChainRig-Windows*.

## Compilare su Mac (per provarlo)

Il codice è lo stesso di Windows. Su Mac usa Core Audio, quindi niente driver ASIO, e le Focusrite funzionano senza installare driver.

1. Installa gli strumenti da riga di comando di Xcode con `xcode-select --install` e CMake con `brew install cmake`. In alternativa scarica CMake da cmake.org.
2. Nella cartella del progetto esegui:
   ```
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j8
   ```
3. Apri `build/ChainRig_artefacts/Release/ChainRig.app`. Al primo avvio macOS chiede il permesso per il microfono: concedilo, altrimenti l'ingresso della chitarra resta muto. Se l'hai negato, lo riattivi da *Impostazioni di Sistema → Privacy e sicurezza → Microfono*.

Su Mac vengono caricati i plugin **VST3**, che trovi in `/Library/Audio/Plug-Ins/VST3`. Gli Audio Unit non sono attivati: quasi tutti i plugin, Valhalla, NAM e TONE3000 compresi, esistono anche in VST3.
Le impostazioni sono salvate in `~/Library/Application Support/ChainRig/ChainRig.settings` e i preset in `~/Documents/ChainRig/Preset`.

> I preset fatti su Mac aperti su Windows ritrovano i plugin cercandoli per nome e identificativo. I percorsi dei file `.nam` e delle IR, invece, cambiano tra un sistema e l'altro: sul Surface dovrai ricaricarli una volta e risalvare il preset.

## Tempo (BPM)

Tocca il nome del preset → **Tempo: 120 BPM...** per impostare il tempo dei delay e dei riverberi sincronizzati
(per esempio un delay a ottavi). Il tempo viene salvato nel preset e compare nella barra in alto.

## Livello d'uscita

La barra a destra mostra il livello d'uscita (L e R) da −48 a 0 dBFS. È verde fino a −12, gialla fino a −3 e rossa sopra.
La lineetta bianca indica il picco recente. In basso c'è il picco massimo in dB.
La spia **CLIP** diventa rossa quando un campione raggiunge 0 dBFS e resta accesa finché non tocchi la barra, così vedi
il clip anche se è passato mentre guardavi altrove. Toccando la barra si azzera anche il picco massimo. Lascia qualche
dB di margine: un picco intorno a −6 dB va bene.

## Registrare

Il pulsante **REC** in alto registra quello che esce dalla scheda (canali 1-2, cioè la catena completa) in un file WAV
stereo a 24 bit. Mentre registra diventa rosso e mostra il tempo. Toccalo di nuovo per fermare: il file viene salvato
nella cartella **Download** con nome `ChainRig AAAA-MM-GG HH-MM-SS.wav`, e il percorso compare per qualche secondo
nella barra in alto. Se cambi la scheda audio mentre registri, la registrazione viene chiusa e salvata.

## Primo avvio

1. **IMPOSTAZIONI → Scheda audio**: su Windows scegli il tipo **ASIO**, su Mac **CoreAudio**, e poi la tua scheda (Gigboard o altra interfaccia). Imposta 48 kHz e un buffer di 64 o 128 campioni.
2. **IMPOSTAZIONI → Ingresso chitarra**: scegli l'ingresso a cui è collegata la chitarra.
3. **IMPOSTAZIONI → Scegli plugin...**: compare l'elenco dei file VST3 delle cartelle dei plugin. Scrivi nel filtro
   (es. "valhalla", "tone"), spunta solo quelli che usi dal vivo e premi **APPLICA**. Vengono esaminati solo quelli spuntati,
   e quelli senza spunta vengono tolti dalla lista. **Nessuno** toglie la spunta a tutti quelli visibili. La scelta viene salvata.
   Ogni plugin viene esaminato in un processo separato: se si blocca per più di 30 secondi o va in crash, viene segnato
   come escluso e ChainRig continua. Il dettaglio è in `scan.log`, accanto alle impostazioni.
   **Avanzate...** apre la lista completa di JUCE, che serve per aggiungere altre cartelle.
4. Tocca un posto vuoto → **Scegli plugin**. Per esempio NAM nel posto 3, un caricatore di IR nel 4, Valhalla nel 7.
5. **MODIFICA** → tocca un pedale grande per scegliere i posti che controlla. Il pedale prende il nome di quei posti (per esempio "KLONE CENTAUR" o "KLONE + DELAY"), quindi per cambiarlo rinomina il posto. Il nome proprio del pedale (BOOST, DRIVE...) compare solo quando non controlla posti. Poi tocca di nuovo **MODIFICA** per uscire.
6. Tocca il nome del preset → **Salva come...**

Per il palco: **IMPOSTAZIONI → Schermo intero**. Poi metti un collegamento a `ChainRig.exe` nella cartella di avvio automatico di Windows: premi Win+R e scrivi `shell:startup`. Così il programma parte già a schermo intero sull'ultimo preset.

## Pedaliera MIDI (USB o Bluetooth)

Tutti gli ingressi MIDI vengono attivati automaticamente, anche quelli collegati dopo l'avvio.

| Comando | Messaggio predefinito |
|---|---|
| Pulsanti 1-4 | CC 80, 81, 82, 83 |
| Accordatore | CC 84 |
| Preset successivo / precedente | CC 85 / CC 86 |
| Preset n. N (in ordine alfabetico) | Program Change N |

Nel menu **IMPOSTAZIONI → Pedaliera MIDI** scegli come interpretare i messaggi:
- **"Ogni pressione alterna"**: per pedaliere che mandano 127 quando premi e 0 quando rilasci.
- **"Il valore decide"**: per pedaliere che mandano 127 o 0 alternati.

Per cambiare i numeri CC, chiudi il programma e modifica
`%APPDATA%\ChainRig\ChainRig.settings`. Le voci da aggiungere o cambiare sono
`cc_button1`…`cc_button4`, `cc_tuner`, `cc_nextPreset` e `cc_prevPreset`.

**Bluetooth**: le pedaliere vendute con la chiavetta USB (ad esempio M-Vave Chocolate) funzionano subito. Per il Bluetooth LE diretto di Windows compila con
`-DCHAINRIG_BLUETOOTH_MIDI=ON` e abbina prima la pedaliera dalle impostazioni Bluetooth di Windows.

## Limiti noti (versione 0.1)

- Quando carichi o sostituisci un plugin ci può essere un attimo di silenzio. Il bypass e i pulsanti, invece, non fanno click.
- Se un plugin va in crash durante l'uso, si chiude tutto il programma, come in quasi tutti gli host semplici.
- Alcuni plugin accettano solo un ingresso mono e danno un'uscita stereo. Se ne metti uno in bypass, il canale destro di quel posto resta muto. Se il plugin ha una versione stereo, usa quella.
- I preset ricordano quale plugin c'è in ogni posto. Se su un altro computer un plugin non è installato, quel posto resta vuoto e il programma te lo segnala.

## Struttura del codice

- `Source/Engine.*`: il motore. Gestisce la scheda audio, il grafo di elaborazione (ingresso → accordatore → posti → uscita), i bypass, il MIDI e i preset.
- `Source/Tuner.h`: il nodo dell'accordatore, con una FIFO lock-free tra il thread audio e l'interfaccia, e il rilevamento del pitch con l'algoritmo YIN.
- `Source/MainComponent.*`: l'interfaccia touch, con i posti, i pulsanti grandi, i menu e le finestre dei plugin.
- `Source/Main.cpp`: l'applicazione e la finestra principale.
