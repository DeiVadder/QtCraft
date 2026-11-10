#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Measures general traits of music pieces and prints them per piece and per music slot
(decision E-1155): length, tempo estimate, key and mode, harmonic rhythm, register, density,
dynamic arc, decay (reverb) and brightness. Used to tune the composer (QcMusicComposer) to the
general character of the tracks in content/qtcraft/music without copying anything of them.

Only general numbers come out; no melody, motif or chord sequence is extracted or written.

Usage:
  scripts/music-traits.py [--tracks DIR] [--pieces DIR] [--per-piece] [--json FILE]
    --tracks DIR   the tracks of a content pack: every <name>.json there with its <name>.ogg;
                   the group is the first slot of the JSON ("biome:" and the namespace dropped)
    --pieces DIR   generated pieces written by `qtcraft-headless --export-sounds <dir>`:
                   music-<slot>-<seed>.wav, grouped by <slot>
    --per-piece    also print one row per piece
    --json FILE    also write all numbers as JSON

Needs ffmpeg (decoding; /opt/homebrew/bin/ffmpeg or on PATH) and numpy.
"""

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("music-traits: numpy is missing (pip3 install numpy)")

RATE = 22050
FFT = 4096
HOP = 1024
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
# Krumhansl–Kessler key profiles (major, minor).
MAJOR_PROFILE = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MINOR_PROFILE = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
SLOT_ORDER = ["menu", "day", "night", "cave", "nether_wastes", "soul_sand_valley", "crimson_forest", "warped_forest",
              "basalt_deltas"]


def ffmpeg_path():
    found = shutil.which("ffmpeg")
    if found:
        return found
    if pathlib.Path("/opt/homebrew/bin/ffmpeg").exists():
        return "/opt/homebrew/bin/ffmpeg"
    sys.exit("music-traits: ffmpeg not found")


def decode(path):
    """Returns the piece as mono float samples at RATE."""
    command = [ffmpeg_path(), "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"]
    data = subprocess.run(command, check=True, capture_output=True).stdout
    return np.frombuffer(data, dtype=np.float32).astype(np.float64)


def power_spectrogram(samples):
    """Returns the power spectrogram (frames x bins) and the bin frequencies."""
    padded = np.concatenate([np.zeros(FFT // 2), samples, np.zeros(FFT // 2)])
    count = 1 + (len(padded) - FFT) // HOP
    window = np.hanning(FFT)
    frames = np.empty((count, FFT // 2 + 1), dtype=np.float32)
    for start in range(0, count, 512):
        stop = min(count, start + 512)
        index = np.arange(start, stop)[:, None] * HOP + np.arange(FFT)[None, :]
        frames[start:stop] = np.abs(np.fft.rfft(padded[index] * window, axis=1)) ** 2
    return frames, np.fft.rfftfreq(FFT, 1.0 / RATE)


def loudness_db(samples, seconds):
    """Returns the RMS level in dB per block of `seconds`."""
    size = int(RATE * seconds)
    blocks = len(samples) // size
    rms = np.sqrt(np.mean(samples[: blocks * size].reshape(blocks, size) ** 2, axis=1) + 1e-12)
    return 20.0 * np.log10(rms)


def dynamic_arc(samples):
    """Returns the loudness of each tenth of the piece relative to its loudest tenth, the share
    where the loudest tenth lies, the range between quiet and loud seconds and the fade-in and
    fade-out times (until 6 dB below the median level)."""
    seconds = loudness_db(samples, 1.0)
    tenths = np.array_split(seconds, 10)
    levels = np.array([10.0 * np.log10(np.mean(10.0 ** (t / 10.0))) for t in tenths])
    relative = levels - levels.max()
    audible = seconds[seconds > seconds.max() - 50.0]
    threshold = np.median(audible) - 6.0
    above = np.nonzero(seconds >= threshold)[0]
    fade_in = float(above[0]) if len(above) else 0.0
    fade_out = float(len(seconds) - 1 - above[-1]) if len(above) else 0.0
    return {
        "arc": [round(float(v), 1) for v in relative],
        "peakAt": round((int(np.argmax(levels)) + 0.5) / 10.0, 2),
        "range": round(float(np.percentile(audible, 95) - np.percentile(audible, 10)), 1),
        "fadeIn": fade_in,
        "fadeOut": fade_out,
    }


def weighted_percentile(values, weights, share):
    order = np.argsort(values)
    cumulative = np.cumsum(weights[order])
    return float(values[order][np.searchsorted(cumulative, share * cumulative[-1])])


def spectrum_traits(power, freqs):
    """Returns centroid, brightness (energy share above 1 kHz) and register (MIDI percentiles)."""
    energy = power.sum(axis=1)
    loud = energy > energy.max() * 1e-4
    centroid = (power[loud] * freqs).sum(axis=1) / (energy[loud] + 1e-12)
    total = power[loud].sum(axis=0)
    band = (freqs >= 30.0) & (freqs <= 4000.0)
    midi = 69.0 + 12.0 * np.log2(freqs[band] / 440.0)
    weights = total[band]
    return {
        "centroid": round(float(np.average(centroid, weights=energy[loud])), 0),
        "brightness": round(float(total[freqs >= 1000.0].sum() / total.sum()) * 100.0, 1),
        "registerLow": round(weighted_percentile(midi, weights, 0.1), 0),
        "registerMid": round(weighted_percentile(midi, weights, 0.5), 0),
        "registerHigh": round(weighted_percentile(midi, weights, 0.9), 0),
    }


def onset_strength(power):
    """Returns the spectral flux of the log magnitudes per frame."""
    magnitude = np.log1p(1000.0 * np.sqrt(power) / (np.sqrt(power).max() + 1e-12))
    flux = np.maximum(0.0, np.diff(magnitude, axis=0)).sum(axis=1)
    return np.concatenate([[0.0], flux])


def detrended(strength):
    """Returns the onset strength minus its moving average over one second, so that slow swells
    count neither as onsets nor as a pulse."""
    width = int(round(RATE / HOP))
    average = np.convolve(strength, np.ones(width) / width, mode="same")
    return np.maximum(0.0, strength - average)


def onsets_per_second(strength):
    """Returns the onsets per second: peaks of the detrended flux, at least 140 ms apart and
    clearly above the piece's usual flux (mean plus two standard deviations)."""
    frame_rate = RATE / HOP
    flux = detrended(strength)
    threshold = flux.mean() + 2.0 * flux.std()
    count = 0
    for i in range(3, len(flux) - 3):
        if flux[i] > threshold and flux[i] == flux[i - 3: i + 4].max():
            count += 1
    return count / (len(flux) / frame_rate)


def tempo(strength):
    """Returns a tempo estimate in BPM (40..180, weighted towards 90) and its confidence."""
    frame_rate = RATE / HOP
    flux = detrended(strength)
    centred = flux - flux.mean()
    spectrum = np.fft.rfft(centred, n=2 * len(centred))
    autocorrelation = np.fft.irfft(spectrum * np.conj(spectrum))[: len(centred)]
    if autocorrelation[0] <= 0.0:
        return 0.0, 0.0
    autocorrelation /= autocorrelation[0]
    best, best_score, best_value = 0.0, -1.0, 0.0
    for lag in range(int(frame_rate * 60.0 / 180.0), int(frame_rate * 60.0 / 40.0) + 1):
        bpm = 60.0 * frame_rate / lag
        score = autocorrelation[lag] * np.exp(-0.5 * (np.log2(bpm / 90.0) / 0.9) ** 2)
        if score > best_score:
            best, best_score, best_value = bpm, score, autocorrelation[lag]
    return round(best, 0), round(float(best_value), 2)


def chroma(power, freqs):
    """Returns the chroma (frames x 12) of the band 55..2000 Hz."""
    band = (freqs >= 55.0) & (freqs <= 2000.0)
    classes = (np.round(69.0 + 12.0 * np.log2(freqs[band] / 440.0)).astype(int)) % 12
    result = np.zeros((power.shape[0], 12))
    for pitch_class in range(12):
        result[:, pitch_class] = power[:, band][:, classes == pitch_class].sum(axis=1)
    return result


def key_of(chroma_frames):
    """Returns key name, mode and the correlation with the best key profile."""
    total = np.sqrt(chroma_frames).sum(axis=0)
    best = (-2.0, 0, "major")
    for tonic in range(12):
        for mode, profile in (("major", MAJOR_PROFILE), ("minor", MINOR_PROFILE)):
            r = float(np.corrcoef(total, np.roll(profile, tonic))[0, 1])
            best = max(best, (r, tonic, mode))
    return NOTE_NAMES[best[1]], best[2], round(best[0], 2)


def triad_templates():
    templates, names = [], []
    for root in range(12):
        for name, third in (("maj", 4), ("min", 3)):
            template = np.zeros(12)
            template[[root, (root + third) % 12, (root + 7) % 12]] = 1.0
            templates.append(template / np.linalg.norm(template))
            names.append(f"{root}{name}")
    return np.array(templates), names


def harmonic_rhythm(chroma_frames, power):
    """Returns chord changes per minute and the mean number of strong pitch classes, from
    triads matched to one-second blocks (no chord names are kept)."""
    block = int(round(RATE / HOP))
    templates, _ = triad_templates()
    energy = power.sum(axis=1)
    labels, voices = [], []
    for start in range(0, len(chroma_frames) - block + 1, block):
        part = np.sqrt(chroma_frames[start: start + block]).sum(axis=0)
        if energy[start: start + block].mean() < energy.max() * 1e-4 or part.max() <= 0.0:
            labels.append(-1)
            continue
        normalised = part / np.linalg.norm(part)
        labels.append(int(np.argmax(templates @ normalised)))
        voices.append(int((part >= 0.5 * part.max()).sum()))
    smoothed = []
    for i in range(len(labels)):
        window = [v for v in labels[max(0, i - 2): i + 3] if v >= 0]
        smoothed.append(max(set(window), key=window.count) if window else -1)
    voiced = [v for v in smoothed if v >= 0]
    changes = sum(1 for a, b in zip(voiced, voiced[1:]) if a != b)
    minutes = max(len(voiced) / 60.0, 1e-9)
    return round(changes / minutes, 1), round(float(np.mean(voices)) if voices else 0.0, 1)


def decay_rt60(power, freqs):
    """Returns the reverb time estimated from the steepest free decays (dB/s of the 300..4000 Hz
    level over 0.4 s windows; the steepest 5 % are taken as the room's tail)."""
    band = (freqs >= 300.0) & (freqs <= 4000.0)
    level = 10.0 * np.log10(power[:, band].sum(axis=1) + 1e-12)
    frame_rate = RATE / HOP
    span = max(3, int(0.4 * frame_rate))
    slopes = []
    t = np.arange(span) / frame_rate
    for start in range(0, len(level) - span, 2):
        part = level[start: start + span]
        if part.max() < level.max() - 45.0:
            continue
        slope, intercept = np.polyfit(t, part, 1)
        residual = part - (slope * t + intercept)
        if slope < 0.0 and residual.std() < 1.5:
            slopes.append(slope)
    if not slopes:
        return 0.0
    steep = np.percentile(slopes, 5)
    return round(float(60.0 / -steep), 1) if steep < 0.0 else 0.0


def analyse(path):
    samples = decode(path)
    power, freqs = power_spectrogram(samples)
    strength = onset_strength(power)
    bpm, confidence = tempo(strength)
    frames = chroma(power, freqs)
    tonic, mode, key_strength = key_of(frames)
    changes, voices = harmonic_rhythm(frames, power)
    traits = {
        "seconds": round(len(samples) / RATE, 0),
        "tempo": bpm,
        "tempoConfidence": confidence,
        "key": f"{tonic} {mode}",
        "mode": mode,
        "keyStrength": key_strength,
        "chordChangesPerMinute": changes,
        "pitchClasses": voices,
        "onsetsPerSecond": round(onsets_per_second(strength), 2),
        "rt60": decay_rt60(power, freqs),
    }
    traits.update(spectrum_traits(power, freqs))
    traits.update(dynamic_arc(samples))
    return traits


def group_of_track(json_path):
    slots = json.loads(json_path.read_text(encoding="utf-8")).get("slots", [])
    slot = slots[0] if slots else "unassigned"
    return slot.split(":")[-1]


def collect(args):
    pieces = []
    if args.tracks:
        for json_path in sorted(pathlib.Path(args.tracks).glob("*.json")):
            audio = json_path.with_suffix(".ogg")
            if audio.exists():
                pieces.append(("track", group_of_track(json_path), audio))
    if args.pieces:
        for wav in sorted(pathlib.Path(args.pieces).glob("music-*.wav")):
            match = re.fullmatch(r"music-(.+)-(\d+)\.wav", wav.name)
            if match:
                pieces.append(("generated", match.group(1), wav))
    return pieces


NUMERIC = ["seconds", "tempo", "chordChangesPerMinute", "pitchClasses", "onsetsPerSecond", "rt60", "centroid",
           "brightness", "registerLow", "registerMid", "registerHigh", "peakAt", "range", "fadeIn", "fadeOut"]


def summarise(rows):
    summary = {}
    for name in NUMERIC:
        values = [row[name] for row in rows]
        summary[name] = (min(values), float(np.mean(values)), max(values))
    modes = sorted({row["mode"] for row in rows})
    summary["mode"] = "/".join(modes)
    arcs = np.array([row["arc"] for row in rows])
    summary["arc"] = [round(float(v), 0) for v in arcs.mean(axis=0)]
    return summary


def span(triple, digits=0):
    low, mean, high = triple
    if abs(high - low) < 1e-9:
        return f"{mean:.{digits}f}"
    return f"{mean:.{digits}f} ({low:.{digits}f}–{high:.{digits}f})"


def print_table(title, grouped):
    print(f"\n### {title}\n")
    print("| Platz | n | Länge s | Tempo bpm | Modus | Akkordwechsel/min | Tonklassen | Einsätze/s | Register (MIDI 10/50/90 %) "
          "| Schwerpunkt Hz | über 1 kHz % | Nachhall s | lautestes Zehntel | Dynamik dB | Ein-/Ausblenden s | Bogen (dB je Zehntel) |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    order = [g for g in SLOT_ORDER if g in grouped] + sorted(g for g in grouped if g not in SLOT_ORDER)
    for group in order:
        s = summarise(grouped[group])
        register = f"{s['registerLow'][1]:.0f}/{s['registerMid'][1]:.0f}/{s['registerHigh'][1]:.0f}"
        fades = f"{s['fadeIn'][1]:.0f}/{s['fadeOut'][1]:.0f}"
        arc = " ".join(f"{v:.0f}" for v in s["arc"])
        print(f"| {group} | {len(grouped[group])} | {span(s['seconds'])} | {span(s['tempo'])} | {s['mode']} "
              f"| {span(s['chordChangesPerMinute'], 1)} | {span(s['pitchClasses'], 1)} | {span(s['onsetsPerSecond'], 2)} "
              f"| {register} | {span(s['centroid'])} | {span(s['brightness'], 1)} | {span(s['rt60'], 1)} "
              f"| {span(s['peakAt'], 2)} | {span(s['range'], 1)} | {fades} | {arc} |")


def print_pieces(rows):
    print("\n| Stück | Platz | Länge s | Tempo (Sicherheit) | Tonart (Stärke) | Akkordwechsel/min | Einsätze/s | Nachhall s "
          "| Schwerpunkt Hz |")
    print("|---|---|---|---|---|---|---|---|---|")
    for kind, group, path, traits in rows:
        print(f"| {path.stem} ({kind}) | {group} | {traits['seconds']:.0f} | {traits['tempo']:.0f} ({traits['tempoConfidence']}) "
              f"| {traits['key']} ({traits['keyStrength']}) | {traits['chordChangesPerMinute']} | {traits['onsetsPerSecond']} "
              f"| {traits['rt60']} | {traits['centroid']:.0f} |")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tracks")
    parser.add_argument("--pieces")
    parser.add_argument("--per-piece", action="store_true")
    parser.add_argument("--json")
    args = parser.parse_args()
    pieces = collect(args)
    if not pieces:
        sys.exit("music-traits: no pieces found (--tracks or --pieces)")

    rows = []
    for kind, group, path in pieces:
        print(f"analysing {path.name}", file=sys.stderr)
        rows.append((kind, group, path, analyse(path)))

    for kind, title in (("track", "Stücke"), ("generated", "Erzeugt")):
        grouped = {}
        for row_kind, group, _, traits in rows:
            if row_kind == kind:
                grouped.setdefault(group, []).append(traits)
        if grouped:
            print_table(title, grouped)
    if args.per_piece:
        print_pieces(rows)
    if args.json:
        data = [{"kind": k, "group": g, "file": p.name, **t} for k, g, p, t in rows]
        pathlib.Path(args.json).write_text(json.dumps(data, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
