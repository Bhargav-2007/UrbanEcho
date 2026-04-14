# UrbanEcho

UrbanEcho is a TinyML-driven urban soundscape monitor for **ESP32 + INMP441** using an Edge Impulse audio model.

It captures 1-second audio windows (16 kHz, mono, PCM16), runs local inference, and can upload high-confidence events to a dashboard API.

## Hardware

- ESP32 (WROOM / ESP32 Dev Module)
- INMP441 I2S microphone
- 3.3V power only

### Pin mapping (current firmware)

| Function | INMP441 | ESP32 GPIO |
|---|---|---|
| I2S BCLK | SCK | 26 |
| I2S WS | WS | 25 |
| I2S Data In | SD | 33 |
| Power | VDD | 3V3 |
| Ground | GND | GND |

## Audio format

- Sample rate: 16000 Hz
- Channels: Mono
- Sample type: signed 16-bit PCM
- Window size: `EI_CLASSIFIER_RAW_SAMPLE_COUNT` (typically 1 second)

## Firmware

Main file: [UrbanEcho.ino](UrbanEcho.ino)

The firmware:
- Captures audio over I2S with DMA
- Runs `run_classifier()` from Edge Impulse
- Finds highest-confidence label
- Posts selected events to HTTP API

### Configure before upload

Edit these values in [UrbanEcho.ino](UrbanEcho.ino):
- `WIFI_SSID`
- `WIFI_PASSWORD`
- `SERVER_URL` (must point to your API server IP/hostname)

Example:

```cpp
const char* SERVER_URL = "http://192.168.1.25:3000/api/upload";
```

## API server

Server file: [server.js](server.js)

Routes:
- `GET /api/health`
- `POST /api/upload`
- `GET /api/events?hours=24`

Start server:

```bash
cd /workspaces/UrbanEcho
bash scripts/start_urbanecho_api.sh
```

Health check:

```bash
curl http://localhost:3000/api/health
```

Note: MongoDB is optional. If Mongo is unavailable, server falls back to in-memory storage.

## Dataset curation (automated)

Script: [scripts/prepare_urbanecho_dataset.py](scripts/prepare_urbanecho_dataset.py)

It downloads ESC-50 and UrbanSound8K, selects relevant classes, and converts output to 16 kHz mono PCM16 WAV into:
- [Biophony](Biophony)
- [Anthropophony](Anthropophony)
- [Geophony](Geophony)

Run full pipeline:

```bash
cd /workspaces/UrbanEcho
bash scripts/run_dataset_pipeline.sh
```

## Edge Impulse bulk upload

Uploader script:
- [upload_edge_impulse_bulk.sh](upload_edge_impulse_bulk.sh)
- [scripts/upload_edge_impulse_bulk.sh](scripts/upload_edge_impulse_bulk.sh)

Upload to training:

```bash
cd /workspaces/UrbanEcho
bash upload_edge_impulse_bulk.sh training
```

Upload to testing:

```bash
cd /workspaces/UrbanEcho
bash upload_edge_impulse_bulk.sh testing
```

If `EI_API_KEY` is not set, the script supports interactive login via `edge-impulse-uploader`.

## End-to-end quick start

1. Start API:

```bash
cd /workspaces/UrbanEcho
bash scripts/start_urbanecho_api.sh
```

2. Prepare dataset:

```bash
cd /workspaces/UrbanEcho
bash scripts/run_dataset_pipeline.sh
```

3. Upload dataset:

```bash
cd /workspaces/UrbanEcho
bash upload_edge_impulse_bulk.sh training
```

4. Flash ESP32 from Arduino IDE and monitor Serial at 115200.

## Important note

This project is now standardized on **ESP32**. Any older references to BW16/RTL8720DN/AmebaD are deprecated and should not be used.
