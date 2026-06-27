#pragma once
#include <Arduino.h>
#include <WiFi.h>         
#include "config.h"
#include "shared_state.h"

/*
 * ============================================================================
 * led_control.h — macchina a stati del LED onboard GPIO25.
 * ============================================================================
 */

enum LedPattern {
    LED_OFF,    
    LED_DIM,    
    LED_FADE,   
    LED_SOLID,  
    LED_PULSE   
};

// Configurazione compatibile con la versione core vecchia (0.0.7)
inline void initLed() {
    ledcSetup(LED_LEDC_CHANNEL, LED_LEDC_FREQ, LED_LEDC_RESOLUTION);
    ledcAttachPin(LED_BUILTIN_PIN, LED_LEDC_CHANNEL);
    ledcWrite(LED_LEDC_CHANNEL, 0);  
}

inline LedPattern getLedPattern() {
    // Override manuale da web: 1=forza spento, 2=forza acceso
    if (ledOverride == 1) return LED_OFF;
    if (ledOverride == 2) return LED_SOLID;

    // Logica automatica (ledOverride == 0)
    if (batLowWarning)                         return LED_DIM;
    if (wifiClients > 0)                       return LED_FADE;
    if (batCharging && batPercent >= 99.0f)    return LED_SOLID;
    if (batCharging && batPercent >= 0.0f)     return LED_PULSE;
    return LED_OFF;
}

inline void ledPatternUpdate(LedPattern pattern) {
    static LedPattern prevPattern = LED_OFF;
    static unsigned long timer     = 0;      
    static bool          pulseHigh = false;  
    static unsigned long fadeStart = 0;      

    if (pattern != prevPattern) {
        prevPattern = pattern;
        timer       = millis();
        pulseHigh   = false;
        fadeStart   = millis();

        switch (pattern) {
            case LED_OFF:   ledcWrite(LED_LEDC_CHANNEL, 0);                  break;
            case LED_DIM:   ledcWrite(LED_LEDC_CHANNEL, LED_DIM_BRIGHTNESS); break;
            case LED_SOLID: ledcWrite(LED_LEDC_CHANNEL, 255);                break;
            default: break;  
        }
    }

    switch (pattern) {
        case LED_OFF:
        case LED_DIM:
        case LED_SOLID:
            break;

        case LED_FADE: {
            unsigned long elapsed = (millis() - fadeStart) % (unsigned long)LED_FADE_DURATION_MS;
            int duty = 255 - (int)(255.0f * (float)elapsed / (float)LED_FADE_DURATION_MS);
            if (duty < 0) duty = 0;
            ledcWrite(LED_LEDC_CHANNEL, (uint32_t)duty);
            break;
        }

        case LED_PULSE: {
            int pct = (batPercent >= 0.0f) ? (int)batPercent : 0;
            if (pct > 99) pct = 99;
            unsigned long offDur = (unsigned long)map(pct, 0, 99, LED_CHARGING_OFF_MAX, LED_CHARGING_OFF_MIN);
            unsigned long elapsed = millis() - timer;

            if (!pulseHigh) {
                if (elapsed >= offDur) {
                    pulseHigh = true;
                    timer     = millis();
                    ledcWrite(LED_LEDC_CHANNEL, 255);
                }
            } else {
                if (elapsed >= (unsigned long)LED_CHARGING_ON_MS) {
                    pulseHigh = false;
                    timer     = millis();
                    ledcWrite(LED_LEDC_CHANNEL, 0);
                }
            }
            break;
        }
    }
}
