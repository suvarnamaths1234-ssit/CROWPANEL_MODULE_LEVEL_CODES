#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <lvgl.h>
#include "ErrorManager.h"

class RTCManager {
private:
    RTC_DS3231 rtc;
    bool rtcReady;
    
    // Calendar popup UI elements
    lv_obj_t *calendar_popup;
    lv_obj_t *calendar_year_roller;
    lv_obj_t *calendar_month_roller;
    lv_obj_t *calendar_day_roller;
    lv_obj_t *calendar_hour_roller;
    lv_obj_t *calendar_minute_roller;
    
    lv_timer_t *update_timer;
    
    // UI object pointers
    lv_obj_t *home_time_label;
    lv_obj_t *settings_button;
    
    ErrorManager *errorManager;
    
    // Private methods
    void createCalendarPopup();
    void closeCalendarPopup();
    
public:
    RTCManager();
    
    // Initialization
    bool begin(int sda_pin = 21, int scl_pin = 22);
    
    // Bind UI objects
    void bindUI(lv_obj_t *home_time, lv_obj_t *settings_btn);
    void setErrorManager(ErrorManager *em);
    
    // Time operations
    DateTime getCurrentTime();
    void setTime(uint16_t year, uint16_t month, uint16_t day, 
                 uint16_t hour, uint16_t minute, uint16_t second);
    
    // UI updates
    void updateHomePageTime();
    void updateSettingsButton();
    
    // Calendar popup
    void showCalendarPopup();
    
    // Status
    bool isReady() { return rtcReady; }
    
    // Static callback for LVGL timer
    static void timerCallback(lv_timer_t *timer);
    
    // Button callback
    static void buttonCallback(lv_event_t *e);
};

extern RTCManager rtcManager;

#endif // RTC_MANAGER_H