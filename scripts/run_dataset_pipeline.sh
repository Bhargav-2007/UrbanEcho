#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "[1/4] Creating Python virtual environment (.venv)"
python3 -m venv .venv

# shellcheck disable=SC1091
source .venv/bin/activate

echo "[2/4] Upgrading pip"
python -m pip install --upgrade pip

echo "[3/4] Installing Python dependencies"
pip install requests librosa soundfile numpy scipy

echo "[4/4] Running UrbanEcho dataset preparation"
python scripts/prepare_urbanecho_dataset.py \
  --output-root "$ROOT_DIR" \
  --work-dir "$ROOT_DIR/.dataset_cache" \
  --clean \
  --max-per-source-class 120

echo ""
echo "[done] Dataset prepared in:"
echo "- $ROOT_DIR/Biophony"
echo "- $ROOT_DIR/Anthropophony"
echo "- $ROOT_DIR/Geophony"
