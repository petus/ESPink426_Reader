// ============================================================================
//  epub.h  -  Konverze EPUB -> .book primo ve ctecce
// ----------------------------------------------------------------------------
//  POSTUP
//   1. otevrit archiv, najit content.opf (pres META-INF/container.xml,
//      s zalohou "prvni soubor s priponou .opf")
//   2. z .opf precist nazev, autora a SPINE, tedy poradi ctenych dokumentu
//   3. kazdy dokument streamovane rozbalit a prohnat parserem HTML,
//      vysledek rovnou zapisovat do .book
//
//  PAMET
//   Nic se nedrzi cele. Nejvetsi HTML v testovanych knihach ma po rozbaleni
//   252 kB, coz by se do haldy (~300 kB) neveslo spolu s 32 kB slovnikem
//   dekomprese. Vse tece po kouscich.
//
//  ODOLNOST
//   Zapisuje se do docasneho souboru a az hotovy vysledek se prejmenuje.
//   Kdyz uzivatel vypne napajeni uprostred konverze, nezustane na karte
//   nedopsany .book, ktery by se tvaril jako platna kniha.
//
//  WATCHDOG
//   Konverze bezi desitky sekund v jedne smycce. Vsude, kde se cyklí,
//   je delay(0), jinak by uloha vyhladovela idle task a deska by se
//   restartovala.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "config.h"
#include "zip.h"
#include "inflate.h"
#include "htmltext.h"
#include "log.h"

// Callback pro prubeh: kolikaty dokument z kolika, at se da ukazat na displeji.
typedef void (*RdrConvProgress)(int done, int total, void *ctx);

#if RDR_HAVE_MINIZ

class RdrEpub {
public:
  // Prevede EPUB na .book. Vraci true pri uspechu.
  bool convert(const String &epubPath, const String &bookPath,
               RdrConvProgress onProgress, void *ctx) {
    uint32_t tStart = millis();

    if (!_zip.open(epubPath)) return fail("Soubor není platný EPUB.");

    // --- najit a precist content.opf --------------------------------------
    int opf = findOpf();
    if (opf < 0) { _zip.close(); return fail("V knize chybí content.opf."); }
    RDR_LOG("epub: opf = '%s'", _zip.entry(opf).name.c_str());

    String opfDir = dirOf(_zip.entry(opf).name);
    String title, author;
    static String spine[RDR_ZIP_MAX_ENTRIES];
    int nSpine = readOpf(opf, opfDir, title, author, spine, RDR_ZIP_MAX_ENTRIES);
    if (nSpine <= 0) { _zip.close(); return fail("Nepodařilo se přečíst obsah knihy."); }
    RDR_LOG("epub: '%s' / '%s', %d dokumentu", title.c_str(), author.c_str(), nSpine);

    // --- zapis do docasneho souboru ---------------------------------------
    String tmp = bookPath + ".tmp";
    SD.remove(tmp);
    File out = SD.open(tmp.c_str(), FILE_WRITE);
    if (!out) { _zip.close(); return fail("Na kartu nelze zapisovat."); }

    if (title.length())  out.println("T:" + title);
    if (author.length()) out.println("A:" + author);

    if (!_inf.begin()) { out.close(); SD.remove(tmp); _zip.close();
                         return fail("Nedostatek paměti pro dekompresi."); }

    // --- projit dokumenty v poradi cteni -----------------------------------
    uint32_t written = 0;
    int okDocs = 0;
    for (int i = 0; i < nSpine; i++) {
      if (onProgress) onProgress(i + 1, nSpine, ctx);

      int e = _zip.find(spine[i]);
      if (e < 0) { RDR_LOG("epub: dokument '%s' v archivu neni", spine[i].c_str()); continue; }

      uint32_t off = _zip.dataOffset(e);
      if (!off) continue;

      _html.begin(&out, true);          // prvni nadpis v dokumentu = kapitola
      g_sinkHtml = &_html;
      int32_t got = _inf.run(_zip.file(), off, _zip.entry(e).compSize,
                             _zip.entry(e).method, sinkToHtml, nullptr);
      _html.finish();

      if (got < 0) { RDR_LOG("epub: dokument %d se nepodarilo rozbalit", i); continue; }
      written += (uint32_t)got;
      okDocs++;
      delay(0);
    }

    out.flush();
    uint32_t size = out.size();
    out.close();
    _zip.close();

    if (okDocs == 0 || size < 128) {
      SD.remove(tmp);
      return fail("V knize se nepodařilo najít žádný text.");
    }

    SD.remove(bookPath);
    if (!SD.rename(tmp, bookPath)) { SD.remove(tmp); return fail("Výsledek nelze uložit."); }

    RDR_LOG("epub: hotovo za %lu ms, %d/%d dokumentu, zpracovano %lu B -> %lu B",
            (unsigned long)(millis() - tStart), okDocs, nSpine,
            (unsigned long)written, (unsigned long)size);
    return true;
  }

  const String& error() const { return _err; }

private:
  RdrZip      _zip;
  RdrInflate  _inf;
  RdrHtmlText _html;
  String      _err;

  static RdrHtmlText *g_sinkHtml;
  static bool sinkToHtml(const uint8_t *d, size_t n, void *) {
    return g_sinkHtml->feed(d, n);
  }

  bool fail(const char *msg) { _err = msg; RDR_LOG("epub: CHYBA - %s", msg); return false; }

  static String dirOf(const String &path) {
    int sl = path.lastIndexOf('/');
    return (sl < 0) ? String("") : path.substring(0, sl + 1);
  }

  // Rozbali malou polozku do Stringu (jen pro opf a container.xml).
  String readSmall(int e, uint32_t maxLen) {
    if (e < 0) return "";
    if (_zip.entry(e).uncompSize > maxLen) return "";
    uint32_t off = _zip.dataOffset(e);
    if (!off) return "";

    g_text = "";
    g_textLimit = maxLen;
    if (!_inf.begin()) return "";
    int32_t got = _inf.run(_zip.file(), off, _zip.entry(e).compSize,
                           _zip.entry(e).method, sinkToString, nullptr);
    return (got < 0) ? String("") : g_text;
  }

  static String   g_text;
  static uint32_t g_textLimit;
  static bool sinkToString(const uint8_t *d, size_t n, void *) {
    for (size_t i = 0; i < n && g_text.length() < g_textLimit; i++) g_text += (char)d[i];
    return true;
  }

  // container.xml ukazuje na opf; kdyz chybi, vezmeme prvni .opf v archivu.
  int findOpf() {
    int c = _zip.find("META-INF/container.xml");
    if (c >= 0) {
      String xml = readSmall(c, 4096);
      int fa = xml.indexOf("full-path");
      if (fa >= 0) {
        int q1 = xml.indexOf('"', fa);
        int q2 = (q1 >= 0) ? xml.indexOf('"', q1 + 1) : -1;
        if (q2 > q1) {
          String path = xml.substring(q1 + 1, q2);
          int e = _zip.find(path);
          if (e >= 0) return e;
        }
      }
    }
    return _zip.findEndsWith(".opf");
  }

  // Z opf vytahne nazev, autora a poradi dokumentu (spine -> manifest href).
  int readOpf(int e, const String &dir, String &title, String &author,
              String *spine, int maxSpine) {
    String x = readSmall(e, 32768);
    if (x.length() == 0) { RDR_LOG("epub: opf je prazdny nebo prilis velky"); return 0; }

    title  = tagText(x, "dc:title");
    author = tagText(x, "dc:creator");
    if (title.length() == 0)  title  = tagText(x, "title");
    if (author.length() == 0) author = tagText(x, "creator");

    // manifest: id -> href
    static String ids[RDR_ZIP_MAX_ENTRIES], hrefs[RDR_ZIP_MAX_ENTRIES];
    int nMan = 0;
    int p = 0;
    while (nMan < RDR_ZIP_MAX_ENTRIES) {
      p = x.indexOf("<item ", p);
      if (p < 0) break;
      int end = x.indexOf('>', p);
      if (end < 0) break;
      String item = x.substring(p, end);
      String id   = attr(item, "id");
      String href = attr(item, "href");
      if (id.length() && href.length()) { ids[nMan] = id; hrefs[nMan] = href; nMan++; }
      p = end + 1;
    }

    // spine: poradi idref
    int n = 0;
    p = x.indexOf("<spine");
    if (p < 0) return 0;
    while (n < maxSpine) {
      p = x.indexOf("<itemref", p);
      if (p < 0) break;
      int end = x.indexOf('>', p);
      if (end < 0) break;
      String ref = attr(x.substring(p, end), "idref");
      for (int k = 0; k < nMan; k++) {
        if (ids[k] == ref) {
          String h = hrefs[k];
          // jen textove dokumenty
          if (h.endsWith(".xhtml") || h.endsWith(".html") || h.endsWith(".htm"))
            spine[n++] = dir + urlDecode(h);
          break;
        }
      }
      p = end + 1;
    }
    return n;
  }

  static String tagText(const String &x, const char *tag) {
    String open = String("<") + tag;
    int a = x.indexOf(open);
    if (a < 0) return "";
    a = x.indexOf('>', a);
    if (a < 0) return "";
    int b = x.indexOf("</", a);
    if (b < 0) return "";
    String t = x.substring(a + 1, b);
    t.trim();
    return t;
  }

  static String attr(const String &tag, const char *name) {
    String key = String(name) + "=";
    int a = tag.indexOf(key);
    if (a < 0) return "";
    a += key.length();
    if (a >= (int)tag.length()) return "";
    char q = tag[a];
    if (q != '"' && q != '\'') return "";
    int b = tag.indexOf(q, a + 1);
    if (b < 0) return "";
    return tag.substring(a + 1, b);
  }

  // Jmena v opf mohou byt procentne kodovana (mezery jako %20).
  static String urlDecode(const String &s) {
    String o;
    for (int i = 0; i < (int)s.length(); i++) {
      if (s[i] == '%' && i + 2 < (int)s.length()) {
        char h[3] = { s[i+1], s[i+2], 0 };
        o += (char)strtol(h, NULL, 16);
        i += 2;
      } else o += s[i];
    }
    return o;
  }
};

RdrHtmlText *RdrEpub::g_sinkHtml = nullptr;
String       RdrEpub::g_text;
uint32_t     RdrEpub::g_textLimit = 0;

#endif  // RDR_HAVE_MINIZ
