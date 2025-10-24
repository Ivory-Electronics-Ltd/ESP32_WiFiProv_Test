#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiProvisioner.h>

// Advanced example migrated from library examples/advanced/advanced.ino
// Provides API key input handling, factory reset, and persistence.

const int buttonPin = 9; // GPIO for BOOT button (adjust if different on your board)

WiFiProvisioner provisioner({
    "Advanced Wi-Fi Provisioning",   // Access Point Name
    "Welcome to Advanced Provision", // HTML Page Title
    "#15be79",                       // Theme Color
    // SVG Logo
    R"rawliteral(<svg id=\"Icons\" xmlns=\"http://www.w3.org/2000/svg\" width=\"5rem\" height=\"5rem\" viewBox=\"0 0 512 512\"><defs><style>.cls-3{fill:#7a2b13}.cls-4{fill:#8c4735}.cls-5{fill:#a05740}.cls-6{fill:#b76049}.cls-7{fill:#c2aacf}.cls-8{fill:#decee5}.cls-9{fill:#f4e6f4}.cls-10{fill:#fff}.cls-11{fill:#33db92}</style></defs><circle cx=\"256\" cy=\"256\" r=\"255.98\" style=\"fill:#15be79\"/></svg>)rawliteral",
    "Advanced Provisioner",                                                   // Project Title
    "Advanced Setup",                                                         // Sub-title
    "Follow the steps to connect. Obtain your API key from the 'User' page.", // Info
    "All rights reserved © Advanced WiFiProvisioner",                         // Footer
    "The status LED will turn green, indicating a successful connection.",    // Success message
    "This action will erase all stored settings, including API key.",         // Reset confirm
    "API Key",                                                                // Input field label
    8,                                                                        // Input length
    false,                                                                    // SHOW_INPUT_FIELD (runtime overridden in callback)
    true                                                                      // SHOW_RESET_FIELD
});

Preferences preferences;

bool connectToWiFi()
{
    preferences.begin("wifi-provision", true);
    String savedSSID = preferences.getString("ssid", "");
    String savedPassword = preferences.getString("password", "");
    preferences.end();
    if (savedSSID.isEmpty())
    {
        Serial.println("No saved Wi-Fi credentials found.");
        return false;
    }
    Serial.printf("Connecting to saved Wi-Fi: %s\n", savedSSID.c_str());
    if (savedPassword.isEmpty())
        WiFi.begin(savedSSID.c_str());
    else
        WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startTime > 10000)
        {
            Serial.println("Failed to connect to saved Wi-Fi.");
            return false;
        }
        delay(500);
    }
    Serial.printf("Successfully connected to %s\n", savedSSID.c_str());
    return true;
}

void setup()
{
    Serial.begin(9600);
    pinMode(buttonPin, INPUT_PULLUP);

    provisioner
        .onProvision([]()
                     {
        preferences.begin("wifi-provision", true);
        String savedAPIKey = preferences.getString("apikey", "");
        if (!savedAPIKey.isEmpty()) {
          provisioner.getConfig().SHOW_INPUT_FIELD = false;
          Serial.println("API key exists. Input field hidden.");
        } else {
          provisioner.getConfig().SHOW_INPUT_FIELD = true;
          Serial.println("No API key found. Input field shown.");
        }
        preferences.end(); })
        .onInputCheck([](const char *input) -> bool
                      {
        Serial.printf("Validating API Key: %s\n", input);
        return strlen(input) == 8; })
        .onFactoryReset([]()
                        {
        preferences.begin("wifi-provision", false);
        Serial.println("Factory reset triggered! Clearing preferences...");
        preferences.clear();
        preferences.end(); })
        .onSuccess([](const char *ssid, const char *password, const char *input)
                   {
        Serial.printf("Provisioning successful! SSID: %s\n", ssid);
        preferences.begin("wifi-provision", false);
        preferences.putString("ssid", String(ssid));
        if (password) preferences.putString("password", String(password));
        if (input) preferences.putString("apikey", String(input));
        preferences.end();
        Serial.println("Credentials and API key saved."); });

    if (!connectToWiFi())
    {
        provisioner.startProvisioning();
    }
}

void loop()
{
    int buttonState = digitalRead(buttonPin);
    if (buttonState == LOW)
    {
        Serial.println("Button pressed. Starting provisioning...");
        provisioner.startProvisioning();
    }
}
