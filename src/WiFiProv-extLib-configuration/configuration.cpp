#include <Arduino.h>
#include <WiFiProvisioner.h>

// Configuration example migrated from library examples/configuration/configuration.ino
// Demonstrates custom configuration object with input validation and reset callback.

WiFiProvisioner::Config customCfg(
    "Custom Wi-Fi Provisioning",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        // Access Point Name
    "Welcome to Custom Provision",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      // HTML Page Title
    "#0989d8",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          // Theme Color
    R"rawliteral(<svg id=\"Icons\" xmlns=\"http://www.w3.org/2000/svg\" width=\"5rem\" height=\"5rem\" viewBox=\"0 0 512 512\"><defs><style>.cls-3{fill:#6b250c}.cls-4{fill:#7a2b13}.cls-5{fill:#dd8c29}.cls-6{fill:#efb732}.cls-7{fill:#962a11}.cls-8{fill:#f9eaa5}.cls-9{fill:#fad98f}.cls-10{fill:#f39e22}.cls-11{fill:#f9c744}.cls-12{fill:#c44a1a}.cls-13{fill:#decee5}.cls-14{fill:#f4e6f4}.cls-16{fill:#fff}.cls-17{fill:#2e3140}.cls-18{fill:#334353}.cls-20{fill:#ccfdff}</style></defs><circle cx=\"256\" cy=\"256\" r=\"255.98\" style=\"fill:#0989d8\"/></svg>)rawliteral", // SVG Logo
    "Custom Provisioner",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               // Project Title
    "Custom Setup",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     // Project Sub-title
    "Follow the steps to connect. Obtain your API key from the 'User' page.",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           // Project Information
    "All rights reserved © Custom WiFiProvisioner",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     // Footer Text
    "The device is now visible in your online dashboard.",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              // Success Message
    "This action will erase all stored settings, including API key.",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   // Reset Confirmation Text
    "API Key",                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          // Input Field Text
    4,                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  // Input Field Length
    true,                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               // Show Input Field
    true                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                // Show Reset Field
);

WiFiProvisioner provisioner(customCfg);

void setup()
{
    Serial.begin(115200);

    provisioner
        .onProvision([]()
                     { Serial.println("Provisioning started."); })
        .onInputCheck([](const char *input) -> bool
                      {
        Serial.printf("Checking if input code equals to 1234: %s\n", input);
        return strcmp(input, "1234") == 0; })
        .onSuccess([](const char *ssid, const char *password, const char *input)
                   {
        Serial.printf("Connected to SSID: %s\n", ssid);
        if (password) Serial.printf("Password: %s\n", password);
        if (input) Serial.printf("Input: %s\n", input);
        Serial.println("Provisioning completed successfully!"); })
        .onFactoryReset([]()
                        { Serial.println("Factory reset triggered!"); });

    provisioner.startProvisioning();
}

void loop() { delay(100); }
