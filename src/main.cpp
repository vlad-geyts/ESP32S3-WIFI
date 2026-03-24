#include <Arduino.h>
#include <Preferences.h>  // The Preferences library is unique to Arduino-esp32
                          // It uses a portion of on-board non-volitile memory
                          // (NVS) of the ESP32 to store data.
                          // Preferences data is stored in NVS in sections
                          // called "namespace". There  are e set of key-value pars.
                          // LIke variables, a key-value pair has a data type.
#include <WiFi.h>
#include <string_view>    // C++17 header for high-performance string handling
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

    // WiFi Credentials using std::string_view
    // This is C++17's way to point to a string without copying it into memory.
    constexpr std::string_view Ssid     = "ASI Personal";
    constexpr std::string_view Password = "Personal@AcceleratedSystems";
}

// Global Objects
constexpr bool RW_MODE = false;
constexpr bool RO_MODE = true;
Preferences prefs;          // Created name of the Preferences object. This object is 
                            // used with the Preferences methods to access the
                            // name space and the key-value pairs it contains.
                                    
SemaphoreHandle_t panicSemaphore;

// Prototypes
void IRAM_ATTR handleButtonInterrupt();
void panicTask(void* pvParameters);
void heartbeatTask(void* pvParameters);
void initWiFi(); // New function for Phase 5

void setup() {
    delay(1000);
    Serial.begin(115200);

    // Initialise WiFi early in the boot sequence
    initWiFi();

    // Initialise NVS and read lifetime count
    prefs.begin("system", RO_MODE);     // Open namespace "system" and make it available
                                        // in READ ONLY (RO) mode.

      // 'auto' - C++17 type deduction (compiler knows this is a uint32_t)
    auto totalPanics = prefs.getUInt("panic_count", 0);   // Retrive value of the "panic_count" 
                                                          // key, define to 0 if not found
    Serial.printf("Bootup - Lifetime Panic Events: %u\n", totalPanics);
    prefs.end(); // Close our preference namespace.

    // Create binary semaphore
    panicSemaphore = xSemaphoreCreateBinary();

    // Configure Hardware using our Namespace
    pinMode(Config::ButtonPin, INPUT_PULLUP);
    pinMode(Config::StrobPin,  OUTPUT);
    pinMode(Config::PanicPin,  OUTPUT);
    pinMode(Config::LedPin,    OUTPUT);

    attachInterrupt(digitalPinToInterrupt(Config::ButtonPin), handleButtonInterrupt, FALLING);

    // FreeRTOS Task Creation
    xTaskCreatePinnedToCore(panicTask, "Panic", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(heartbeatTask, "Heartbeat", 4096, NULL, 1, NULL, 0);

    // Terminate the initial Arduino setup task to free up memory
    vTaskDelete(NULL);
}

// The Linker needs this function to exist, even if it is never called.
void loop() {}

// --- WiFi Initialization ---
void initWiFi() {
    Serial.print("Connecting to WiFi...");
    // .data() converts string_view back to the char* that WiFi.begin needs
    // .data() provides the raw pointer WiFi.begin() requires   
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

// --- Interrupt Service Routine ---
void IRAM_ATTR handleButtonInterrupt() {
    digitalWrite(Config::StrobPin, HIGH);

    // static ensures this variable is created once, not every time ISR runs
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(panicSemaphore, &xHigherPriorityTaskWoken);
    
    digitalWrite(Config::StrobPin, LOW);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// --- High Priority Panic Handler ---
void panicTask(void* pvParameters) {
    for(;;) {
        // Task blocks here (0% CPU) waiting for the semaphore
        // portMAX_DELAY means the task sleeps until the semaphore is "given"
        if (xSemaphoreTake(panicSemaphore, portMAX_DELAY) == pdPASS) {
            detachInterrupt(digitalPinToInterrupt(Config::ButtonPin));
            digitalWrite(Config::PanicPin, HIGH);

             // Update (increment) NVS Persistence
            prefs.begin("system", RW_MODE);
            auto count = prefs.getUInt("panic_count", 0) + 1;
            prefs.putUInt("panic_count", count);
            prefs.end();

            Serial.printf("[Panic] Event #%u stored. WiFi Status: %d\n", count, WiFi.status());

             // Visual strobe feedback
            for(int i = 0; i < 20; ++i) {
                digitalWrite(Config::LedPin, !digitalRead(Config::LedPin));
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            digitalWrite(Config::PanicPin, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));  // Cool-down delay
            
            // C++17 "Flush": Empty the semaphore of any extra signals from bouncing
            // C++17 style: we can use 'while' to clear any pending semaphores (bounces)
            while(xSemaphoreTake(panicSemaphore, 0) == pdPASS); 
            
            // Re-arm the hardware
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