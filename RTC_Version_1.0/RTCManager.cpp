#include "RTCManager.h"

// Global instance
RTCManager rtcManager;


// Static pointer for callbacks
static RTCManager *instance = nullptr;

RTCManager::RTCManager() 
    : rtcReady(false), calendar_popup(nullptr), 
      calendar_year_roller(nullptr), calendar_month_roller(nullptr),
      calendar_day_roller(nullptr), calendar_hour_roller(nullptr),
      calendar_minute_roller(nullptr), update_timer(nullptr),
      home_time_label(nullptr), settings_button(nullptr),
      errorManager(nullptr) {
    instance = this;
}

bool RTCManager::begin(int sda_pin, int scl_pin) {
    Serial.println("=== Initializing RTC ===");
    
    // Initialize I2C
    Wire.begin(sda_pin, scl_pin);
    
    if (!rtc.begin()) {
        Serial.println("ERROR: RTC DS3231 not found!");
        rtcReady = false;
        if (errorManager) {
            errorManager->logError(ERR_DISPLAY_FAILED, -1, "RTC module not found");
        }
        return false;
    }
    
    rtcReady = true;
    Serial.println("RTC initialized successfully");
    
    // Check if RTC lost power
    if (rtc.lostPower()) {
        Serial.println("RTC lost power, setting default time");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    
    // Print current time
    DateTime now = rtc.now();
    Serial.printf("Current RTC Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second());
    
    // Create update timer (1 second interval)
    update_timer = lv_timer_create(timerCallback, 1000, NULL);
    
    return true;
}

void RTCManager::bindUI(lv_obj_t *home_time, lv_obj_t *settings_btn) {
    home_time_label = home_time;
    settings_button = settings_btn;
    
    Serial.printf("RTC UI bound - Home: %p, Button: %p\n", home_time, settings_btn);
}

void RTCManager::setErrorManager(ErrorManager *em) {
    errorManager = em;
}

DateTime RTCManager::getCurrentTime() {
    if (rtcReady) {
        return rtc.now();
    }
    return DateTime(2025, 1, 1, 0, 0, 0); // Default fallback
}

void RTCManager::setTime(uint16_t year, uint16_t month, uint16_t day,
                         uint16_t hour, uint16_t minute, uint16_t second) {
    if (rtcReady) {
        rtc.adjust(DateTime(year, month, day, hour, minute, second));
        Serial.printf("RTC Time Set: %04d-%02d-%02d %02d:%02d:%02d\n",
                     year, month, day, hour, minute, second);
    }
}

/*void RTCManager::updateHomePageTime() {
    if (!rtcReady || !home_time_label) return;
    
    DateTime now = rtc.now();
    
    char timeBuf[32];
    sprintf(timeBuf, "%02d:%02d:%02d\n%04d-%02d-%02d", 
            now.hour(), now.minute(), now.second(),
            now.year(), now.month(), now.day());
    
    lv_label_set_text(home_time_label, timeBuf);
    lv_obj_align(home_time_label, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_text_color(home_time_label, lv_color_white(), 0);
}*/     //changed

void RTCManager::updateHomePageTime() {
    if (!home_time_label) {
        Serial.println("WARNING: home_time_label is NULL!");
        return;
    }
    
    DateTime now;
    if (rtcReady) {
        now = rtc.now();
    } else {
        // Use default time if RTC not ready
        now = DateTime(2025, 1, 1, 0, 0, 0);
    }
    
    char timeBuf[32];
    sprintf(timeBuf, "%02d:%02d:%02d", 
            now.hour(), now.minute(), now.second());
    
    lv_label_set_text(home_time_label, timeBuf);
    
    // Don't force alignment or color - let UI defaults handle it
    Serial.printf("Home time updated: %s\n", timeBuf);
}







void RTCManager::updateSettingsButton() {
    if (!rtcReady || !settings_button) return;
    
    DateTime now = rtc.now();
    
    lv_obj_t *label = lv_obj_get_child(settings_button, 0);
    if (label) {
        char buf[64];
        sprintf(buf, "Date/Time\n%04d-%02d-%02d\n%02d:%02d",
               now.year(), now.month(), now.day(),
               now.hour(), now.minute());
        lv_label_set_text(label, buf);
    }
}

void RTCManager::createCalendarPopup() {
    if (calendar_popup != nullptr) return; // Already open
    
    // Create popup background
    calendar_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(calendar_popup, 420, 300);
    lv_obj_center(calendar_popup);
    lv_obj_set_style_bg_color(calendar_popup, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
    lv_obj_set_style_border_width(calendar_popup, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(calendar_popup, lv_color_hex(0x00A8E8), LV_PART_MAIN);
    lv_obj_set_style_radius(calendar_popup, 10, LV_PART_MAIN);
    
    // Title
    lv_obj_t *title = lv_label_create(calendar_popup);
    lv_label_set_text(title, "Set Date & Time");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    DateTime now = getCurrentTime();
Wire.begin();

    if (rtc.begin()) {
        now = getCurrentTime();
        Serial.println("Using RTC time");
    } else {
        now = DateTime(2025, 1, 1, 12, 0, 0);  // Default time
        Serial.println("Using default time (RTC not connected)");
    }

    int y_offset = 50;
    
    // Year Roller
    lv_obj_t *year_label = lv_label_create(calendar_popup);
    lv_label_set_text(year_label, "Year");
    lv_obj_set_style_text_color(year_label, lv_color_hex(0x00A8E8), 0);
    lv_obj_align(year_label, LV_ALIGN_TOP_LEFT, 25, y_offset);
    
    calendar_year_roller = lv_roller_create(calendar_popup);
    lv_roller_set_options(calendar_year_roller,
        "2020\n2021\n2022\n2023\n2024\n2025\n2026\n2027\n2028\n2029\n"
        "2030\n2031\n2032\n2033\n2034\n2035\n2036\n2037\n2038\n2039\n2040",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(calendar_year_roller, 70, 120);
    lv_obj_align(calendar_year_roller, LV_ALIGN_TOP_LEFT, 15, y_offset + 25);
    lv_roller_set_selected(calendar_year_roller, now.year() - 2020, LV_ANIM_OFF);
    lv_roller_set_visible_row_count(calendar_year_roller, 4);
    
    // Month Roller
    lv_obj_t *month_label = lv_label_create(calendar_popup);
    lv_label_set_text(month_label, "Month");
    lv_obj_set_style_text_color(month_label, lv_color_hex(0x00A8E8), 0);
    lv_obj_align(month_label, LV_ALIGN_TOP_LEFT, 105, y_offset);
    
    calendar_month_roller = lv_roller_create(calendar_popup);
    lv_roller_set_options(calendar_month_roller,
        "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(calendar_month_roller, 60, 120);
    lv_obj_align(calendar_month_roller, LV_ALIGN_TOP_LEFT, 95, y_offset + 25);
    lv_roller_set_selected(calendar_month_roller, now.month() - 1, LV_ANIM_OFF);
    lv_roller_set_visible_row_count(calendar_month_roller, 4);
    
    // Day Roller
    lv_obj_t *day_label = lv_label_create(calendar_popup);
    lv_label_set_text(day_label, "Day");
    lv_obj_set_style_text_color(day_label, lv_color_hex(0x00A8E8), 0);
    lv_obj_align(day_label, LV_ALIGN_TOP_LEFT, 180, y_offset);
    
    calendar_day_roller = lv_roller_create(calendar_popup);
    lv_roller_set_options(calendar_day_roller,
        "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n"
        "11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n"
        "21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(calendar_day_roller, 60, 120);
    lv_obj_align(calendar_day_roller, LV_ALIGN_TOP_LEFT, 170, y_offset + 25);
    lv_roller_set_selected(calendar_day_roller, now.day() - 1, LV_ANIM_OFF);
    lv_roller_set_visible_row_count(calendar_day_roller, 4);
    
    // Hour Roller
    lv_obj_t *hour_label = lv_label_create(calendar_popup);
    lv_label_set_text(hour_label, "Hour");
    lv_obj_set_style_text_color(hour_label, lv_color_hex(0x00A8E8), 0);
    lv_obj_align(hour_label, LV_ALIGN_TOP_LEFT, 255, y_offset);
    
    calendar_hour_roller = lv_roller_create(calendar_popup);
    lv_roller_set_options(calendar_hour_roller,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n"
        "12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(calendar_hour_roller, 60, 120);
    lv_obj_align(calendar_hour_roller, LV_ALIGN_TOP_LEFT, 245, y_offset + 25);
    lv_roller_set_selected(calendar_hour_roller, now.hour(), LV_ANIM_OFF);
    lv_roller_set_visible_row_count(calendar_hour_roller, 4);
    
    // Minute Roller
    lv_obj_t *minute_label = lv_label_create(calendar_popup);
    lv_label_set_text(minute_label, "Min");
    lv_obj_set_style_text_color(minute_label, lv_color_hex(0x00A8E8), 0);
    lv_obj_align(minute_label, LV_ALIGN_TOP_LEFT, 330, y_offset);
    
    calendar_minute_roller = lv_roller_create(calendar_popup);
    lv_roller_set_options(calendar_minute_roller,
        "00\n05\n10\n15\n20\n25\n30\n35\n40\n45\n50\n55",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(calendar_minute_roller, 60, 120);
    lv_obj_align(calendar_minute_roller, LV_ALIGN_TOP_LEFT, 320, y_offset + 25);
    lv_roller_set_selected(calendar_minute_roller, now.minute() / 5, LV_ANIM_OFF);
    lv_roller_set_visible_row_count(calendar_minute_roller, 4);
    
    // Save Button
    lv_obj_t *save_btn = lv_btn_create(calendar_popup);
    lv_obj_set_size(save_btn, 170, 45);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_LEFT, 15, -10);
    lv_obj_set_style_bg_color(save_btn, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    lv_obj_set_style_radius(save_btn, 8, LV_PART_MAIN);
    
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save Time");
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_14, 0);
    lv_obj_center(save_label);
    
   /* lv_obj_add_event_cb(save_btn, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED && instance) {
            uint16_t year = lv_roller_get_selected(instance->calendar_year_roller) + 2020;
            uint16_t month = lv_roller_get_selected(instance->calendar_month_roller) + 1;
            uint16_t day = lv_roller_get_selected(instance->calendar_day_roller) + 1;
            uint16_t hour = lv_roller_get_selected(instance->calendar_hour_roller);
            uint16_t minute = lv_roller_get_selected(instance->calendar_minute_roller) * 5;
            
            instance->setTime(year, month, day, hour, minute, 0);
            instance->updateHomePageTime();
            instance->updateSettingsButton();
            
            lv_obj_t *btn = (lv_obj_t*)lv_event_get_target(e);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x00FF00), LV_PART_MAIN);
            
            lv_timer_t *close_timer = lv_timer_create([](lv_timer_t *timer) {
                if (instance) {
                    instance->closeCalendarPopup();
                }
                lv_timer_del(timer);
            }, 500, NULL);
        }
    }, LV_EVENT_CLICKED, NULL);*/  //chnaged

    // Save Button callback - REPLACE THE EXISTING ONE
lv_obj_add_event_cb(save_btn, [](lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && instance) {
        uint16_t year = lv_roller_get_selected(instance->calendar_year_roller) + 2020;
        uint16_t month = lv_roller_get_selected(instance->calendar_month_roller) + 1;
        uint16_t day = lv_roller_get_selected(instance->calendar_day_roller) + 1;
        uint16_t hour = lv_roller_get_selected(instance->calendar_hour_roller);
        uint16_t minute = lv_roller_get_selected(instance->calendar_minute_roller) * 5;
        
        // Set the RTC time
        instance->setTime(year, month, day, hour, minute, 0);
        
        Serial.println("=== Time saved, updating displays ===");
        
        // Update home page time immediately
        instance->updateHomePageTime();
        
        // Update settings button
        instance->updateSettingsButton();
        
        // Visual feedback - green button
        lv_obj_t *btn = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x00FF00), LV_PART_MAIN);
        
        // Close popup after 500ms
        lv_timer_t *close_timer = lv_timer_create([](lv_timer_t *timer) {
            if (instance) {
                instance->closeCalendarPopup();
                
                // Force LVGL to refresh the display
                lv_obj_invalidate(lv_scr_act());
            }
            lv_timer_del(timer);
        }, 500, NULL);
    }
}, LV_EVENT_CLICKED, NULL);




    // Cancel Button
    lv_obj_t *cancel_btn = lv_btn_create(calendar_popup);
    lv_obj_set_size(cancel_btn, 170, 45);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -15, -10);
    lv_obj_set_style_bg_color(cancel_btn, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 8, LV_PART_MAIN);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_14, 0);
    lv_obj_center(cancel_label);
    
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED && instance) {
            instance->closeCalendarPopup();
        }
    }, LV_EVENT_CLICKED, NULL);
    
    Serial.println("Calendar popup created");
}

void RTCManager::closeCalendarPopup() {
    if (calendar_popup) {
        lv_obj_del(calendar_popup);
        calendar_popup = nullptr;
        calendar_year_roller = nullptr;
        calendar_month_roller = nullptr;
        calendar_day_roller = nullptr;
        calendar_hour_roller = nullptr;
        calendar_minute_roller = nullptr;
    }
}


void RTCManager::timerCallback(lv_timer_t *timer) {
    if (instance) {
        instance->updateHomePageTime();
    }
}





void RTCManager::buttonCallback(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("=== RTC Button Clicked ===");
        
        if (instance) {
            Serial.println("Instance exists");
            Serial.printf("rtcReady: %s\n", instance->rtcReady ? "true" : "false");
            
            // Always show popup, even if RTC not ready (for testing)
            Serial.println("Opening calendar popup...");
            instance->showCalendarPopup();
            Serial.println("Calendar popup opened");
    }
}}

void RTCManager::showCalendarPopup() {
    Serial.println("=== showCalendarPopup() called ===");
    
    Wire.begin();

  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    while (1);
  }
    
    
  Serial.println("RTC ready!");
  Serial.println("Use command: SETTIME hh mm ss dd mm yyyy");
  Serial.println("Example: SETTIME 14 30 0 1 12 2025");
    
    Serial.println("Creating calendar popup...");
    createCalendarPopup();
    Serial.println("Calendar popup created");
}
