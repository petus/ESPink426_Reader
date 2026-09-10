# Čtečka eknih na ESPink 4.26

Firmware, který z vývojové desky **LaskaKit ESPink-4.26** udělá čtečku
elektronických knih. Knihy nahrajete přes WiFi z prohlížeče nebo přímo na
microSD kartu, EPUB si čtečka převede sama a knihy vybíráte nebo listujete v knize třemi tlačítky na desce.

Navrhl [chiptron.cz](https://chiptron.cz) a Claude AI.

## Co potřebujete

- **[LaskaKit ESPink-4.26 ESP32-S3 e-Paper](https://www.laskakit.cz/laskakit-espink-4-26-esp32-s3-e-paper/)**
  s displejem 4,26" 800 × 480, verze 1.4
- **microSD kartu** naformátovanou na FAT32
- **LiPol baterii** s konektorem JST-PH
- USB-C kabel
- nebo **[tuhle stavebnici, kde je všechno](https://www.laskakit.cz/laskakit-live-4-26-e-paper-stavebnice-s-wifi-pro---zivy-obraz-/)**

Karty nad 32 GB se z výroby dodávají jako exFAT a čtečka je nepřečte.
Přeformátujte je na FAT32.

---

# Pro začátečníky

Nemusíte nic programovat. Stačí nahrát hotový soubor a vložit kartu.

## Nahrání firmwaru

Použijte webový nástroj **[esp32flasher.chiptron.cz](https://esp32flasher.chiptron.cz)**.
Běží přímo v prohlížeči, nic se neinstaluje.

1. Stáhněte si soubor `ESPink426_Reader_vX.bin` z tohoto repozitáře, složka Releases.
2. Otevřete [esp32flasher.chiptron.cz](https://esp32flasher.chiptron.cz)
   v prohlížeči **Chrome nebo Edge**. Firefox a Safari to neumí.
3. Připojte desku k počítači USB-C kabelem.
4. Klikněte na **Připojit** a v okně vyberte port desky.
5. Vyberte stažený soubor `.bin`.
6. Klikněte na **Nahrát** a počkejte, než se to dokončí.
7. Odpojte a znovu připojte napájení.

Když se deska v seznamu portů neobjeví, zkuste jiný USB-C kabel. Některé
kabely vedou jen napájení, ne data.

## Příprava karty

Na kartě vytvořte složku `books`. Do ní patří knihy. Kartu pak zasuňte do
čtečky.

## Jak dostat knihy do čtečky

Máte dvě možnosti a obě končí stejně úspěšně, tedy souborem na kartě.

**Přes kartu.** Vyndejte kartu, zasuňte ji do počítače a nakopírujte knihy do
složky `books`. Funguje `.epub` i `.book` (o typu souboru *.book níže - část "Formáty knih")

**Přes WiFi.** Podržte tlačítko PUSH pět sekund při zapnutí/resetu čtečky. Na
displeji se objeví, kam se připojit. Otevřete v prohlížeči uvedenou IP adresu a
knihy nahrajete přetažením do stránky. Čtečka je uloží na kartu, takže
výsledek je stejný, jako byste kartu vyndali. Detailně níže - část "Nahrávání knih přes WiFi podrobně".

## Ovládání

Deska má tři tlačítka. Ovládání má tři úrovně.

**Záložky** se přepínají tlačítky UP a DOWN. Jsou tři: Rozečteno,
Nepřečteno a Přečteno. Krátký stisk PUSH vstoupí do seznamu.

**Seznam knih** se prochází tlačítky UP a DOWN, držením se posouvá rychleji.
Krátký stisk PUSH otevře knihu, dlouhý stisk vrátí na záložky (min 2s).

**Čtení** se listuje tlačítky UP a DOWN. Krátký stisk PUSH mění velikost
písma, dlouhý stisk (min 2s) vrátí do seznamu.

Dlouhý stisk znamená podržení na dvě sekundy.

U každé knihy vidíte, kolik procent máte přečteno. Pozice se ukládá po každé
stránce, takže po vypnutí pokračujete tam, kde jste skončili.

## Formáty knih

Čtečka čte vlastní formát `.book`. Je to obyčejný text v UTF-8, kde každý
řádek je jeden odstavec.

**EPUB nahrajete taky.** Při prvním otevření si ho čtečka převede sama, což
trvá 5 až 10 sekund podle velikosti. Na kartě pak leží oba soubory, v seznamu
se ale zobrazí jen výsledek, tedy *.book se stejným názvem jako vložený ePub.

## Nahrávání knih přes WiFi podrobně

Podržte PUSH pět sekund při zapnutí. Na displeji se objeví, kam se připojit. 
Deska vytvoří vlastní WiFi, na ní se připojíte a můžete tam zadat SSID a heslo vaší domácí WiFi sítě.

Když máte uloženou domácí síť, čtečka se připojí k ní a ukáže IP adresu nebo zadáte do prohlížeče "ctecka.local" (tuhle vlastnost nepodporují všechny routery). 
Jinak vytvoří vlastní hotspot a ukáže název sítě i heslo. Na telefonu se
stránka většinou otevře sama.

Na stránce nahrajete knihy přetažením nebo výběrem, smažete je, označíte jako
přečtené a nastavíte velikost písma i dobu do uspání. Domácí síť si můžete
vybrat ze seznamu dostupných, nemusíte psát název ručně.

Režim se sám vypne po pěti minutách nečinnosti. Dlouhé nahrávání ho nepřeruší. WiFi funguje pouze v případě, kdy při zapnutí/resetu čtečky držíte tlačítko PUSH alespoň 5s. Jinak je WiFi vypnutá z důvodu úspory energie.

## Úspora energie

Při čekání na stisk je deska v light sleepu. Po nastavené době nečinnosti,
ve výchozím stavu po pěti minutách, přejde do deep sleepu. Z něj ji probudí
tlačítko RESET nebo vypnutí a zapnutí napájení čtečky.

Tlačítka UP, PUSH a DOWN jsou na pinech, které v režimu hlubokého spánku nefungují - fungují jen během aktivního čtení. Je to omezení desky, ne firmwaru.

WiFi i Bluetooth jsou při čtení vypnuté. WiFi se zapne jen v režimu
nahrávání knih.

Čtečka hlídá napětí baterie. Pod 3,5 V se uspí a uloží pozici, aby se
článek nepodvybil. Režim nahrávání se nespustí pod 3,6 V.

Deska má vypínač. Můžete ji vypnout kdykoli, zápisy na kartu jsou dělané tak,
aby to nevadilo.

---

# Pro pokročilé

## Překlad v Arduino IDE

Nainstalujte podporu ESP32 a tyto knihovny:

- **GxEPD2**
- **Adafruit GFX Library**
- **U8g2_for_Adafruit_GFX**

Nastavení desky:

| Položka | Hodnota |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| USB CDC On Boot | **Disabled** |
| PSRAM | není potřeba |

Pokud vám IDE hlásí, že je sketch příliš velký, máte špatně Partition Scheme.
Výchozí schéma dává aplikaci jen 1,2 MB, což nestačí.

Když IDE hlásí, že našlo víc knihoven `SD.h`, smažte samostatnou knihovnu SD
ze složky `Arduino15/libraries`. Ta z jádra ESP32 je správná.

## Převod EPUB na počítači

Čtečka si EPUB převede sama, ale u knih s netypickou strukturou se to nemusí
povést. Pro takové případy je ve složce `tools` skript pro počítač.

```
pip install ebooklib beautifulsoup4 lxml
python epub2book.py kniha.epub
```

Vedle vstupu vznikne `kniha.book`, který zkopírujete na kartu do složky
`books`. Skript zvládne i více souborů najednou:

```
python epub2book.py C:\knihy\*.epub
```

Skript čte pořadí kapitol ze souboru `content.opf`, odstraňuje značky,
skripty a styly, převádí HTML entity a normalizuje českou diakritiku do
složené podoby. Ta poslední věc je důležitá, protože rozložená diakritika
by se na displeji zobrazila bez háčků.

## Ladicí výpisy

V souboru `config.h` je přepínač:

```c
#define RDR_DEBUG_SERIAL  1
```

Čtečka pak vypisuje na sériovou linku průběh startu, stisky tlačítek, dobu
sazby i překreslení a průběh konverze EPUB. Rychlost 115200 baud.

## Nastavení, které se dá měnit

Vše podstatné je v `config.h`:

| Konstanta | Význam |
|---|---|
| `RDR_ROTATION` | otočení displeje |
| `RDR_EPD_SPI_HZ` | takt SPI pro displej, výchozí 10 MHz |
| `RDR_FULL_REFRESH_EVERY` | po kolika překresleních plný refresh |
| `RDR_USE_LIGHT_SLEEP` | 0 přepne na polling, když by light sleep zlobil |
| `RDR_IDLE_TIMEOUT_MS` | výchozí doba do uspání |
| `RDR_FONT_BODY_S/M/L` | fonty pro tři velikosti písma |

Fonty musí být varianty `_te`. Varianty `_tf` pokrývají jen Latin-1 a české
háčky a kroužky by v nich chyběly.

## Formát .book

Textový soubor v UTF-8. Každý řádek je jeden odstavec. Prefixy určují typ:

```
T:Název knihy
A:Autor
C:Název kapitoly
H:Nadpis uvnitř kapitoly
Běžný odstavec bez prefixu.
```

Čtečka si text sama láme na řádky podle zvolené velikosti písma, takže
odstavce nemají mít vlastní zalomení.

## Odkazy

- Vývoj: [chiptron.cz](https://chiptron.cz)
- Deska: [LaskaKit ESPink-4.26](https://www.laskakit.cz/laskakit-espink-4-26-esp32-s3-e-paper/)
- microSD karty a LiPol baterie: [laskakit.cz](https://www.laskakit.cz)
- Nahrání firmwaru: [esp32flasher.chiptron.cz](https://esp32flasher.chiptron.cz)
