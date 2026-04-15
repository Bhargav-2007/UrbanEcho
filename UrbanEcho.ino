/*
 * Project: UrbanEcho - TinyML Urban Soundscape Monitor
 * Hardware: ESP32 WROOM + INMP441 I2S Microphone
 * Framework: Arduino (ESP32 Core) + Edge Impulse
 * Network: Wi-Fi HTTP POST Integration
 * 
 * Features:
 * - Real-time audio capture via I2S DMA
 * - Edge Impulse TinyML inference
 * - Intelligent event buffering and upload
 * - Robust Wi-Fi reconnection with exponential backoff
 * - Comprehensive error handling and logging
 */

#include <driver/i2s.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>

// Edge Impulse exported library headers
#if __has_include(<bhargav_2007_project_1_inferencing.h>)
#include <bhargav_2007_project_1_inferencing.h>
#elif __has_include(<bhargav-2007-project-1_inferencing.h>)
#include <bhargav-2007-project-1_inferencing.h>
#elif __has_include(<urban-echo_inferencing.h>)
#include <urban-echo_inferencing.h>
#elif __has_include(<urban_echo_inferencing.h>)
#include <urban_echo_inferencing.h>
#else
#error "Edge Impulse inferencing header not found. Install your exported Edge Impulse Arduino library (.zip)."
#endif

// ============================================================================
// Configuration & Constants
// ============================================================================

// Wi-Fi Configuration
const char* WIFI_SSID     = "Bhargav";
const char* WIFI_PASSWORD = "radheradhe";
const char* SERVER_HOST   = "10.23.241.101";
const uint16_t SERVER_PORT = 3000;
const char* SERVER_PATH   = "/api/upload";

String get_server_url() {
    return String("http://") + SERVER_HOST + ":" + String(SERVER_PORT) + SERVER_PATH;
}

// Hardware Pin Definitions
#define I2S_SCK_PIN     26 // BCLK
#define I2S_WS_PIN      25 // LRC
#define I2S_SD_PIN      33 // Data In
#define I2S_PORT        I2S_NUM_0

// Audio Specifications
#define SAMPLE_RATE     16000

// System Timing (milliseconds)
#define WIFI_RECONNECT_BASE_DELAY   5000  // 5 seconds
#define WIFI_RECONNECT_MAX_DELAY    60000 // 60 seconds
#define WIFI_CONNECT_TIMEOUT        20000 // 20 seconds
#define UPLOAD_RETRY_DELAY          2000  // 2 seconds
#define UPLOAD_MAX_RETRIES          3

// Classification Thresholds
#define CONFIDENCE_THRESHOLD        0.80   // 80% confidence
#define BUFFER_MAX_EVENTS           10     // Max events to buffer

// ============================================================================
// Data Structures
// ============================================================================

struct AudioEvent {
    String label;
    float confidence;
    unsigned long timestamp;
};

struct SystemState {
    bool wifi_connected;
    unsigned long last_wifi_attempt;
    unsigned long wifi_reconnect_delay;
    unsigned long last_inference;
    unsigned long inference_count;
    std::vector<AudioEvent> event_buffer;
};

// ============================================================================
// Global Variables
// ============================================================================

int16_t sampleBuffer[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
SystemState system_state = {
    .wifi_connected = false,
    .last_wifi_attempt = 0,
    .wifi_reconnect_delay = WIFI_RECONNECT_BASE_DELAY,
    .last_inference = 0,
    .inference_count = 0,
    .event_buffer = {}
};

// ============================================================================
// Logging Utilities
// ============================================================================

void log_info(const char* message) {
    Serial.print("[INFO] ");
    Serial.println(message);
}

void log_warn(const char* message) {
    Serial.print("[WARN] ");
    Serial.println(message);
}

void log_error(const char* message) {
    Serial.print("[ERROR] ");
    Serial.println(message);
}

void log_debug(const char* message) {
    Serial.print("[DEBUG] ");
    Serial.println(message);
}

// ============================================================================
// Wi-Fi Management
// ============================================================================

void setup_wifi() {
    log_info("Initializing Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
}

bool attempt_wifi_connection() {
    unsigned long now = millis();
    
    // Check if enough time has passed for retry
    if (now - system_state.last_wifi_attempt < system_state.wifi_reconnect_delay) {
        return false;
    }
    
    system_state.last_wifi_attempt = now;
    
    if (WiFi.status() == WL_CONNECTED) {
        system_state.wifi_connected = true;
        system_state.wifi_reconnect_delay = WIFI_RECONNECT_BASE_DELAY; // Reset backoff
        return true;
    }
    
    Serial.printf("[INFO] Attempting Wi-Fi connection... SSID: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    // Wait for connection with timeout
    unsigned long start_time = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[INFO] Wi-Fi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        system_state.wifi_connected = true;
        system_state.wifi_reconnect_delay = WIFI_RECONNECT_BASE_DELAY;
        return true;
    } else {
        system_state.wifi_connected = false;
        
        // Exponential backoff for reconnection attempts
        system_state.wifi_reconnect_delay = min(
            system_state.wifi_reconnect_delay * 2,
            (unsigned long)WIFI_RECONNECT_MAX_DELAY
        );
        
        Serial.printf("[WARN] Wi-Fi connection failed. Next attempt in %lu ms\n", 
                      system_state.wifi_reconnect_delay);
        return false;
    }
}

// ============================================================================
// I2S Audio Capture
// ============================================================================

void setup_i2s() {
    log_info("Initializing I2S audio interface...");
    
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_PIN
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        log_error("Failed to install I2S driver");
        return;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        log_error("Failed to set I2S pins");
        return;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    log_info("I2S initialized successfully");
}

bool capture_audio() {
    size_t bytes_read = 0;
    size_t total_bytes_needed = EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t);
    
    esp_err_t err = i2s_read(I2S_PORT, (void*)sampleBuffer, total_bytes_needed, 
                             &bytes_read, portMAX_DELAY);
    
    if (err != ESP_OK) {
        log_error("I2S read failed");
        return false;
    }
    
    return bytes_read == total_bytes_needed;
}

// ============================================================================
// Edge Impulse Signal Processing
// ============================================================================

int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    if (offset + length > EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
        return -1;
    }
    
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = (float)sampleBuffer[offset + i];
    }
    
    return 0;
}

bool run_inference(const char** highest_label, float* highest_confidence) {
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &microphone_audio_signal_get_data;

    ei_impulse_result_t result = {0};
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);
    
    if (res != EI_IMPULSE_OK) {
        log_error("Inference failed");
        return false;
    }

    *highest_confidence = 0.0;
    *highest_label = "Unknown";

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > *highest_confidence) {
            *highest_confidence = result.classification[ix].value;
            *highest_label = result.classification[ix].label;
        }
    }

    system_state.last_inference = millis();
    system_state.inference_count++;

    return true;
}

// ============================================================================
// Event Buffering & Upload
// ============================================================================

void buffer_event(const char* label, float confidence) {
    if (system_state.event_buffer.size() >= BUFFER_MAX_EVENTS) {
        log_warn("Event buffer full, dropping oldest event");
        system_state.event_buffer.erase(system_state.event_buffer.begin());
    }

    AudioEvent event = {
        .label = String(label),
        .confidence = confidence,
        .timestamp = millis()
    };

    system_state.event_buffer.push_back(event);
    Serial.printf("[BUFFER] Added event: %s (%.2f%%) - Buffer size: %d\n", 
                  label, confidence * 100.0, system_state.event_buffer.size());
}

bool upload_event(const AudioEvent& event) {
    if (!system_state.wifi_connected) {
        log_warn("Wi-Fi not connected, cannot upload");
        return false;
    }

    HTTPClient http;
    String server_url = get_server_url();
    http.begin(server_url);
    http.addHeader("Content-Type", "application/json");

    // Construct JSON payload
    char jsonPayload[256];
    snprintf(jsonPayload, sizeof(jsonPayload),
             "{\"event_type\":\"%s\", \"confidence\":%.2f, \"timestamp\":%lu}",
             event.label.c_str(), event.confidence, event.timestamp);

    Serial.printf("[UPLOAD] Sending: %s\n", jsonPayload);

    int httpResponseCode = http.POST(jsonPayload);
    http.end();

    if (httpResponseCode > 0) {
        if (httpResponseCode >= 200 && httpResponseCode < 300) {
            Serial.printf("[UPLOAD] Success (HTTP %d)\n", httpResponseCode);
            return true;
        } else {
            Serial.printf("[UPLOAD] Server error (HTTP %d)\n", httpResponseCode);
            return false;
        }
    } else {
        log_error("HTTP request failed");
        return false;
    }
}

void process_event_buffer() {
    if (system_state.event_buffer.empty() || !system_state.wifi_connected) {
        return;
    }

    // Try to upload buffered events
    auto it = system_state.event_buffer.begin();
    while (it != system_state.event_buffer.end()) {
        bool success = upload_event(*it);

        if (success) {
            it = system_state.event_buffer.erase(it);
        } else {
            ++it; // Keep failed event for retry
            break; // Stop trying after first failure
        }
    }
}

// ============================================================================
// Main Setup & Loop
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n╔════════════════════════════════════════════════════╗");
    Serial.println("║  UrbanEcho: TinyML Urban Soundscape Monitor        ║");
    Serial.println("║  ESP32 + INMP441 + Edge Impulse                   ║");
    Serial.println("╚════════════════════════════════════════════════════╝\n");

    log_info("System initialization started");
    
    setup_wifi();
    setup_i2s();
    
    log_info("System ready. Starting continuous inference.");
    Serial.println("─────────────────────────────────────────────────────\n");
}

void loop() {
    // 1. Attempt Wi-Fi connection if needed
    attempt_wifi_connection();

    // 2. Capture audio
    if (!capture_audio()) {
        log_error("Audio capture failed");
        delay(100);
        return;
    }

    // 3. Run inference
    const char* highest_label;
    float highest_confidence;
    
    if (!run_inference(&highest_label, &highest_confidence)) {
        log_error("Inference execution failed");
        delay(100);
        return;
    }

    // 4. Log result
    Serial.printf("[INFERENCE] %s (%.2f%%) - Inference #%lu\n", 
                  highest_label, highest_confidence * 100.0, system_state.inference_count);

    // 5. Smart triggering: buffer high-confidence events
    if (highest_confidence >= CONFIDENCE_THRESHOLD) {
        buffer_event(highest_label, highest_confidence);
    }

    // 6. Try to upload buffered events if Wi-Fi connected
    if (system_state.wifi_connected) {
        process_event_buffer();
    }

    delay(50); // Small delay to prevent overwhelming the serial/network
}
