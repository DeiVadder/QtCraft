#!/usr/bin/env python3
"""Fills a Qt .ts file from a JSON dictionary {"source text": "translation"}.

Usage: scripts/apply-translations.py translations/qtcraft_de.ts translations/de.json
Messages without an entry stay unfinished; the script lists them so nothing is forgotten.
"""
import json
import sys
import xml.etree.ElementTree as ET


def main():
    ts_path, dictionary_path = sys.argv[1], sys.argv[2]
    with open(dictionary_path, encoding="utf-8") as handle:
        dictionary = json.load(handle)
    tree = ET.parse(ts_path)
    missing = []
    for message in tree.getroot().iter("message"):
        source = message.findtext("source")
        translation = message.find("translation")
        if source in dictionary:
            translation.text = dictionary[source]
            translation.attrib.pop("type", None)
        elif translation.get("type") != "vanished" and not translation.text:
            missing.append(source)
    # Match lupdate's own formatting so later lupdate runs do not produce churn.
    body = ET.tostring(tree.getroot(), encoding="unicode")
    with open(ts_path, "w", encoding="utf-8") as handle:
        handle.write('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n')
        handle.write(body.replace(" />", "/>"))
        handle.write("\n")
    for source in missing:
        print(f"untranslated: {source!r}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
