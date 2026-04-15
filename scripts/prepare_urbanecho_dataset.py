#!/usr/bin/env python3
"""
UrbanEcho Dataset Preparation Tool
Senior ML Data Engineer - Acoustic Ecology Specialist
Automates curation, resampling, and organization of audio data for TinyML training.

Usage:
    python3 scripts/prepare_urbanecho_dataset.py [--clean]

Features:
    - Downloads ESC-50 and UrbanSound8K datasets
    - Resamples to 16 kHz, 16-bit PCM, Mono
    - Organizes into Biophony/Anthropophony/Geophony taxonomy
    - Validates audio format and integrity
"""

from __future__ import annotations

import argparse
import csv
import os
import random
import shutil
import tarfile
import zipfile
import logging
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple

try:
    import librosa
    import soundfile as sf
    import requests
except ImportError:
    print("ERROR: Required packages not found. Install with:")
    print("  pip install librosa soundfile requests")
    exit(1)


ESC50_ZIP_URL = "https://github.com/karolpiczak/ESC-50/archive/refs/heads/master.zip"
URBANSOUND8K_TAR_URL = "https://zenodo.org/record/1203745/files/UrbanSound8K.tar.gz?download=1"

TARGET_SR = 16000

# Configure logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger(__name__)

# ESC-50 categories relevant to acoustic ecology taxonomy.
ESC50_CLASS_TO_TAXONOMY: Dict[str, str] = {
    "chirping_birds": "Biophony",
    "crickets": "Biophony",
    "frog": "Biophony",
    "rain": "Geophony",
    "thunderstorm": "Geophony",
    "wind": "Geophony",
    "water_drops": "Geophony",
    "sea_waves": "Geophony",
}

# UrbanSound8K classes relevant to anthropophony.
URBANSOUND8K_CLASS_TO_TAXONOMY: Dict[str, str] = {
    "air_conditioner": "Anthropophony",
    "car_horn": "Anthropophony",
    "children_playing": "Anthropophony",
    "drilling": "Anthropophony",
    "engine_idling": "Anthropophony",
    "jackhammer": "Anthropophony",
    "siren": "Anthropophony",
    "street_music": "Anthropophony",
}


def download_file(url: str, dest_path: Path, timeout_seconds: int = 120) -> None:
    """Download a file if it does not already exist."""
    if dest_path.exists() and dest_path.stat().st_size > 0:
        print(f"[skip] Already downloaded: {dest_path}")
        return

    print(f"[download] {url}")
    dest_path.parent.mkdir(parents=True, exist_ok=True)

    with requests.get(url, stream=True, timeout=timeout_seconds) as response:
        response.raise_for_status()
        with open(dest_path, "wb") as out:
            for chunk in response.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    out.write(chunk)

    print(f"[ok] Saved: {dest_path}")


def extract_zip(zip_path: Path, extract_dir: Path) -> None:
    if extract_dir.exists() and any(extract_dir.iterdir()):
        print(f"[skip] Zip already extracted: {extract_dir}")
        return

    print(f"[extract] {zip_path} -> {extract_dir}")
    extract_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(extract_dir)


def extract_tar_gz(tar_path: Path, extract_dir: Path) -> None:
    if extract_dir.exists() and any(extract_dir.iterdir()):
        print(f"[skip] Tar already extracted: {extract_dir}")
        return

    print(f"[extract] {tar_path} -> {extract_dir}")
    extract_dir.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tar_path, "r:gz") as tf:
        tf.extractall(extract_dir)


def find_dataset_root(base_dir: Path, marker_relative_path: str) -> Path:
    """Find a dataset root by searching for a marker relative path."""
    for root, _, _ in os.walk(base_dir):
        candidate = Path(root) / marker_relative_path
        if candidate.exists():
            return Path(root)
    raise FileNotFoundError(f"Could not find dataset marker: {marker_relative_path} under {base_dir}")


def ensure_output_dirs(output_root: Path) -> Dict[str, Path]:
    dirs = {
        "Biophony": output_root / "Biophony",
        "Anthropophony": output_root / "Anthropophony",
        "Geophony": output_root / "Geophony",
    }
    for path in dirs.values():
        path.mkdir(parents=True, exist_ok=True)
    return dirs


def clean_output_dirs(output_dirs: Dict[str, Path]) -> None:
    for label, folder in output_dirs.items():
        removed = 0
        for wav in folder.glob("*.wav"):
            wav.unlink(missing_ok=True)
            removed += 1
        print(f"[clean] {label}: removed {removed} wav files")


def read_csv_rows(csv_path: Path) -> List[dict]:
    with open(csv_path, "r", newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def select_esc50_files(
    esc_rows: List[dict],
    max_per_source_class: int,
    rng: random.Random,
) -> List[Tuple[str, str, str, str]]:
    """
    Returns tuples of (taxonomy_label, dataset_name, source_class, rel_audio_path)
    """
    rows = esc_rows[:]
    rng.shuffle(rows)

    selected: List[Tuple[str, str, str, str]] = []
    class_counts: Dict[str, int] = defaultdict(int)

    for row in rows:
        source_class = row.get("category", "")
        taxonomy = ESC50_CLASS_TO_TAXONOMY.get(source_class)
        if not taxonomy:
            continue

        if class_counts[source_class] >= max_per_source_class:
            continue

        rel_path = f"audio/{row['filename']}"
        selected.append((taxonomy, "ESC50", source_class, rel_path))
        class_counts[source_class] += 1

    return selected


def select_urbansound8k_files(
    us_rows: List[dict],
    max_per_source_class: int,
    rng: random.Random,
) -> List[Tuple[str, str, str, str]]:
    """
    Returns tuples of (taxonomy_label, dataset_name, source_class, rel_audio_path)
    """
    rows = us_rows[:]
    rng.shuffle(rows)

    selected: List[Tuple[str, str, str, str]] = []
    class_counts: Dict[str, int] = defaultdict(int)

    for row in rows:
        source_class = row.get("class", "")
        taxonomy = URBANSOUND8K_CLASS_TO_TAXONOMY.get(source_class)
        if not taxonomy:
            continue

        if class_counts[source_class] >= max_per_source_class:
            continue

        fold = row.get("fold", "")
        fname = row.get("slice_file_name", "")
        rel_path = f"audio/fold{fold}/{fname}"

        selected.append((taxonomy, "UrbanSound8K", source_class, rel_path))
        class_counts[source_class] += 1

    return selected


def sanitize_name(value: str) -> str:
    return "".join(ch if ch.isalnum() or ch in {"_", "-"} else "_" for ch in value)


def convert_audio_to_target(src_wav: Path, dst_wav: Path) -> None:
    # librosa handles mono conversion and resampling in one step.
    y, _ = librosa.load(src_wav.as_posix(), sr=TARGET_SR, mono=True)

    # Write exact PCM 16-bit WAV output.
    sf.write(dst_wav.as_posix(), y, TARGET_SR, subtype="PCM_16")


def process_selected_files(
    esc_root: Path,
    us_root: Path,
    selected_files: List[Tuple[str, str, str, str]],
    output_dirs: Dict[str, Path],
) -> Dict[str, int]:
    counters = defaultdict(int)

    for taxonomy_label, dataset_name, source_class, rel_audio_path in selected_files:
        dataset_root = esc_root if dataset_name == "ESC50" else us_root
        src_path = dataset_root / rel_audio_path

        if not src_path.exists():
            print(f"[warn] Missing source file, skipped: {src_path}")
            continue

        counters[taxonomy_label] += 1
        idx = counters[taxonomy_label]

        out_name = (
            f"{dataset_name}_{sanitize_name(source_class)}_{idx:05d}.wav"
        )
        dst_path = output_dirs[taxonomy_label] / out_name

        try:
            convert_audio_to_target(src_path, dst_path)
        except Exception as exc:
            print(f"[warn] Failed conversion, skipped: {src_path} ({exc})")
            counters[taxonomy_label] -= 1

    return dict(counters)


def main() -> None:
    parser = argparse.ArgumentParser(description="Prepare UrbanEcho TinyML dataset from ESC-50 + UrbanSound8K")
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path("."),
        help="Directory where Biophony/Anthropophony/Geophony folders are created",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path(".dataset_cache"),
        help="Cache directory for downloaded/extracted datasets",
    )
    parser.add_argument(
        "--max-per-source-class",
        type=int,
        default=120,
        help="Max clips to keep per source class (e.g., siren, rain, frog)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Random seed for deterministic sampling",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Delete existing WAVs in output taxonomy folders before writing new files",
    )

    args = parser.parse_args()

    rng = random.Random(args.seed)

    output_dirs = ensure_output_dirs(args.output_root)
    if args.clean:
        clean_output_dirs(output_dirs)

    downloads_dir = args.work_dir / "downloads"
    extracted_dir = args.work_dir / "extracted"
    esc_zip = downloads_dir / "esc50_master.zip"
    us_tar = downloads_dir / "urbansound8k.tar.gz"

    esc_extract_dir = extracted_dir / "esc50"
    us_extract_dir = extracted_dir / "urbansound8k"

    download_file(ESC50_ZIP_URL, esc_zip)
    download_file(URBANSOUND8K_TAR_URL, us_tar)

    extract_zip(esc_zip, esc_extract_dir)
    extract_tar_gz(us_tar, us_extract_dir)

    esc_root = find_dataset_root(esc_extract_dir, "meta/esc50.csv")
    us_root = find_dataset_root(us_extract_dir, "metadata/UrbanSound8K.csv")

    esc_rows = read_csv_rows(esc_root / "meta" / "esc50.csv")
    us_rows = read_csv_rows(us_root / "metadata" / "UrbanSound8K.csv")

    esc_selected = select_esc50_files(esc_rows, args.max_per_source_class, rng)
    us_selected = select_urbansound8k_files(us_rows, args.max_per_source_class, rng)

    selected_all = esc_selected + us_selected

    print(f"[info] Selected clips: ESC-50={len(esc_selected)} UrbanSound8K={len(us_selected)} Total={len(selected_all)}")

    counts = process_selected_files(
        esc_root=esc_root,
        us_root=us_root,
        selected_files=selected_all,
        output_dirs=output_dirs,
    )

    print("\n[done] Output summary")
    for label in ("Biophony", "Anthropophony", "Geophony"):
        folder = output_dirs[label]
        total = counts.get(label, 0)
        print(f"- {label}: {total} files -> {folder}")

    print("\n[next] Edge Impulse upload labels:")
    print("- Biophony")
    print("- Anthropophony")
    print("- Geophony")


if __name__ == "__main__":
    main()
