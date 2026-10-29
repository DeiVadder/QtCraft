#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Fills a Qt .ts file from a JSON dictionary {"source text": "translation"}; plural messages
(%n) take a list with one text per numerus form.

Usage: scripts/apply-translations.py translations/qtcraft_de.ts translations/de.json
Messages without an entry stay unfinished; the script lists them so nothing is forgotten. A live
message translated only in the .ts file (no entry in the dictionary) is listed as a warning: the
dictionary is the full source of the translations.
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
    only_in_ts = []
    for message in tree.getroot().iter("message"):
        source = message.findtext("source")
        translation = message.find("translation")
        if source in dictionary:
            value = dictionary[source]
            if isinstance(value, list):
                # Plural message: one entry per numerus form of the target language.
                for form, text in zip(translation.findall("numerusform"), value):
                    form.text = text
            else:
                translation.text = value
            if translation.get("type") not in ("vanished", "obsolete"):
                translation.attrib.pop("type", None)
        elif translation.get("type") in ("vanished", "obsolete"):
            continue
        elif translation.text or any(form.text for form in translation.findall("numerusform")):
            only_in_ts.append(source)
        else:
            missing.append(source)
    # Match lupdate's own formatting so later lupdate runs do not produce churn.
    body = ET.tostring(tree.getroot(), encoding="unicode")
    with open(ts_path, "w", encoding="utf-8") as handle:
        handle.write('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n')
        handle.write(body.replace(" />", "/>").replace("'", "&apos;"))
        handle.write("\n")
    for source in only_in_ts:
        print(f"warning: translated only in {ts_path}, not in {dictionary_path}: {source!r}")
    for source in missing:
        print(f"untranslated: {source!r}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
