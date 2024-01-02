#include "Arduino.h"
#include "esp32-hal-cpu.h"
#include "display.h"
#include "tinywatch.h"
#include "tw_widgets/tw_widget.h"

Display display;

// Peripherals
#include "peripherals/buzzer.h"
#include "peripherals/battery.h"
#include "peripherals/imu.h"

// Faces
#include "tw_faces/face_Boot.h"
#include "tw_faces/face_System.h"
#include "tw_faces/face_Watch.h"
#include "tw_faces/face_IMU.h"
#include "tw_faces/face_Compass.h"
#include "tw_faces/face_Settings.h"
#include "tw_faces/face_Notifications.h"
#include "tw_faces/face_WatchSettings.h"
#include "tw_faces/face_Microphone.h"
#include "tw_faces/face_BatteryEmpty.h"

// Widgets
#include "tw_widgets/widget_Battery.h"
#include "tw_widgets/widget_ESP32.h"
#include "tw_widgets/widget_Wifi.h"
#include "tw_widgets/widget_Message.h"
#include "tw_widgets/widget_ActivityRing.h"
// #include "tw_widgets/widget_OpenWeather.h"

// Controls
#include "tw_controls/control_Toggle.h"
#include "tw_controls/control_Button.h"
#include "tw_controls/control_Value.h"
#include "tw_controls/control_ValueSlider.h"
#include "tw_controls/control_Label.h"

// Other
#include "settings/settings.h"
#include "bitmaps/bitmaps_general.h"
#include "fonts/RobotoMono_Regular_All.h"

#define TFT_CS 16
#define TFT_RST 17
#define TFT_DC 15
#define TFT_LED 13

// touch
#define TP_SDA 5
#define TP_SCL 10
#define TP_RST 12
#define TP_IRQ 11

// Managing the backlight is done in a seperate thread
static void process_backlight(void *param);
static TaskHandle_t backlightTaskHandle;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas[] = {TFT_eSprite(&tft), TFT_eSprite(&tft)};
TFT_eSprite loading_screen_canvas = TFT_eSprite(&tft);


// Touch is initialised on I2C bus 2, using IO5 for SDA, and IO10 for SCL
cst816t touchpad(TP_RST, TP_IRQ);

String dirNames[7] = {"UP", "RIGHT", "DOWN", "LEFT", "CLICK", "CLICK_DBL", "CLICK_LONG"};

unsigned long last_finger_move = 0;
unsigned long last_touch = 0;
unsigned long dbl_touch = 0;
unsigned long drag_rate = 0;

int16_t deltaX = 0, deltaY = 0;
int16_t moved_x = 0, moved_y = 0;
int16_t startX = 0, startY = 0;
uint touchTime = 0;
bool isTouched = false;

void Display::init_screen()
{
	if (touchpad.begin(mode_change))
	{
		info_print("Touch started\nVersion: ");
		info_println(touchpad.version());
	}
	tft.init();
	tft.setRotation(settings.config.flipped ? 2 : 0);
	tft.setPivot(120, 140);
	tft.fillScreen(TFT_BLACK);

	for (int c = 0; c < 2; c++)
	{
		canvas[c].setFreeFont(RobotoMono_Regular[3]);
		canvas[c].setSwapBytes(true);
		canvas[c].createSprite(240, 280);
		canvas[c].setRotation(0);
		canvas[c].setTextDatum(4);
	}

	loading_screen_canvas.createSprite(64, 64);

	xTaskCreate(process_backlight, "backlight", configMINIMAL_STACK_SIZE * 3, nullptr, 3, &backlightTaskHandle);
}

void Display::kill_backlight_task()
{
	info_println("Killing backlight RTOS task!");
	vTaskDelete(backlightTaskHandle);
}

void Display::set_current_face(tw_face *face)
{
	current_face = face;
}

void Display::update_rotation()
{
	tft.setRotation(settings.config.flipped ? 2 : 0);
}

void Display::set_display_state(display_states state, String message)
{
	current_display_state = state;

	// dont ask...  more to come here...
	if (state == LOADING)
	{
		showing_loading_icon = false;
	}
	else
	{
		showing_loading_icon = false;
	}
}

void Display::show_loading_icon()
{
	loading_screen_canvas.createSprite(64, 64);
	loading_screen_canvas.setPivot(32, 32);   
	loading_screen_canvas.pushImage(0, 0, 64, 64, bitmap_loading_icon);
	loading_screen_canvas.pushRotated(loading_screen_rotation, TFT_BLACK);

	loading_screen_canvas.deleteSprite();
	loading_screen_rotation += 30;
}

void Display::force_save()
{
	settings.save(true);
}

void Display::show_watch_from_boot()
{
	current_face = &face_watch;
	current_face->draw(true);

	last_touch = millis();
	dbl_touch = millis();
}

void Display::createFaces(bool was_sleeping)
{
	tft.setSwapBytes(true);
	tft.fillScreen(TFT_BLACK);

	update_rotation();

    // Now create the boot and watch tw_face
    face_boot.add("boot", 0, 80);

	if (!was_sleeping)
	{
		face_boot.draw(true);
		BuzzerUI({ {262, 210}, {1851, 150}, {523, 150} });
	}

	backlight_level = 0;
	set_backlight(0);
	
	WidgetBattery * wBattery = new WidgetBattery();
	wBattery->create("Battery", 210, 7, 40, 40, 1000);

	WidgetActivityRing * wActivity = new WidgetActivityRing();
	wActivity->create("Activity", 40, 218, 40, 40, 1000);

	// Used to show the current CPU frequency on a watch face
	// WidgetESP32 * wESP32 = new WidgetESP32();
	// wESP32->create("ESP32", 50, 5, 32, 24, 500);

	// Used to show the WiFi icon if WiFi is enabled
	// Needs to be refactored once the new WiFi threaded manager is ready
	WidgetWifi * wWifi = new WidgetWifi();
	wWifi->create("Wifi", 30, 7, 40, 40, 1000);

	face_watch.add("Time", 1000);
	face_watch.add_widget(wBattery);
	face_watch.add_widget(wActivity);
	face_watch.add_widget(wWifi);
	// face_watch.add_widget(wESP32);

	// If we were sleeping, show the clock here, before processing everything else.
	// Otherwise the clock will be shown from main.cpp once all boot processing is done.
	// Michael H is going to HATE how this logic is split! 
	if (was_sleeping)
		show_watch_from_boot();
	

	face_imu.add("IMU", 100, 80);
	// face_compass.add("Compass", 100, 80);

	face_microphone.add("FFT", 25, 160);
	face_microphone.set_single_navigation(LEFT, &face_watch);

	face_notifications.add("Messages", 1000, 80);
	face_notifications.set_scrollable(false, true);

	face_system.add("System Info", 0);
	face_system.set_scrollable(false, true);
	face_system.set_single_navigation(UP, &face_notifications);

	face_settings.add("Settings", 0, 80);
	face_settings.set_scrollable(false, true);
	face_settings.set_single_navigation(LEFT, &face_boot);
	

	face_watch.set_single_navigation(LEFT, &face_settings);
	face_watch.set_single_navigation(UP, &face_imu);
	face_watch.set_single_navigation(DOWN, &face_notifications);

	// WidgetOpenWeather * wWeather = new WidgetOpenWeather();
	// wWeather->create("Weather", 10, 180, 90, 90, 5000);
	// face_watch.add_widget(wWeather);

	// example of a notification widget - non functional for now
	// WidgetMessage * wMessage = new WidgetMessage();
	// wMessage->create("Message", 0, 0, 200, 50, 1000);
	// face_notifications.add_widget(wMessage);

		
	ControlToggle *cToggle = new ControlToggle();
	cToggle->create("24 Hour", "OFF", "OK", 30, 60, 80, 30);
	cToggle->set_data(&settings.setting_time_24hour);

	ControlToggle *cToggle2 = new ControlToggle();
	cToggle2->create("Handed", "RIGHT", "LEFT", 130, 60, 80, 30);
	cToggle2->set_data(&settings.setting_left_handed);

	ControlToggle *cToggle3 = new ControlToggle();
	cToggle3->create("Rotation", "NORMAL", "FLIPPED", 30, 130, 80, 30);
	cToggle3->set_data(&settings.setting_flipped);
	cToggle3->set_callback(display.update_rotation);

	ControlLabel *cLabel1 = new ControlLabel();
	cLabel1->create("AUDIO", 120, 180);

	ControlToggle *cToggle4 = new ControlToggle();
	cToggle4->create("UI Sound", "OFF", "ON", 30, 210, 80, 30);
	cToggle4->set_data(&settings.setting_audio_ui);

	ControlToggle *cToggle5 = new ControlToggle();
	cToggle5->create("Alarm", "OFF", "ON", 130, 210, 80, 30);
	cToggle5->set_data(&settings.setting_audio_alarm);

	// ControlButton * cButton1 = new ControlButton();
	// cButton1->create("SAVE", 70, 250, 100, 40);
	// cButton1->set_callback(force_save);

	// ControlValue * cValue1 = new ControlValue();
	// cValue1->create("Click Value", "", "", 120, 250, 200, 50);

	// ControlValueSlider * cValue2 = new ControlValueSlider();
	// cValue2->create("Slide Value", "", "", 120, 340, 200, 50);
	// cValue2->set_scrollable(true, false);

	face_settings.add_control(cToggle);
	face_settings.add_control(cToggle2);
	face_settings.add_control(cToggle3);
	face_settings.add_control(cLabel1);
	face_settings.add_control(cToggle4);
	face_settings.add_control(cToggle5);
	// face_settings.add_control(cValue1);
	// face_settings.add_control(cValue2);
}

void Display::update_boot_face(wifi_states status)
{
	setCpuFrequencyMhz(240);
	current_face = &face_boot;
	face_boot.wifi_connect_status(status);
}

void Display::update_current_face()
{
	current_face->draw(false);
}

void Display::show_low_battery()
{
	current_face = &face_batteryempty;
	update_current_face();
}

void Display::check_navigation()
{
	Directions _dir = NONE; 
	Directions swipe_dir = NONE; 

	if (touchpad.available(settings.config.flipped))
    {
		tinywatch.set_cpu_frequency(current_face->get_cpu_speed(), CPU_CHANGE_HIGH);
		
		if (!isTouched && touchpad.finger_num == 1)
		{
			startX = touchpad.x;
			startY = touchpad.y;
			deltaX = 0;
			deltaY = 0;
			moved_x = touchpad.x;
			moved_y = touchpad.y;
			isTouched = true;
			touchTime = millis();
			dbl_touch = last_touch;
			last_touch = millis();
			drag_rate = millis();
			last_finger_move = millis();

			current_face->drag_begin(startX, startY);

		backlight_level = 0;
		set_backlight(backlight_level);

		// info_println("Start");
		}
		else if (isTouched && touchpad.finger_num == 1)
		{
			deltaX = touchpad.x - startX;
			deltaY = touchpad.y - startY;

			int16_t moved_much_x = touchpad.x - moved_x;
			int16_t moved_much_y = touchpad.y - moved_y;

			moved_x = touchpad.x;
			moved_y = touchpad.y;

			last_touch = millis();

			current_face->drag(deltaX, deltaY, moved_much_x, moved_much_y, touchpad.x, touchpad.y, true);
		}
		else if (isTouched && touchpad.finger_num == 0)
		{
			deltaX = touchpad.x - startX;
			deltaY = touchpad.y - startY;
			int distance = sqrt(pow((touchpad.x-startX),2)+pow((touchpad.y-startY),2));
			touchTime = millis()-touchTime;
			// Directions swipe_dir;

			bool double_click = (last_touch - dbl_touch < 300) && distance < 12;
            // info_println( String(double_click ? "YES" : "NO") + " time: "+String(last_touch - dbl_touch)+" dist: "+String(distance));
            // info_println( "last_touch "+String(last_touch) + ", dbl_touch "+String(dbl_touch));
			int16_t last_dir_x = touchpad.x - moved_x;
			int16_t last_dir_y = touchpad.y - moved_y;

			if (current_face->drag_end(deltaX, deltaY, true, distance, double_click, touchpad.x, touchpad.y, last_dir_x, last_dir_y))
			{
				// switch face to the new one and make it the current face
				int dir = current_face->drag_dir;
				if (current_face->navigation[dir] != nullptr)
				{
					current_face = current_face->navigation[dir];
					current_face->draw(true);
				}
				isTouched = false;
				return;
			}
		}
	}
	else
	{
		isTouched = false;
		tinywatch.set_cpu_frequency(current_face->get_cpu_speed(), CPU_CHANGE_LOW);

		if ( current_face->is_face_cached())
			current_face->reset_cache_status();

		if (backlight_level > 0)
		{
			imu.update();
			if (imu.is_looking_at_face())
			{
				backlight_level=0;
				set_backlight(backlight_level);
				last_touch = millis();
				info_println("IMU backlight level: "+String(backlight_level));
			}
		}
	}

	// Process the backlight timer
	if (millis() - last_touch > get_backlight_period())
	{
		last_touch = millis();
		if (backlight_level < 2)
		{
			backlight_level++;
			set_backlight(backlight_level);
			info_println("Setting backlight level: "+String(backlight_level));
		}
		else if (!tinywatch.vbus_present() || true)
		{
			tinywatch.go_to_sleep();
		}
	}
}

uint Display::get_backlight_period()
{
	// Is 5V present even though we think we are on battery power?
	return tinywatch.vbus_present() ? settings.config.bl_period_vbus : settings.config.bl_period_vbat;
}

void Display::set_backlight(int level)
{	
	if (last_backlight != level)
	{
		last_backlight = level;
		backlight_target_val = tinywatch.vbus_present() ? backlight_settings_vbus[level] : backlight_settings_vbat[level];
	}
}

bool Display::adjust_backlight()
{
	if (backlight_current_val < backlight_target_val - 1)
	{
		uint8_t delta = (backlight_target_val - backlight_current_val) / 2;
		backlight_current_val += delta;
		return true;
	}
	else if (backlight_current_val > backlight_target_val + 1)
	{
		uint8_t delta = (backlight_current_val - backlight_target_val) / 2;
		backlight_current_val -= delta;
		return true;
	}
	else if (backlight_current_val != backlight_target_val)
	{
		backlight_current_val = backlight_target_val;
		return true;
	}
	return false;
}

uint8_t Display::get_current_backlight_val()
{
	return backlight_current_val;
}

display_states Display::get_current_display_state()
{
	return current_display_state;
}

// TODO: Convert backlight values from 0-255 to 0-100%
static void process_backlight(void *param)
{
	info_print("Starting backlight control and loading icon system on core ");
  	info_println(xPortGetCoreID());

	while (true)
	{
		bool update_pwm = display.adjust_backlight();
		bool doing_something = false;

		if (update_pwm)
		{
			doing_something = true;	
			analogWrite(TFT_LED, display.get_current_backlight_val());
		}

		if (display.get_current_display_state() == LOADING)
		{
			doing_something = true;
			display.show_loading_icon();
			vTaskDelay(100);
		}

		if (!doing_something)
			vTaskDelay(10);
	}
}


void Display::fill_arc(uint8_t canvasid, int x, int y, int start_angle, int seg_count, int rx, int ry, int w, unsigned int colour)
{

  byte seg = 6; // Segments are 3 degrees wide = 120 segments for 360 degrees
  byte inc = 6; // Draw segments every 3 degrees, increase to 6 for segmented ring

  // Calculate first pair of coordinates for segment start
  float sx = cos((start_angle - 90) * DEG2RAD);
  float sy = sin((start_angle - 90) * DEG2RAD);
  uint16_t x0 = sx * (rx - w) + x;
  uint16_t y0 = sy * (ry - w) + y;
  uint16_t x1 = sx * rx + x;
  uint16_t y1 = sy * ry + y;

  // Draw colour blocks every inc degrees
  for (int i = start_angle; i < start_angle + seg * seg_count; i += inc) {

    // Calculate pair of coordinates for segment end
    float sx2 = cos((i + seg - 90) * DEG2RAD);
    float sy2 = sin((i + seg - 90) * DEG2RAD);
    int x2 = sx2 * (rx - w) + x;
    int y2 = sy2 * (ry - w) + y;
    int x3 = sx2 * rx + x;
    int y3 = sy2 * ry + y;

    canvas[canvasid].fillTriangle(x0, y0, x1, y1, x2, y2, colour);
    canvas[canvasid].fillTriangle(x1, y1, x2, y2, x3, y3, colour);

    // Copy segment end to segment start for next segment
    x0 = x2;
    y0 = y2;
    x1 = x3;
    y1 = y3;
  }
}