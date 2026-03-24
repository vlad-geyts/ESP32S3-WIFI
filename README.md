
Key C++17 Highlights for your learning:

    std::string_view: 
    Traditional strings create a copy on the "Heap." On microcontrollers, 
    too many heap copies cause memory fragmentation. string_view is just a "window" into 
    existing memory—very fast and memory-safe.

    auto: 
    Instead of typing unsigned int or uint32_t, auto lets the compiler figure it out.
    It prevents "Type Mismatch" bugs.

    Namespace: 
    Notice how Config::LedPin is cleaner than just LED_PIN. 
    It tells other engineers that this is a configuration setting, not a temporary variable.


    By me:
    I do see the IP address. Correct me if I'm wrong. The loop(), setup() and initWiFi() 
    should be considered as Tasks, since we are running RTOS. 
    But we did not assign those Tasks to any core. Should we?

Gemini said 
    That is a high-level architectural question, Vlad! You are thinking like a true RTOS engineer.
    The short answer is: They are already running inside a task, but the "Arduino" way hides 
    the details from you.

Here is how the ESP32-S3 handles this under the hood:

1.  Where do setup() and loop() live?
    When you use the Arduino framework on an ESP32, the system automatically creates a task 
    called the "Arduino Loop Task."
    Core Assignment: 
        By default, this task is pinned to Core 1.
    Execution: 
            1. The task starts and calls your setup() function once.
            2. Then, it enters an infinite loop that calls your loop() function repeatedly.
            3. Because you called vTaskDelete(NULL) at the end of your setup(), you effectively 
               "killed" the Arduino Loop Task. This is why we don't care about assigning loop() 
               to a core—it literally ceases to exist before it ever starts.

2. What about initWiFi()?
   Since initWiFi() is called inside setup(), it "borrows" the context of the Arduino Loop Task.
   Current Core: 
        It is running on Core 1.
   Blocking:
        While initWiFi() is looping (while (status != WL_CONNECTED)), it is blocking Core 1 
        from doing anything else in that specific task. However, since you created your 
        heartbeatTask on Core 0, your LED keeps blinking perfectly while the WiFi connects. 
        This is the power of your dual-core setup!   

3. Should we assign them to a core?
   For setup(), you don't need to. The system does it for you. However, for a "Senior" implementation, 
   we usually care about where the WiFi Stack itself is running. 
   The WiFi Stack:
        On the ESP32, the internal WiFi and TCP/IP stack (LwIP) usually runs on Core 0.
   Your Strategy:
         By putting your panicTask (critical real-time) on Core 1 and your heartbeatTask on Core 0, 
         you have made a smart choice. You've isolated your time-critical interrupt logic (Core 1) 
         away from the heavy lifting of the WiFi/Network processing (Core 0).
