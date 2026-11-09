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
#include <vector>
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
    // Lokalni vektor misto statickeho pole: po konverzi se pamet uvolni
    // a pocet dokumentu neni omezeny pevnym limitem.
    std::vector<String> spine;
    int nSpine = readOpf(opf, opfDir, title, author, spine);
    if (nSpine <= 0) { _zip.close(); return fail("Nepodařilo se přečíst obsah knihy."); }
    RDR_LOG("epub: '%s' / '%s', %d dokumentu", title.c_str(), author.c_str(), nSpine);

    // --- zapis do docasneho souboru ---------------------------------------
    String tmp = bookPath + ".tmp";
    SD.remove(tmp);
    File out = SD.open(tmp.c_str(), FILE_WRITE);
    if (!out) { _zip.close(); return fail("Na kartu nelze zapisovat."); }

    // Hlavicka je po radcich - zalomeni uvnitr nazvu by rozbilo format.
    title.replace('\n', ' ');  title.replace('\r', ' ');
    author.replace('\n', ' '); author.replace('\r', ' ');
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
    // Predem alokovat celou delku. Pri postupnem pridavani po znacich se
    // retezec opakovane realokuje a u desitek kB fragmentuje haldu.
    g_text.reserve(_zip.entry(e).uncompSize + 1);
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
              std::vector<String> &spine) {
    // Limit drive 32 kB. OPF ma kolem 150 B na polozku, takze kniha
    // s vice nez ~200 soubory se vubec neprevedla.
    String x = readSmall(e, 98304);
    if (x.length() == 0) { RDR_LOG("epub: opf je prazdny nebo prilis velky"); return 0; }

    title  = decodeEntities(tagText(x, "dc:title"));
    author = decodeEntities(tagText(x, "dc:creator"));
    if (title.length() == 0)  title  = decodeEntities(tagText(x, "title"));
    if (author.length() == 0) author = decodeEntities(tagText(x, "creator"));

    // manifest: id -> href (jen textove dokumenty)
    std::vector<String> ids, hrefs;
    int p = 0;
    for (;;) {
      p = x.indexOf("<item ", p);
      if (p < 0) break;
      int end = x.indexOf('>', p);
      if (end < 0) break;
      String item = x.substring(p, end);
      String id   = attr(item, "id");
      String href = attr(item, "href");
      String mt   = attr(item, "media-type");
      // Textovy dokument pozname podle media-type; pripona je jen zaloha.
      // Nektere EPUBy pouzivaji pro kapitoly priponu .xml nebo zadnou.
      String hl = href; hl.toLowerCase();
      bool isText = (mt.indexOf("html") >= 0) ||
                    hl.endsWith(".xhtml") || hl.endsWith(".html") || hl.endsWith(".htm");
      if (id.length() && href.length() && isText) { ids.push_back(id); hrefs.push_back(href); }
      p = end + 1;
    }

    // spine: poradi idref
    p = x.indexOf("<spine");
    if (p < 0) return 0;
    for (;;) {
      p = x.indexOf("<itemref", p);
      if (p < 0) break;
      int end = x.indexOf('>', p);
      if (end < 0) break;
      String ref = attr(x.substring(p, end), "idref");
      for (size_t k = 0; k < ids.size(); k++) {
        if (ids[k] == ref) { spine.push_back(normPath(dir + urlDecode(hrefs[k]))); break; }
      }
      p = end + 1;
    }
    return (int)spine.size();
  }

  // Slozi cestu "OEBPS/Text/../Text/kap1.xhtml" na "OEBPS/Text/kap1.xhtml".
  // V archivu jsou jmena vzdy bez "." a "..", takze bez normalizace by se
  // takovy dokument nenasel.
  static String normPath(const String &in) {
    std::vector<String> parts;
    int a = 0;
    while (a <= (int)in.length()) {
      int b = in.indexOf('/', a);
      if (b < 0) b = in.length();
      String seg = in.substring(a, b);
      if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
      else if (seg.length() && seg != ".") parts.push_back(seg);
      a = b + 1;
    }
    String out;
    for (size_t i = 0; i < parts.size(); i++) { if (i) out += '/'; out += parts[i]; }
    return out;
  }

  // Nazev a autor v OPF mohou obsahovat entity (&amp; apod.)
  static String decodeEntities(String t) {
    t.replace("&amp;", "&"); t.replace("&lt;", "<"); t.replace("&gt;", ">");
    t.replace("&quot;", "\""); t.replace("&apos;", "'"); t.replace("&#39;", "'");
    return t;
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
