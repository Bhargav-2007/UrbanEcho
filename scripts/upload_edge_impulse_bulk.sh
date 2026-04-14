#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

USE_API_KEY=1
if [[ -z "${EI_API_KEY:-}" ]]; then
  USE_API_KEY=0
  echo "[info] EI_API_KEY is not set. Will use interactive Edge Impulse login flow."
fi

CATEGORY="${1:-training}"
if [[ "$CATEGORY" != "training" && "$CATEGORY" != "testing" ]]; then
  echo "[error] Category must be 'training' or 'testing'"
  exit 1
fi

if ! command -v edge-impulse-uploader >/dev/null 2>&1; then
  echo "[info] edge-impulse-uploader not found. Installing globally..."
  npm install -g edge-impulse-cli
fi

if [[ "$USE_API_KEY" == "0" ]]; then
  echo "[info] Running one-time interactive login check..."
  echo "[info] Complete the prompts once, then bulk upload will continue automatically."
  edge-impulse-uploader --help >/dev/null 2>&1 || true
fi

upload_folder() {
  local label="$1"
  local folder="$ROOT_DIR/$label"

  if [[ ! -d "$folder" ]]; then
    echo "[warn] Missing folder: $folder (skipping)"
    return
  fi

  local count
  count=$(find "$folder" -type f -name "*.wav" | wc -l | tr -d ' ')
  echo "[upload] $label ($count files)"

  if [[ "$count" == "0" ]]; then
    echo "[warn] No WAV files in $folder"
    return
  fi

  if [[ "$USE_API_KEY" == "1" ]]; then
    find "$folder" -type f -name "*.wav" -print0 | while IFS= read -r -d '' f; do
      edge-impulse-uploader --api-key "$EI_API_KEY" --category "$CATEGORY" --label "$label" "$f"
    done
  else
    local first_file
    first_file="$(find "$folder" -type f -name "*.wav" | head -n 1)"

    if [[ -z "$first_file" ]]; then
      echo "[warn] No WAV files in $folder"
      return
    fi

    # First call may prompt for interactive login.
    edge-impulse-uploader --category "$CATEGORY" --label "$label" "$first_file"

    find "$folder" -type f -name "*.wav" | tail -n +2 | while IFS= read -r f; do
      edge-impulse-uploader --category "$CATEGORY" --label "$label" "$f"
    done
  fi
}

upload_folder "Biophony"
upload_folder "Anthropophony"
upload_folder "Geophony"

echo "[done] Upload complete for category: $CATEGORY"
