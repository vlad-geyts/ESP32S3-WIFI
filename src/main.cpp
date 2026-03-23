#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string_view> // C++17 header for high-performance string handling
                       // Just to data members: a pointer and a length. 
                       // Does not created a copy in memory. Read-only.

// --- Modern C++: Namespaces & Constexpr ---
// We use a namespace to group related constants. This prevents "LED_PIN" from 
// accidentally conflicting with other libraries.
namespace Config {
    // 'constexpr' tells the compiler this value is known at compile-time.
    // It is more efficient than 'const' and safer than '#define'.
    constexpr int ButtonPin = 47;
    constexpr int LedPin    = 2;
    constexpr int StrobPin  = 48;
    constexpr int PanicPin  = 45;

    // WiFi Credentials (Replace with your own)
    // std::string_view is C++17. It doesn't copy the string memory, making it faster.
    constexpr std::string_view Ssid     = "Your_WiFi_SSID";
    constexpr std::string_view Password = "Your_WiFi_Password";
}

// Global Objects
Preferences prefs;
SemaphoreHandle_t panicSemaphore;

// Prototypes
void IRAM_ATTR handleButtonInterrupt();
void panicTask(void* pvParameters);
void heartbeatTask(void* pvParameters);
void initWiFi(); // New function for Phase 5

void setup() {
    delay(1000);
    Serial.begin(115200);

    // Initialise NVS and read lifetime count
    prefs.begin("system", true);
    // Use 'auto' - C++ deduces this is a uint32_t automatically
    auto totalPanics = prefs.getUInt("panic_count", 0); 
    Serial.printf("Bootup - Lifetime Panic Events: %u\n", totalPanics);
    prefs.end();

    initWiFi();

    panicSemaphore = xSemaphoreCreateBinary();

    // Pin Configurations using our new Namespace
    pinMode(Config::ButtonPin, INPUT_PULLUP);
    pinMode(Config::StrobPin,  OUTPUT);
    pinMode(Config::PanicPin,  OUTPUT);
    pinMode(Config::LedPin,    OUTPUT);

    attachInterrupt(digitalPinToInterrupt(Config::ButtonPin), handleButtonInterrupt, FALLING);

    // Task Creation
    xTaskCreatePinnedToCore(panicTask, "Panic", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(heartbeatTask, "Heartbeat", 4096, NULL, 1, NULL, 0);

    vTaskDelete(NULL);
}

// --- WiFi Initialization ---
void initWiFi() {
    Serial.print("Connecting to WiFi...");
    // .data() converts string_view back to the char* that WiFi.begin needs
    WiFi.begin(Config::Ssid.data(), Config::Password.data());

    // Simple connection loop
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nCONNECTED!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

void IRAM_ATTR handleButtonInterrupt() {
    digitalWrite(Config::StrobPin, HIGH);

    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(panicSemaphore, &xHigherPriorityTaskWoken);
    
    digitalWrite(Config::StrobPin, LOW);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void panicTask(void* pvParameters) {
    for(;;) {
        // portMAX_DELAY means the task sleeps until the semaphore is "given"
        if (xSemaphoreTake(panicSemaphore, portMAX_DELAY) == pdPASS) {
            detachInterrupt(digitalPinToInterrupt(Config::ButtonPin));
            digitalWrite(Config::PanicPin, HIGH);

            // Increment Persistence
            prefs.begin("system", false);
            auto count = prefs.getUInt("panic_count", 0) + 1;
            prefs.putUInt("panic_count", count);
            prefs.end();

            Serial.printf("[Panic] Event #%u stored. WiFi Status: %d\n", count, WiFi.status());

            // Strobe feedback
            for(int i = 0; i < 20; ++i) {
                digitalWrite(Config::LedPin, !digitalRead(Config::LedPin));
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            digitalWrite(Config::PanicPin, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // C++17 style: we can use 'while' to clear any pending semaphores (bounces)
            while(xSemaphoreTake(panicSemaphore, 0) == pdPASS); 
            
            attachInterrupt(digitalPinToInterrupt(Config::ButtonPin), handleButtonInterrupt, FALLING);
        }
    }
}

void heartbeatTask(void* pvParameters) {
    for(;;) {
        digitalWrite(Config::LedPin, !digitalRead(Config::LedPin));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}