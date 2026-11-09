// ============================================================================
//  portal.h  -  Rezim nahravani knih pres WiFi
// ----------------------------------------------------------------------------
//  Nahrazuje puvodne planovany USB Mass Storage. Deska NEMA nativni USB
//  (USB-C vede do prevodniku USB-UART), takze se jako flashdisk hlasit neumi.
//
//  SPUSTENI: podrzet PUSH pri startu po dobu RDR_PORTAL_HOLD_MS (5 s).
//
//  DVA REZIMY:
//   STA  - jsou-li ulozene udaje domaci WiFi, deska se k ni pripoji.
//          Na displeji: jmeno site, IP adresa, pripadne mDNS jmeno.
//   AP   - jinak (nebo kdyz se pripojeni nepodari) vytvori vlastni hotspot.
//          Na displeji: SSID, heslo, IP adresa.
//
//  OSETRENE CHYBOVE STAVY:
//   - slaba baterie      -> rezim se vubec nespusti (WiFi ma spicky >300 mA)
//   - chybi SD karta     -> rezim se nespusti, neni kam ukladat
//   - domaci WiFi nejde  -> automaticky se spusti vlastni hotspot
//   - vypadek WiFi behem prace -> po RDR_STA_MAX_RETRIES prechod na hotspot
//   - plna karta         -> nahravani odmitnuto s hlaskou, ne useknuty soubor
//   - pretrzeny prenos   -> nedopsany soubor se smaze
//
//  AUTOMATICKE VYPNUTI po RDR_PORTAL_TIMEOUT_MS necinnosti. Casovac se
//  resetuje pri KAZDEM HTTP pozadavku VCETNE kazdeho kousku nahravaneho
//  souboru, takze dlouhy prenos rezim neukonci.
//
//  SDILENA SPI: ePaper a SD sdili sbernici. Behem zapisu na kartu se NESMI
//  prekreslovat displej - hlida to priznak g_uploading.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SD.h>
#include "esp_system.h"   // esp_random()
#include "config.h"
#include "battery.h"
#include "library.h"
#include "settings.h"
#include "portal_page.h"
#include "zip.h"
#include "log.h"

enum RdrPortalMode : uint8_t {
  RDR_PORTAL_OFF = 0, RDR_PORTAL_AP, RDR_PORTAL_STA, RDR_PORTAL_ERROR
};

struct RdrPortalState {
  RdrPortalMode mode = RDR_PORTAL_OFF;
  String ssid, pass, ip, host, error;
  bool   fellBack = false;    // domaci WiFi selhala -> jedeme na hotspotu
};

typedef void (*RdrPortalDrawFn)(const RdrPortalState &st);

// --- vnitrni stav ----------------------------------------------------------
static WebServer       g_http(80);
static DNSServer       g_dns;
static RdrPortalState  g_ps;
static RdrPortalDrawFn g_draw = nullptr;
static RdrLibrary     *g_lib  = nullptr;
static RdrSettings    *g_set  = nullptr;

static bool     g_running   = false;
static bool     g_uploading = false;
static bool     g_finish    = false;
static uint32_t g_lastAct   = 0;
static File     g_upFile;
static String   g_upName;
static bool     g_upFail    = false;
static uint32_t g_upExpect  = 0;   // velikost slibena prohlizecem
static String   g_upErr;
static uint8_t  g_staFails  = 0;
static uint32_t g_lastCheck = 0;
static bool     g_libDirty  = true;   // knihovnu je treba znovu nacist

// Knihovna se prochazi jen kdyz se opravdu neco zmenilo. Scan otevira
// kazdou knihu kvuli nazvu, takze delat ho pri kazdem dotazu stranky
// (kazdych 5 s) by kartu zbytecne zatezovalo.
static void rdrEnsureLib() {
  if (!g_libDirty) return;
  g_lib->scan();
  g_libDirty = false;
}

#define RDR_NVS_WIFI   "rdrwifi"

static inline void touchAct() { g_lastAct = millis(); }

// ---------------------------------------------------------------------------
//  Sanitizace jmena souboru
//  FAT32 nema rad diakritiku, mezery a specialni znaky, a firmware ocekava
//  priponu .book. Diakritiku prevadime na zakladni pismena, aby zustala
//  citelnost (napr. "Rytíř Pájka" -> "Rytir-Pajka.book").
// ---------------------------------------------------------------------------
static String rdrDeaccent(const String &in) {
  static const char *RDR_ACCENTED[] = {
    "á","č","ď","é","ě","í","ň","ó","ř","š","ť","ú","ů","ý","ž",
    "Á","Č","Ď","É","Ě","Í","Ň","Ó","Ř","Š","Ť","Ú","Ů","Ý","Ž" };
  static const char *RDR_PLAIN[] = {
    "a","c","d","e","e","i","n","o","r","s","t","u","u","y","z",
    "A","C","D","E","E","I","N","O","R","S","T","U","U","Y","Z" };
  String s = in;
  for (int i = 0; i < 30; i++) s.replace(RDR_ACCENTED[i], RDR_PLAIN[i]);
  return s;
}

// Vraci priponu v malych pismenech (".book", ".epub"), jinak prazdny retezec.
static String rdrExtOf(const String &name) {
  int d = name.lastIndexOf('.');
  if (d < 0) return "";
  String e = name.substring(d);
  e.toLowerCase();
  return e;
}

static String rdrSafeName(const String &raw) {
  String base = raw;
  int p = base.lastIndexOf('/'); if (p >= 0) base = base.substring(p + 1);
  p = base.lastIndexOf('\\');    if (p >= 0) base = base.substring(p + 1);

  // Priponu si zapamatujeme a na konci vratime zpet.
  String ext = rdrExtOf(base);
  if (ext != ".book" && ext != ".epub") ext = ".book";
  int d = base.lastIndexOf('.');
  if (d > 0) base = base.substring(0, d);

  base = rdrDeaccent(base);

  String out;
  for (size_t i = 0; i < base.length() && out.length() < RDR_NAME_MAX; i++) {
    char c = base[i];
    bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_';
    if (alnum) { out += c; continue; }
    // Mezery, tecky, carky i spojovniky se sloucí do JEDNOHO spojovniku.
    // Bez toho by z "Autor - Nazev" vzniklo "Autor--Nazev".
    if (c == ' ' || c == '.' || c == '-' || c == ',') {
      if (out.length() && out[out.length() - 1] != '-') out += '-';
    }
    // vse ostatni (zavorky, uvozovky, ...) zahodime
  }
  while (out.length() && out[out.length() - 1] == '-') out.remove(out.length() - 1);
  if (out.length() == 0) out = "kniha";
  return out + ext;
}

static uint32_t rdrFreeKB() {
  uint64_t t = SD.totalBytes(), u = SD.usedBytes();
  return (uint32_t)(((t > u) ? (t - u) : 0) / 1024ULL);
}

static void rdrRedraw() {
  if (g_uploading) return;          // sdilena SPI - displej ted ne!
  if (g_draw) g_draw(g_ps);
}

// ---------------------------------------------------------------------------
//  HTTP handlery
// ---------------------------------------------------------------------------
static void hRoot() {
  touchAct();
  g_http.send_P(200, "text/html; charset=utf-8", RDR_PORTAL_HTML);
}

static String jsonEsc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n' || c == '\r') o += ' ';
    else o += c;
  }
  return o;
}

static void hStatus() {
  touchAct();
  rdrEnsureLib();

  String j = "{\"mode\":\"";
  j += (g_ps.mode == RDR_PORTAL_STA) ? "Připojeno k WiFi" : "Vlastní hotspot";
  j += "\",\"ip\":\"" + g_ps.ip + "\",\"host\":\"" + g_ps.host + "\"";
  j += ",\"batt\":"  + String(batteryPercent(batteryVoltage()));
  j += ",\"total\":" + String((uint32_t)(SD.totalBytes() / 1024ULL));
  j += ",\"free\":"  + String(rdrFreeKB());
  j += ",\"font\":"  + String(g_set->fontSize);
  j += ",\"sleep\":" + String(g_set->sleepMin);

  uint32_t el = millis() - g_lastAct, left = 0;
  if (el < RDR_PORTAL_TIMEOUT_MS) left = (RDR_PORTAL_TIMEOUT_MS - el) / 1000;
  j += ",\"idle\":" + String(left);

  String saved;
  Preferences pr;
  if (pr.begin(RDR_NVS_WIFI, true)) { saved = pr.getString("ssid", ""); pr.end(); }
  j += ",\"ssid\":\"" + jsonEsc(saved) + "\"";

  j += ",\"books\":[";
  for (int i = 0; i < g_lib->count(); i++) {
    RdrBookEntry &b = g_lib->at(i);
    if (i) j += ",";
    j += "{\"file\":\"" + jsonEsc(b.file) + "\",\"title\":\"" + jsonEsc(b.title) + "\"";
    j += ",\"size\":" + String(b.size / 1024);
    j += ",\"pct\":"  + String(b.percent());
    j += ",\"done\":" + String(b.done ? "true" : "false");
    j += ",\"conv\":" + String(b.needsConv ? "true" : "false") + "}";
  }
  j += "]}";
  g_http.send(200, "application/json; charset=utf-8", j);
}

static void hUploadData() {
  HTTPUpload &up = g_http.upload();
  touchAct();                         // KAZDY kousek odsune timeout

  if (up.status == UPLOAD_FILE_START) {
    g_upFail = false; g_upErr = ""; g_uploading = true;
    g_upName = rdrSafeName(up.filename);

    // Ocekavanou velikost posila prohlizec SAMOSTATNYM pozadavkem
    // /api/expect tesne pred nahranim (viz g_upExpect).
    //
    // Proc ne jinak: parametr v URL muze WebServer pri multipart nahravani
    // prepsat obsahem formulare, a collectHeaders() se v Arduino core 3.x
    // zmenilo na variadickou sablonu, takze hlavicka k nam nedorazila
    // (v logu se objevilo "ocekavano 0").
    if (!SD.exists(RDR_BOOKS_DIR)) SD.mkdir(RDR_BOOKS_DIR);

    if (rdrFreeKB() < RDR_MIN_FREE_KB) {
      g_upFail = true;
      g_upErr = "Na kartě není dost místa. Smažte nějakou knihu.";
      return;
    }
    String path = String(RDR_BOOKS_DIR) + "/" + g_upName;
    if (SD.exists(path)) SD.remove(path);
    RDR_LOG("portal: nahravam '%s' -> %s", up.filename.c_str(), g_upName.c_str());
    g_upFile = SD.open(path, FILE_WRITE);
    if (!g_upFile) { g_upFail = true; g_upErr = "Soubor na kartě nelze vytvořit."; }
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_upFail || !g_upFile) return;
    if (g_upFile.write(up.buf, up.currentSize) != up.currentSize) {
      g_upFail = true;
      g_upErr  = "Zápis na kartu selhal (plná karta nebo chyba karty).";
      RDR_LOG("portal: ZAPIS NA KARTU SELHAL u '%s'", g_upName.c_str());
      g_upFile.close();
    }
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (g_upFile) g_upFile.close();
    g_uploading = false;
    g_libDirty  = true;

    uint32_t got = (uint32_t)up.totalSize;
    RDR_LOG("portal: nahrano %lu B (ocekavano %lu)",
            (unsigned long)got, (unsigned long)g_upExpect);

    // Nezavisla kontrola: u EPUBu overime, ze soubor konci platnym
    // zaznamem ZIP archivu. Odhali useknuty prenos i tehdy, kdyz
    // prohlizec zadnou velikost neposlal.
    if (!g_upFail && rdrExtOf(g_upName) == ".epub") {
      // Na haldu, ne na zasobnik - RdrZip drzi pole 64 polozek (~2 kB).
      RdrZip *probe = new RdrZip();
      String path = String(RDR_BOOKS_DIR) + "/" + g_upName;
      if (probe && !probe->open(path)) {
        probe->close();
        SD.remove(path);
        g_upFail = true;
        g_upErr  = "Soubor není platný EPUB (přenos byl nejspíš neúplný). "
                   "Byl odstraněn, zkuste to znovu.";
        RDR_LOG("portal: soubor neni platny ZIP, smazan");
      } else if (probe) {
        RDR_LOG("portal: EPUB overen, %d polozek", probe->count());
        probe->close();
      }
      delete probe;
    }

    if (!g_upFail && g_upExpect && got != g_upExpect) {
      // Neuplny prenos: soubor smazat, at na karte nezustane zmetek.
      SD.remove(String(RDR_BOOKS_DIR) + "/" + g_upName);
      g_upFail = true;
      g_upErr  = "Přenos byl neúplný (" + String(got) + " z " +
                 String(g_upExpect) + " B). Soubor byl odstraněn, zkuste to znovu.";
      RDR_LOG("portal: NEUPLNY PRENOS, soubor smazan");
    }
    // Nove nahrany EPUB: kdyz uz na karte lezi .book ze stejnojmenneho
    // drivejsiho EPUBu, smazat ho. Jinak by se novy EPUB v seznamu skryl
    // (viz hideConvertedEpubs) a ctecka by dal ukazovala starou verzi.
    if (!g_upFail && rdrExtOf(g_upName) == ".epub") {
      String stale = String(RDR_BOOKS_DIR) + "/" +
                     g_upName.substring(0, g_upName.length() - 5) + ".book";
      if (SD.exists(stale)) {
        SD.remove(stale);
        SD.remove(stale + RDR_POS_SUFFIX);
        RDR_LOG("portal: stary prevod '%s' smazan, EPUB se prevede znovu", stale.c_str());
      }
    }
    g_upExpect = 0;   // plati vzdy jen pro nejblizsi nahravani
  }
  else if (up.status == UPLOAD_FILE_ABORTED) {
    // Pretrzeny prenos: nedopsany soubor smazeme, at se necte zmetek.
    if (g_upFile) g_upFile.close();
    SD.remove(String(RDR_BOOKS_DIR) + "/" + g_upName);
    g_upFail = true; g_upErr = "Přenos se přerušil, soubor byl odstraněn.";
    RDR_LOG("portal: PRENOS PRERUSEN u '%s', soubor smazan", g_upName.c_str());
    g_uploading = false;
  }
}

static void hUploadDone() {
  touchAct(); g_uploading = false;
  if (g_upFail) g_http.send(507, "text/plain; charset=utf-8", g_upErr);
  else          g_http.send(200, "text/plain; charset=utf-8", "OK");
}

// Bezpecnostni kontrola jmena z formulare
static bool validFile(const String &n) {
  if (n.length() <= 5) return false;
  if (n.indexOf('/') >= 0 || n.indexOf('\\') >= 0 || n.indexOf("..") >= 0) return false;
  String e = rdrExtOf(n);
  return e == ".book" || e == ".epub";
}

static void hDelete() {
  touchAct();
  String f = g_http.arg("file");
  if (!validFile(f)) { g_http.send(400, "text/plain; charset=utf-8", "Neplatný název."); return; }
  String path = String(RDR_BOOKS_DIR) + "/" + f;
  SD.remove(path + RDR_POS_SUFFIX);      // smazat i ulozenou pozici

  // Kdyz kniha vznikla konverzi z EPUBu, lezi na karte oba soubory.
  // Smazani jednoho ma odstranit i ten druhy, aby nezustal osirely.
  String stem = f.substring(0, f.length() - 5);
  String twin = String(RDR_BOOKS_DIR) + "/" + stem +
                (rdrExtOf(f) == ".book" ? ".epub" : ".book");
  if (SD.exists(twin)) { SD.remove(twin); SD.remove(twin + RDR_POS_SUFFIX); }

  g_libDirty = true;
  if (SD.remove(path)) g_http.send(200, "text/plain; charset=utf-8", "Smazáno: " + f);
  else g_http.send(500, "text/plain; charset=utf-8", "Soubor se nepodařilo smazat.");
}

static void hMarkRead() {
  touchAct();
  String f = g_http.arg("file");
  bool done = g_http.arg("done") == "1";
  if (!validFile(f)) { g_http.send(400, "text/plain; charset=utf-8", "Neplatný název."); return; }

  rdrEnsureLib();
  for (int i = 0; i < g_lib->count(); i++) {
    RdrBookEntry &b = g_lib->at(i);
    if (b.file != f) continue;
    b.done = done;
    if (!done && b.offset == 0) { /* zustane neprectena */ }
    g_lib->savePos(b);
    g_libDirty = true;
    g_http.send(200, "text/plain; charset=utf-8",
                done ? "Označeno jako přečtené." : "Označeno jako nepřečtené.");
    return;
  }
  g_http.send(404, "text/plain; charset=utf-8", "Kniha nenalezena.");
}

static void hSettings() {
  touchAct();
  if (g_http.hasArg("font"))  g_set->fontSize = (uint8_t)g_http.arg("font").toInt();
  if (g_http.hasArg("sleep")) g_set->sleepMin = (uint16_t)g_http.arg("sleep").toInt();
  g_set->save();
  g_http.send(200, "text/plain; charset=utf-8",
              String("Uloženo. Písmo: ") + g_set->fontName() +
              ", uspání po " + String(g_set->sleepMin) + " min.");
}

static void hWifi() {
  touchAct();
  String s = g_http.arg("ssid"); s.trim();
  String p = g_http.arg("pass");
  if (s.length() > 32) { g_http.send(400, "text/plain; charset=utf-8", "Název sítě je příliš dlouhý."); return; }
  if (s.length() && p.length() && p.length() < 8) {
    g_http.send(400, "text/plain; charset=utf-8", "Heslo WiFi musí mít alespoň 8 znaků."); return;
  }
  Preferences pr;
  if (!pr.begin(RDR_NVS_WIFI, false)) {
    g_http.send(500, "text/plain; charset=utf-8", "Nastavení se nepodařilo uložit."); return;
  }
  if (s.length() == 0) {
    pr.remove("ssid"); pr.remove("pass"); pr.end();
    RDR_LOG("portal: ulozena WiFi smazana");
    g_http.send(200, "text/plain; charset=utf-8",
                "Uložená WiFi smazána. Příště se spustí vlastní hotspot.");
    return;
  }
  pr.putString("ssid", s); pr.putString("pass", p); pr.end();
  RDR_LOG("portal: ulozena domaci WiFi '%s' (heslo %d znaku)", s.c_str(), (int)p.length());
  g_http.send(200, "text/plain; charset=utf-8",
              "Uloženo. Příště se čtečka připojí k síti " + s + ".");
}

// Vyhleda dostupne WiFi site, aby uzivatel nemusel SSID psat rucne.
// Skenovani v rezimu AP kratce prerusi vysilani hotspotu, proto se
// docasne prepneme do AP_STA a po skenu vratime zpet.
static void hScan() {
  touchAct();
  RDR_LOG("portal: skenuji WiFi site");

  bool wasAP = (g_ps.mode == RDR_PORTAL_AP);
  if (wasAP) WiFi.mode(WIFI_AP_STA);

  int n = WiFi.scanNetworks(false, false);
  String j = "{\"nets\":[";
  int shown = 0;
  for (int i = 0; i < n && shown < 20; i++) {
    String ss = WiFi.SSID(i);
    if (ss.length() == 0) continue;            // skryte site preskocime
    if (shown) j += ",";
    // RSSI -100..-50 dBm prevedeme na priblizna procenta kvality
    int q = constrain(2 * (WiFi.RSSI(i) + 100), 0, 100);
    j += "{\"s\":\"" + jsonEsc(ss) + "\",\"q\":" + String(q) +
         ",\"e\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1) + "}";
    shown++;
  }
  j += "]}";
  WiFi.scanDelete();
  if (wasAP) WiFi.mode(WIFI_AP);

  RDR_LOG("portal: nalezeno %d siti", shown);
  g_http.send(200, "application/json; charset=utf-8", j);
}

// Prohlizec sem posle velikost souboru tesne pred jeho nahranim.
static void hExpect() {
  touchAct();
  g_upExpect = (uint32_t)strtoul(g_http.arg("size").c_str(), NULL, 10);
  RDR_LOG("portal: ocekavam soubor %lu B", (unsigned long)g_upExpect);
  g_http.send(200, "text/plain", "OK");
}

static void hFinish() {
  touchAct(); g_finish = true;
  RDR_LOG("portal: uzivatel ukoncil rezim nahravani");
  g_http.send(200, "text/plain", "OK");
}

// Captive portal: cokoli nezname presmerujeme na uvodni stranku
static void hNotFound() {
  touchAct();
  if (g_ps.mode == RDR_PORTAL_AP) {
    g_http.sendHeader("Location", "http://" + g_ps.ip + "/", true);
    g_http.send(302, "text/plain", "");
  } else g_http.send(404, "text/plain; charset=utf-8", "Nenalezeno");
}

// ---------------------------------------------------------------------------
//  Spusteni site
// ---------------------------------------------------------------------------
static String rdrApPassword() {
  Preferences pr; String p;
  if (pr.begin(RDR_NVS_WIFI, true)) { p = pr.getString("appass", ""); pr.end(); }
  if (p.length() >= 8) return p;
  // Bez matoucich znaku (0/O, 1/l/I) - heslo se opisuje z displeje.
  static const char *RDR_PWCHARS = "23456789abcdefghjkmnpqrstuvwxyz";
  int n = strlen(RDR_PWCHARS);
  p = "";
  for (int i = 0; i < 10; i++) p += RDR_PWCHARS[esp_random() % n];
  if (pr.begin(RDR_NVS_WIFI, false)) { pr.putString("appass", p); pr.end(); }
  return p;
}

static String rdrApSsid() {
  uint8_t m[6]; WiFi.macAddress(m);
  char b[24]; snprintf(b, sizeof(b), "Ctecka-%02X%02X", m[4], m[5]);
  return String(b);
}

static bool rdrStartAP() {
  String ssid = rdrApSsid(), pass = rdrApPassword();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid.c_str(), pass.c_str())) {
    g_ps.mode = RDR_PORTAL_ERROR;
    g_ps.error = "Hotspot se nepodařilo spustit.";
    return false;
  }
  WiFi.setTxPower(WIFI_POWER_11dBm);      // nizsi vykon = mensi spicky odberu
  IPAddress ip = WiFi.softAPIP();
  RDR_LOG("portal: hotspot '%s', heslo '%s', IP %s",
          ssid.c_str(), pass.c_str(), ip.toString().c_str());
  g_ps.mode = RDR_PORTAL_AP;
  g_ps.ssid = ssid; g_ps.pass = pass;
  g_ps.ip   = ip.toString(); g_ps.host = "";
  g_dns.start(53, "*", ip);               // captive portal
  return true;
}

static bool rdrStartSTA() {
  Preferences pr; String s, p;
  if (pr.begin(RDR_NVS_WIFI, true)) {
    s = pr.getString("ssid", ""); p = pr.getString("pass", ""); pr.end();
  }
  if (s.length() == 0) return false;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(s.c_str(), p.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < RDR_STA_CONNECT_MS) delay(200);
  if (WiFi.status() != WL_CONNECTED) {
    RDR_LOG("portal: pripojeni k '%s' se nezdarilo, spoustim hotspot", s.c_str());
    WiFi.disconnect(true); return false;
  }

  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.setAutoReconnect(true);
  g_ps.mode = RDR_PORTAL_STA;
  g_ps.ssid = s; g_ps.pass = "";
  g_ps.ip   = WiFi.localIP().toString();
  RDR_LOG("portal: pripojeno k '%s', IP %s", s.c_str(), g_ps.ip.c_str());
  if (MDNS.begin(RDR_MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    g_ps.host = String(RDR_MDNS_NAME) + ".local";
  }
  return true;
}

// ---------------------------------------------------------------------------
//  Verejne API
// ---------------------------------------------------------------------------

// Drzi uzivatel PUSH uz pri startu? Voláme na ZACATKU setup(), jeste pred
// inicializaci displeje a karty.
inline bool rdrPushAtBoot() {
  pinMode(RDR_BTN_PUSH, INPUT_PULLUP);
  delay(RDR_BTN_DEBOUNCE_MS);
  return digitalRead(RDR_BTN_PUSH) == LOW;
}

// Podrzel uzivatel PUSH dost dlouho na spusteni portalu?
//
// DULEZITE: doba se meri OD STARTU DESKY (bootMs), ne od zavolani teto
// funkce. Inicializace displeje a karty trva pres dve sekundy, takze kdyby
// se merilo az tady, musel by uzivatel drzet tlacitko skoro osm sekund.
inline bool rdrPortalRequested(bool pushAtBoot, uint32_t bootMs) {
  if (!pushAtBoot) return false;
  while (digitalRead(RDR_BTN_PUSH) == LOW) {
    if (millis() - bootMs >= RDR_PORTAL_HOLD_MS) return true;
    delay(20);
  }
  return false;   // pustil driv
}

inline bool rdrPortalBegin(RdrLibrary &lib, RdrSettings &set, RdrPortalDrawFn drawFn) {
  g_lib = &lib; g_set = &set; g_draw = drawFn;
  g_ps = RdrPortalState();

  // 1) baterie - WiFi ma spicky pres 300 mA
  if (batteryVoltage() < RDR_WIFI_VBAT_MIN) {
    g_ps.mode = RDR_PORTAL_ERROR;
    g_ps.error = "Slabá baterie pro WiFi. Nabijte čtečku.";
    rdrRedraw(); return false;
  }
  // 2) karta
  if (!SD.exists("/")) {
    g_ps.mode = RDR_PORTAL_ERROR;
    g_ps.error = "Není vložena SD karta.";
    rdrRedraw(); return false;
  }
  if (!SD.exists(RDR_BOOKS_DIR)) SD.mkdir(RDR_BOOKS_DIR);

  // 3) nejdriv domaci WiFi, jinak vlastni hotspot
  if (!rdrStartSTA()) {
    g_ps.fellBack = true;
    if (!rdrStartAP()) { rdrRedraw(); return false; }
  }

  g_http.on("/",             HTTP_GET,  hRoot);
  g_http.on("/api/status",   HTTP_GET,  hStatus);
  g_http.on("/api/expect",   HTTP_POST, hExpect);
  g_http.on("/api/upload",   HTTP_POST, hUploadDone, hUploadData);
  g_http.on("/api/delete",   HTTP_POST, hDelete);
  g_http.on("/api/read",     HTTP_POST, hMarkRead);
  g_http.on("/api/settings", HTTP_POST, hSettings);
  g_http.on("/api/wifi",     HTTP_POST, hWifi);
  g_http.on("/api/scan",     HTTP_GET,  hScan);
  g_http.on("/api/finish",   HTTP_POST, hFinish);
  g_http.onNotFound(hNotFound);

  g_http.begin();

  g_running = true; g_finish = false; g_libDirty = true;
  touchAct(); rdrRedraw();
  return true;
}

// Obsluha ve smycce. false = rezim ma skoncit.
inline bool rdrPortalLoop() {
  if (!g_running) return false;
  if (g_ps.mode == RDR_PORTAL_AP) g_dns.processNextRequest();
  g_http.handleClient();
  if (g_finish) return false;

  // Vypadek domaci WiFi: par pokusu, pak prechod na vlastni hotspot,
  // aby uzivatel mohl praci dokoncit.
  if (g_ps.mode == RDR_PORTAL_STA && !g_uploading && millis() - g_lastCheck > 3000) {
    g_lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (++g_staFails >= RDR_STA_MAX_RETRIES) {
        WiFi.disconnect(true); MDNS.end();
        if (!rdrStartAP()) return false;
        g_ps.fellBack = true;
        touchAct();            // po prepnuti dame plny cas znovu
        rdrRedraw();
        g_staFails = 0;
      }
    } else {
      g_staFails = 0;
      String now = WiFi.localIP().toString();
      if (now != g_ps.ip) { g_ps.ip = now; rdrRedraw(); }
    }
  }

  // Automaticke vypnuti. Behem nahravani se neuplatni.
  if (!g_uploading && millis() - g_lastAct > RDR_PORTAL_TIMEOUT_MS) {
    RDR_LOG("portal: vyprsela necinnost, koncim");
    return false;
  }
  return true;
}

inline void rdrPortalEnd() {
  if (g_upFile) g_upFile.close();
  g_http.stop();
  if (g_ps.mode == RDR_PORTAL_AP) g_dns.stop();
  MDNS.end();
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_running = false;
  g_ps.mode = RDR_PORTAL_OFF;
}

inline const RdrPortalState& rdrPortalStatus() { return g_ps; }
