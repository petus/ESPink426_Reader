#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
epub2book.py

Prevede EPUB do jednoducheho radkove orientovaneho formatu (.book),
ktery se snadno cte na ESP32/ESP32-S3 cteckou eknih.

Vystupni format (.book, cely soubor je UTF-8):
    T:Nazev knihy
    A:Autor
    C:Nazev kapitoly
    H:Podnadpis v kapitole
    Text odstavce jako jeden souvisly radek bez zalomeni.
    Dalsi odstavec...
    C:Dalsi kapitola
    ...

Prefixy radku:
    T:   titul knihy      (obvykle jednou na zacatku)
    A:   autor            (obvykle jednou na zacatku)
    C:   nazev kapitoly   (zacatek nove kapitoly, firmware muze udelat page break)
    H:   nadpis uvnitr    (h1..h6 uvnitr kapitoly)
    (bez prefixu) = bezny odstavec, jeden odstavec = jeden radek

Firmware si odstavce sam lame na sirku displeje. Diakritika zustava v UTF-8.

Zavislosti:
    pip install ebooklib beautifulsoup4 lxml

Pouziti:
    python epub2book.py kniha.epub
    python epub2book.py kniha.epub -o vystup.book
    python epub2book.py *.epub            # davkove, kazdy epub -> stejnojmenny .book
"""

import argparse
import os
import re
import sys
import unicodedata

try:
    from ebooklib import epub
    import ebooklib
except ImportError:
    sys.exit("Chybi knihovna 'ebooklib'. Nainstaluj: pip install ebooklib beautifulsoup4 lxml")

try:
    from bs4 import BeautifulSoup
except ImportError:
    sys.exit("Chybi knihovna 'beautifulsoup4'. Nainstaluj: pip install beautifulsoup4 lxml")


# ---------------------------------------------------------------------------
#  NAHRAZENI ZNAKU, KTERE FONT VE CTECCE NEOBSAHUJE
#
#  Fonty U8g2 s priponou _te maji glyfy s unicode 32 az 701 (plus par
#  doplnku jako euro). Ceska a anglicka typograficka interpunkce lezi v
#  bloku General Punctuation kolem 8200 az 8230, tedy mimo rozsah:
#      „ U+201E   “ U+201C   ” U+201D   ‘ U+2018   ’ U+2019
#      – U+2013   — U+2014   … U+2026   • U+2022
#  Font pro ne nema glyf a na displeji se nevykresli NIC.
#
#  Nahrazujeme je proto tim, co font zna. Typograficka krasa se ztrati,
#  ale text je citelny. Stejnou tabulku ma i firmware v textmap.h.
# ---------------------------------------------------------------------------
FONT_MAX_CP = 701
FONT_EXTRA = {7838, 8364, 8451, 8457} | set(range(8320, 8330)) | set(range(64256, 64264))

REPLACE = {
    0x2018: "'", 0x2019: "'", 0x201A: "'", 0x201B: "'",
    0x201C: '"', 0x201D: '"', 0x201E: '"', 0x201F: '"',
    0x2032: "'", 0x2033: '"', 0x2039: "<", 0x203A: ">",
    0x2010: "-", 0x2011: "-", 0x2012: "-", 0x2013: "-",
    0x2014: "-", 0x2015: "-", 0x2212: "-",
    0x2026: "...", 0x2022: "*", 0x2023: "*", 0x25CF: "*", 0x25AA: "*",
    0x2044: "/", 0x2190: "<-", 0x2192: "->", 0x2122: "(TM)",
    0x00A0: " ",
    0x200B: "", 0x200C: "", 0x200D: "", 0x2060: "", 0xFEFF: "", 0x00AD: "",
}


def sanitize_for_display(s: str) -> str:
    """Nahradi znaky, ktere font ve ctecce neumi vykreslit."""
    out = []
    for ch in s:
        cp = ord(ch)
        if cp in REPLACE:
            out.append(REPLACE[cp])
        elif cp <= FONT_MAX_CP or cp in FONT_EXTRA:
            out.append(ch)
        else:
            out.append("?")   # neznamy znak, at je videt, ze neco chybi
    return "".join(out)


# Tagy, ktere povazujeme za nadpisy (dostanou prefix H:)
HEADING_TAGS = {"h1", "h2", "h3", "h4", "h5", "h6"}
# Blokove tagy, ktere ukoncuji odstavec
BLOCK_TAGS = {"p", "div", "li", "blockquote", "br"} | HEADING_TAGS


def clean_text(s: str) -> str:
    """Normalizuje bile znaky a sjednoti Unicode do NFC."""
    # NFC slouceni (napr. rozlozena diakritika -> slozena) kvuli fontum na ESP32
    s = unicodedata.normalize("NFC", s)
    # Sjednoceni typografickych mezer a neviditelnych znaku na obycejnou mezeru
    s = s.replace("\u00a0", " ")   # nedelitelna mezera
    s = s.replace("\u200b", "")    # zero-width space
    s = s.replace("\ufeff", "")    # BOM
    # Vice bilych znaku -> jedna mezera, orez okraju
    s = re.sub(r"\s+", " ", s).strip()
    # Znaky, ktere font ve ctecce nezna, nahradit
    s = sanitize_for_display(s)
    return s


def html_to_blocks(html: str):
    """
    Prevede jeden XHTML dokument (kapitolu) na seznam bloku.
    Kazdy blok je dvojice (typ, text), typ = 'H' (nadpis) nebo 'P' (odstavec).
    """
    soup = BeautifulSoup(html, "lxml")

    # Odstranime veci, ktere do textu cist nechceme
    for tag in soup(["script", "style", "head", "title", "svg", "img", "figure"]):
        tag.decompose()

    body = soup.body if soup.body else soup

    blocks = []
    # Projdeme vsechny relevantni blokove elementy v poradi
    for el in body.find_all(["p", "div", "li", "blockquote"] + list(HEADING_TAGS)):
        # Preskocime bloky, ktere obsahuji jen dalsi bloky (aby se text nezdvojil)
        if el.find(["p", "div", "li", "blockquote"] + list(HEADING_TAGS)):
            # Vyjimka: nadpis s vnorenym spanem je OK, ten deti-bloky nema
            if el.name not in HEADING_TAGS:
                continue

        text = clean_text(el.get_text(separator=" "))
        if not text:
            continue

        if el.name in HEADING_TAGS:
            blocks.append(("H", text))
        else:
            blocks.append(("P", text))

    # Fallback: kdyz jsme nic nenasli (neobvykla struktura), vezmeme cely text
    if not blocks:
        text = clean_text(body.get_text(separator=" "))
        if text:
            blocks.append(("P", text))

    return blocks


def get_metadata(book):
    """Vytahne titul a autora z EPUB metadat."""
    title = ""
    author = ""

    dc_title = book.get_metadata("DC", "title")
    if dc_title:
        title = clean_text(dc_title[0][0])

    dc_creator = book.get_metadata("DC", "creator")
    if dc_creator:
        author = clean_text(dc_creator[0][0])

    return title, author


def convert(epub_path: str, out_path: str, verbose: bool = True) -> None:
    book = epub.read_epub(epub_path)
    title, author = get_metadata(book)

    lines = []
    if title:
        lines.append("T:" + title)
    if author:
        lines.append("A:" + author)

    # Projdeme dokumenty ve spravnem cteci poradi (spine)
    chapter_count = 0
    for item_id, _ in book.spine:
        item = book.get_item_with_id(item_id)
        if item is None or item.get_type() != ebooklib.ITEM_DOCUMENT:
            continue

        html = item.get_content().decode("utf-8", errors="replace")
        blocks = html_to_blocks(html)
        if not blocks:
            continue

        # Prvni nadpis v dokumentu bereme jako nazev kapitoly (prefix C:),
        # dalsi nadpisy jako H:. Kdyz zadny nadpis neni, kapitolu nezaklada.
        chapter_started = False
        for btype, text in blocks:
            if btype == "H" and not chapter_started:
                lines.append("C:" + text)
                chapter_started = True
                chapter_count += 1
            elif btype == "H":
                lines.append("H:" + text)
            else:
                lines.append(text)

        # Kdyz dokument nezacal nadpisem, vlozime aspon oddelovac kapitoly
        # jen pokud uz nejaky text existuje (aby prvni nebyl prazdny)
        if not chapter_started and len(lines) > 0:
            # nova sekce bez nazvu - firmware muze udelat page break na prazdne C:
            pass

    # Zapis v UTF-8, LF konce radku (firmware cte po \n)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
        f.write("\n")

    if verbose:
        size_kb = os.path.getsize(out_path) / 1024.0
        print(f"OK: {os.path.basename(epub_path)} -> {os.path.basename(out_path)} "
              f"({chapter_count} kapitol, {len(lines)} radku, {size_kb:.1f} kB)")


def main():
    ap = argparse.ArgumentParser(
        description="Prevede EPUB na jednoduchy .book format pro ESP32 ctecku."
    )
    ap.add_argument("inputs", nargs="+", help="Vstupni .epub soubor(y)")
    ap.add_argument("-o", "--output", help="Vystupni soubor (jen pri jednom vstupu)")
    args = ap.parse_args()

    if args.output and len(args.inputs) > 1:
        sys.exit("Volbu -o lze pouzit jen s jednim vstupnim souborem.")

    for epub_path in args.inputs:
        if not os.path.isfile(epub_path):
            print(f"Preskakuji (neexistuje): {epub_path}", file=sys.stderr)
            continue
        if args.output:
            out_path = args.output
        else:
            base = os.path.splitext(epub_path)[0]
            out_path = base + ".book"
        try:
            convert(epub_path, out_path)
        except Exception as e:
            print(f"CHYBA u {epub_path}: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
