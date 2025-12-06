#ifndef JAR_CONTROLLER_H
#define JAR_CONTROLLER_H

#include <Arduino.h>
#include <lvgl.h>
#include "ErrorManager.h"





class JarController {
private:
    int jarID;
    int setRPM;
    int setDuration; // Duration in SECONDS
    bool motorOn;
    bool settingsSaved;
    
    unsigned long motorStartMillis;
    unsigned long lastMotorCheckMillis;
    
    int motorEnablePin;
    int pwmChannel;
    
    // Error tracking
    ErrorManager* errorManager;
    int consecutiveFailures;
    
    // Home page UI objects (INDIVIDUAL for each jar)
    lv_obj_t *ui_home_countdown_label;
    lv_obj_t *ui_home_state_label;
    
    // Info page UI objects (SHARED)
    lv_obj_t *ui_rpm_textarea;
    lv_obj_t *ui_duration_textarea;
    lv_obj_t *ui_save_button;
    lv_obj_t *ui_start_button;
    lv_obj_t *ui_stop_button;
    
    lv_obj_t *ui_rpm_reading_label;
    lv_obj_t *ui_timeleft_label;
    
    // Helper methods
    bool checkMotorHealth();
    bool validateSettings();
    void applyMotorSettings();
    
public:
    JarController(int id, int enablePin, int channel);
    
    void begin();
    void setErrorManager(ErrorManager* errMgr) { errorManager = errMgr; }
    
    // Motor Control
    bool startMotor();
    void stopMotor(bool isError = false);
    void updateMotor();
    
    // Settings Management
    void setRPMValue(int rpm);
    void setDurationValue(int duration);
    int getRPM() const { return setRPM; }
    int getDuration() const { return setDuration; }
    bool isMotorRunning() const { return motorOn; }
    bool areSettingsSaved() const { return settingsSaved; }
    unsigned long getRemainingTime() const;
    unsigned long getElapsedTime() const;
    
    // Increment/Decrement
    void incrementRPM(int step = 5);
    void decrementRPM(int step = 5);
    void incrementDuration(int step = 10);
    void decrementDuration(int step = 10);
    
    // Save/Load
    bool saveSettings(const char *filepath);
    
    // UI Binding
    void bindInfoPageUI(lv_obj_t *rpm_ta, lv_obj_t *dur_ta, lv_obj_t *save_btn, lv_obj_t *start_btn, lv_obj_t *stop_btn);
    
    void bindHomePageUI(lv_obj_t *home_countdown, lv_obj_t *home_state);
    
    // UI Update Functions
    void updateUIFields();
    void updateHomePageUI();
    void updateButtonStates(bool running);
    
    void onTimerTick();
    void markUnsaved();
    
    // Error status
    bool hasError() const { return consecutiveFailures > 0; }
};

#endif