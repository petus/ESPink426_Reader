# Changelog

Verzování podle [semver](https://semver.org/lang/cs/).

## [0.1.2]

### Čtení

- Dlouhé odstavce se zobrazí celé i u velkého písma.
- Měkké dělení slov se nezobrazuje jako spojovník uprostřed slova.
- Na konci knihy vrátí UP na poslední stránku.

### Knihovna

- Naposledy čtená kniha je v záložce Rozečteno vždy nahoře.
- Dlouhé české názvy se v seznamu zkracují po celých znacích.
- Pozice čtení přežije výpadek napájení i v okamžiku ukládání.

### Ovládání

- Stisky dvou různých tlačítek během překreslování se zpracují oba.
- Z chybové hlášky vrátí libovolné tlačítko do seznamu knih.

### Konverze EPUB

- Knihy až s 512 soubory v archivu. Obrázky, fonty a styly se přeskakují.
- Kapitoly se poznají podle typu v `content.opf`, nejen podle přípony.
- Cesty s `../` v `content.opf`.
- `content.opf` až do 96 kB.
- Samostatný znak `&` v textu se zachová.
- Entity v názvu a autorovi knihy.
- Stabilnější převod velkých knih, dekompresor je na heapu.

### WiFi

- Znovu nahraný EPUB nahradí starší převod, kniha se převede znovu.

## [0.1.1]

### Čtení

- Čtení knih ve formátu `.book` z microSD karty.
- Tři velikosti písma. Přepínají se krátkým stiskem PUSH při čtení.
- Text na výšku, 480 × 800.
- Dělení odstavců mezi stránkami. Dlouhá slova se dělí spojovníkem.
- Nová kapitola začíná na nové stránce.
- Česká diakritika, fonty U8g2 s Latin Extended-A.
- Postup čtení v procentech.
- Pozice čtení se ukládá po každé stránce na SD kartu.
- Obrazovka na konci knihy. Kniha se označí jako přečtená.

### Knihovna

- Seznam knih se třemi záložkami: Rozečteno, Nepřečteno, Přečteno.
- Stav se ukládá ke každé knize zvlášť do souboru `.pos`.
- Rozečtené knihy jsou řazené podle posledního čtení, ostatní abecedně.
- Rolování seznamu s ukazatelem polohy.
- Po zavření knihy se seznam přepne na záložku, kam se kniha přesunula.

### Ovládání

- Tři tlačítka: UP, PUSH, DOWN.
- UP a DOWN listují, PUSH potvrzuje, dlouhý stisk PUSH jde o úroveň zpět.
- Autorepeat při držení UP a DOWN v seznamu.
- Stisk během překreslování displeje se nezahodí.

### Konverze EPUB

- Nahraný `.epub` se převede na `.book` přímo ve čtečce.
- Konverze se spustí při otevření knihy. Trvá 5 až 10 sekund.
- Na kartě zůstane `.epub` i `.book`. V seznamu se zobrazí jen `.book`.
- Čte se pořadí kapitol z `content.opf`, název a autor z metadat.
- Převod entit, normalizace diakritiky.

### WiFi

- Režim nahrávání knih se spustí podržením PUSH 5 sekund při zapnutí.
- Připojí se k uložené domácí síti, jinak vytvoří vlastní hotspot.
- Na displeji je název sítě, heslo a IP adresa.
- Webová stránka v češtině, funguje i na mobilu.
- Nahrávání `.book` i `.epub`, přetažením nebo výběrem.
- Mazání knih, označování přečtených a nepřečtených.
- Nastavení velikosti písma a doby do uspání.
- Vyhledávání dostupných sítí včetně síly signálu.
- mDNS `ctecka.local` a captive portal.
- Kontrola úplnosti přenosu. Neúplný soubor se smaže.
- Automatické vypnutí po 5 minutách nečinnosti.

### Napájení

- Light sleep při čekání na tlačítko.
- Deep sleep po nastavené době nečinnosti.
- WiFi a Bluetooth vypnuté při čtení.
- Hlídání napětí baterie. Pod 3,5 V se čtečka uspí.
- WiFi se nespustí pod 3,6 V.
- Zápisy na kartu jsou atomické, čtečku lze kdykoli vypnout vypínačem.

### Displej

- Partial refresh 0,5 s, plný refresh 1,7 s.
- Plný refresh každých 8 překreslení.
- SPI na 10 MHz.
- Stavový řádek: stav zařízení, pozice, baterie.

### Nástroje

- `epub2book.py` pro převod EPUB na počítači.
