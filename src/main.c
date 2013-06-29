#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"
#include "stdlib.h"
#include "config.h"
#include "http.h"
#include "util.h"
#include "link_monitor.h"

#define MY_UUID { 0x0D, 0xB7, 0x36, 0xD9, 0x13, 0x0A, 0x40, 0x6B, 0x9F, 0xA6, 0xB1, 0x18, 0x35, 0xC9, 0x8E, 0x02 }
PBL_APP_INFO(MY_UUID,
             "Hoelaat_httpebble", "Thiso6",
             1, 0, /* App version */
             DEFAULT_MENU_ICON,
             APP_INFO_WATCH_FACE);

static Window window;
static GFont font_small;
static GFont font_big;
static InverterLayer inverter;
static PropertyAnimation inverter_anim;

//91
TextLayer wbox_layer;   			/* white box layer */
TextLayer text_temperature_layer;
TextLayer calls_layer;   			/* layer for Phone Calls info */
TextLayer sms_layer;   				/* layer for SMS info */
static int our_latitude, our_longitude, our_timezone = 99;
static bool located = false;
static bool temperature_set = false;
GFont font_temperature;        		/* font for Temperature */

#define TOTAL_WEATHER_IMAGES 1
BmpContainer weather_images[TOTAL_WEATHER_IMAGES];

const int WEATHER_IMAGE_RESOURCE_IDS[] = {
    RESOURCE_ID_IMAGE_CLEAR_DAY,
	RESOURCE_ID_IMAGE_CLEAR_NIGHT,
	RESOURCE_ID_IMAGE_RAIN,
	RESOURCE_ID_IMAGE_SNOW,
	RESOURCE_ID_IMAGE_SLEET,
	RESOURCE_ID_IMAGE_WIND,
	RESOURCE_ID_IMAGE_FOG,
	RESOURCE_ID_IMAGE_CLOUDY,
	RESOURCE_ID_IMAGE_PARTLY_CLOUDY_DAY,
	RESOURCE_ID_IMAGE_PARTLY_CLOUDY_NIGHT,
	RESOURCE_ID_IMAGE_NO_WEATHER
};


#define TOTAL_HTTPEBBLE_IMAGES 2
BmpContainer httpebble_images[TOTAL_HTTPEBBLE_IMAGES];

const int HTTPEBBLE_IMAGE_RESOURCE_IDS[] = {
	RESOURCE_ID_IMAGE_PHONECALLS,
	RESOURCE_ID_IMAGE_SMS
};
void set_container_image(BmpContainer *bmp_container, const int resource_id, GPoint origin) {
  layer_remove_from_parent(&bmp_container->layer.layer);
  bmp_deinit_container(bmp_container);

  bmp_init_container(resource_id, bmp_container);

  GRect frame = layer_get_frame(&bmp_container->layer.layer);
  frame.origin.x = origin.x;
  frame.origin.y = origin.y;
  layer_set_frame(&bmp_container->layer.layer, frame);

  layer_add_child(&window.layer, &bmp_container->layer.layer);
}

//
typedef struct
{
        TextLayer layer;
        PropertyAnimation anim;
        const char * text;
        const char * old_text;
} word_t;

static word_t first_word;
static word_t first_word_between;
static word_t second_word;
static word_t second_word_between;
static word_t third_word;


static const char *hours[] = {
	"twaalf",
	"een",
	"twee",
	"drie",
	"vier",
	"vijf",
	"zes",
	"zeven",
	"acht",
	"negen",
	"tien",
	"elf",
	"twaalf"
};
static const char *minutes[] = {
	"",
	"",
	"",
	"",
	"",
	"vijf",
	"",
	"",
	"",
	"",
	"tien",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"twintig"
};

//91
void request_weather();

void display_counters(TextLayer *dataLayer, struct Data d, int infoType) {
	
	static char temp_text[5];
	
	if(d.link_status != LinkStatusOK){
		memcpy(temp_text, "?", 1);
	}
	else {	
		if (infoType == 1) {
			if(d.missed) {
				memcpy(temp_text, itoa(d.missed), 4);
			}
			else {
				memcpy(temp_text, itoa(0), 4);
			}
		}
		else if(infoType == 2) {
			if(d.unread) {
				memcpy(temp_text, itoa(d.unread), 4);
			}
			else {
				memcpy(temp_text, itoa(0), 4);
			}
		}
	}
	
	text_layer_set_text(dataLayer, temp_text);
}

void failed(int32_t cookie, int http_status, void* context) {
	
	if((cookie == 0 || cookie == WEATHER_HTTP_COOKIE) && !temperature_set) {
		set_container_image(&weather_images[0], WEATHER_IMAGE_RESOURCE_IDS[10], GPoint(30, 117));
		text_layer_set_text(&text_temperature_layer, "---°");
	}
	
	//link_monitor_handle_failure(http_status);
	
	//Re-request the location and subsequently weather on next minute tick
	//located = false;
}

void success(int32_t cookie, int http_status, DictionaryIterator* received, void* context) {
	
	if(cookie != WEATHER_HTTP_COOKIE) return;
	
	Tuple* icon_tuple = dict_find(received, WEATHER_KEY_ICON);
	if(icon_tuple) {
		int icon = icon_tuple->value->int8;
		if(icon >= 0 && icon < 10) {
			set_container_image(&weather_images[0], WEATHER_IMAGE_RESOURCE_IDS[icon], GPoint(30, 117));  // ---------- Weather Image
		} else {
			set_container_image(&weather_images[0], WEATHER_IMAGE_RESOURCE_IDS[10], GPoint(30, 117));
		}
	}
	
	Tuple* temperature_tuple = dict_find(received, WEATHER_KEY_TEMPERATURE);
	if(temperature_tuple) {
		
		static char temp_text[5];
		memcpy(temp_text, itoa(temperature_tuple->value->int16), 4);
		int degree_pos = strlen(temp_text);
		memcpy(&temp_text[degree_pos], "°", 3);
		text_layer_set_text(&text_temperature_layer, temp_text);
		temperature_set = true;
	}
	
	link_monitor_handle_success(&data);
	//display_counters(&calls_layer, data, 1);
	//display_counters(&sms_layer, data, 2);
}

void location(float latitude, float longitude, float altitude, float accuracy, void* context) {
	// Fix the floats
	our_latitude = latitude * 10000;
	our_longitude = longitude * 10000;
	located = true;
	request_weather();
}

void reconnect(void* context) {
	located = false;
	request_weather();
}

bool read_state_data(DictionaryIterator* received, struct Data* d){
	(void)d;
	bool has_data = false;
	Tuple* tuple = dict_read_first(received);
	if(!tuple) return false;
	do {
		switch(tuple->key) {
	  		case TUPLE_MISSED_CALLS:
				d->missed = tuple->value->uint8;
				
				static char temp_calls[5];
				memcpy(temp_calls, itoa(tuple->value->uint8), 4);
				text_layer_set_text(&calls_layer, temp_calls);
				
				has_data = true;
				break;
			case TUPLE_UNREAD_SMS:
				d->unread = tuple->value->uint8;
			
				static char temp_sms[5];
				memcpy(temp_sms, itoa(tuple->value->uint8), 4);
				text_layer_set_text(&sms_layer, temp_sms);
			
				has_data = true;
				break;
		}
	}
	while((tuple = dict_read_next(received)));
	return has_data;
}

void app_received_msg(DictionaryIterator* received, void* context) {	
	link_monitor_handle_success(&data);
	if(read_state_data(received, &data)) 
	{
		//display_counters(&calls_layer, data, 1);
		//display_counters(&sms_layer, data, 2);
		if(!located)
		{
			request_weather();
		}
	}
}
static void app_send_failed(DictionaryIterator* failed, AppMessageResult reason, void* context) {
	link_monitor_handle_failure(reason, &data);
	//display_counters(&calls_layer, data, 1);
	//display_counters(&sms_layer, data, 2);
}

bool register_callbacks() {
	if (callbacks_registered) {
		if (app_message_deregister_callbacks(&app_callbacks) == APP_MSG_OK)
			callbacks_registered = false;
	}
	if (!callbacks_registered) {
		app_callbacks = (AppMessageCallbacksNode){
			.callbacks = { .in_received = app_received_msg, .out_failed = app_send_failed} };
		if (app_message_register_callbacks(&app_callbacks) == APP_MSG_OK) {
      callbacks_registered = true;
      }
	}
	return callbacks_registered;
}

void receivedtime(int32_t utc_offset_seconds, bool is_dst, uint32_t unixtime, const char* tz_name, void* context)
{	
	our_timezone = (utc_offset_seconds / 3600);
	if (is_dst)
	{
		our_timezone--;
	}
	
}


//


void
text_layer_setup(
        Window * window,
        TextLayer * layer,
        GRect frame,
        GFont font
)
{
        text_layer_init(layer, frame);
        text_layer_set_text(layer, "");
        text_layer_set_text_color(layer, GColorWhite);
        text_layer_set_background_color(layer, GColorClear);
     	text_layer_set_text_alignment(layer, GTextAlignmentCenter);
        text_layer_set_font(layer, font);
        layer_add_child(&window->layer, &layer->layer);
}

static const char *
min_string(
        int i
)
{
        return minutes[i];
}


static const char *
hour_string(
        int h
)
{
        if (h < 12)
                return hours[h];
        else
                return hours[h - 12];
}


static void
update_word(
        word_t * const word
)
{
        text_layer_set_text(&word->layer, word->text);
        if (word->text != word->old_text)
                animation_schedule(&word->anim.animation);
}



static void
nederlands_format(
        int h,
        int m
)
{
		first_word.text = "";
		first_word_between.text = "";
		second_word.text = "";
		second_word_between.text = "";
		third_word.text = "";
	
		int unrounded = m;
		float temp_m = m;
		temp_m = temp_m / 5;
		m = temp_m + 0.5;
		m = m * 5;
	
		if(m == 0 || m == 60) {
			if(m == 60) {
				h++;
			}
			first_word_between.text = hour_string(h);
			second_word_between.text = "uur";
		} else if(m == 30) {
			first_word_between.text = "half";
			second_word_between.text = hour_string(h + 1);
		} else if(m == 15) {
			first_word.text = "kwart";
			second_word.text = "over";
			third_word.text = hour_string(h);
		} else if(m == 45) {
			first_word.text = "kwart";
			second_word.text = "voor";
			third_word.text = hour_string(h + 1);
		} else if(m > 30) {
			if(m < 40) {
				first_word.text = min_string(m - 30);
				second_word.text = "over half";
				third_word.text = hour_string(h + 1);
			} else {
				first_word.text = min_string(60 - m);
				second_word.text = "voor";
				third_word.text = hour_string(h + 1);
			}
		} else {
			if(m > 20) {
				first_word.text = min_string(30 - m);
				second_word.text = "voor half";
				third_word.text = hour_string(h + 1);
			} else {
				first_word.text = min_string(m);
				second_word.text = "over";
				third_word.text = hour_string(h);
			}
		}
	
		GRect frame;
		GRect frame_right;
		switch(unrounded - m) {
			case -2:
				frame = GRect(108, 166, 36, 1);
				frame_right = frame;
				frame_right.origin.x = 0;
				break;
			case -1:
				frame = GRect(0, 166, 36, 1);
				frame_right = frame;
				frame_right.origin.x = 36;
				break;
			case 0:
				frame = GRect(36, 166, 36, 1);
				frame_right = frame;
				frame_right.origin.x = 63;
				frame_right.size.w = 18;
				break;
			case 1:
				frame = GRect(63, 166, 36, 1);
				frame_right = frame;
				frame_right.origin.x = 72;
				frame_right.size.w = 36;
				break;
			case 2:
				frame = GRect(72, 166, 36, 1);
				frame_right = frame;
				frame_right.origin.x = 108;
				break;
		}
		property_animation_init_layer_frame(&inverter_anim, (Layer *)&inverter, &frame, &frame_right);
		animation_schedule(&inverter_anim.animation);

}

/** Called once per minute */
static void
handle_tick(
        AppContextRef ctx,
        PebbleTickEvent * const t
)
{
        (void) ctx;
        const PblTm * const ptm = t->tick_time;

        int hour = ptm->tm_hour;
        int min = ptm->tm_min;
	
		first_word.old_text = first_word.text;
		first_word_between.old_text = first_word_between.text;
		second_word.old_text = second_word.text;
        second_word_between.old_text = second_word_between.text;
        third_word.old_text = third_word.text;

        nederlands_format(hour,  min);

        update_word(&first_word);
        update_word(&first_word_between);
        update_word(&second_word);
        update_word(&second_word_between);
        update_word(&third_word);
	
	//91
	if(!located || !(t->tick_time->tm_min % 15))
	{
		// Every 15 minutes, request updated weather
		http_location_request();
	}
	
	// Every 15 minutes, request updated time
	http_time_request();


	if(!(t->tick_time->tm_min % 2) || data.link_status == LinkStatusUnknown) link_monitor_ping();
//	
	
	
}


static void
text_layer(
        word_t * word,
        GRect frame,
        GFont font
)
{
        text_layer_setup(&window, &word->layer, frame, font);

        GRect frame_right = frame;
        frame_right.origin.x = 150;

        property_animation_init_layer_frame(
                &word->anim,
                &word->layer.layer,
                &frame_right,
                &frame
        );

        animation_set_duration(&word->anim.animation, 500);
        animation_set_curve(&word->anim.animation, AnimationCurveEaseIn);
	
		animation_set_duration(&inverter_anim.animation, 2000);
        animation_set_curve(&inverter_anim.animation, AnimationCurveEaseIn);
}


static void
handle_init(
        AppContextRef ctx
)
{
        (void) ctx;

        window_init(&window, "Main");
        window_stack_push(&window, true);
        window_set_background_color(&window, GColorBlack);

        resource_init_current_app(&RESOURCES);

        font_big = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_40));
        font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_30));

        text_layer(&first_word, GRect(0, 0, 143, 48), font_big);
        text_layer(&second_word, GRect(0, 41, 143, 42), font_small);
        text_layer(&third_word, GRect(0, 66, 143, 48), font_big);
	
	    text_layer(&first_word_between, GRect(0, 10, 143, 48), font_big);
	    text_layer(&second_word_between, GRect(0, 55, 143, 48), font_big);
	
		inverter_layer_init(&inverter, GRect(0, 166, 36, 1));
		layer_add_child(&window.layer, (Layer *)&inverter);

	//91
	// Load Fonts
    font_temperature = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FUTURA_40));
	
		// WHITE BOX layer 
	text_layer_init(&wbox_layer, window.layer.frame);
	text_layer_set_text_color(&wbox_layer, GColorWhite);
	text_layer_set_background_color(&wbox_layer, GColorWhite);
	layer_set_frame(&wbox_layer.layer,  GRect(0, 117, 144, 48));
	//text_layer_set_font(&wbox_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	layer_add_child(&window.layer, &wbox_layer.layer);
	//
	
		// Text for Temperature
	
	
	text_layer_init(&text_temperature_layer, window.layer.frame);
	text_layer_set_text_color(&text_temperature_layer, GColorBlack);
	text_layer_set_background_color(&text_temperature_layer, GColorClear);
	layer_set_frame(&text_temperature_layer.layer, GRect(80, 117, 64, 68));
	text_layer_set_font(&text_temperature_layer, font_temperature);
	layer_add_child(&window.layer, &text_temperature_layer.layer); 

	
	// Calls Info layer
	text_layer_init(&calls_layer, window.layer.frame);
	text_layer_set_text_color(&calls_layer, GColorBlack);
	text_layer_set_background_color(&calls_layer, GColorClear);
	layer_set_frame(&calls_layer.layer, GRect(17, 122, 100, 30));
	text_layer_set_font(&calls_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	layer_add_child(&window.layer, &calls_layer.layer);
	text_layer_set_text(&calls_layer, "?");
	set_container_image(&httpebble_images[0], HTTPEBBLE_IMAGE_RESOURCE_IDS[0], GPoint(5, 126));
	
	// SMS Info layer 
	text_layer_init(&sms_layer, window.layer.frame);
	text_layer_set_text_color(&sms_layer, GColorBlack);
	text_layer_set_background_color(&sms_layer, GColorClear);
	layer_set_frame(&sms_layer.layer, GRect(17, 142, 100, 30));
	text_layer_set_font(&sms_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	layer_add_child(&window.layer, &sms_layer.layer);
	text_layer_set_text(&sms_layer, "?");
	set_container_image(&httpebble_images[1], HTTPEBBLE_IMAGE_RESOURCE_IDS[1], GPoint(5, 147));
	
	data.link_status = LinkStatusUnknown;
	link_monitor_ping();
	
	// request data refresh on window appear (for example after notification)
	WindowHandlers handlers = { .appear = &link_monitor_ping};
	window_set_window_handlers(&window, handlers);
	
    http_register_callbacks((HTTPCallbacks){.failure=failed,.success=success,.reconnect=reconnect,.location=location,.time=receivedtime}, (void*)ctx);
    register_callbacks();
	
    // Avoids a blank screen on watch start.
    PblTm tick_time;

    get_time(&tick_time);
    //update_display(&tick_time);
	
}


static void
handle_deinit(
        AppContextRef ctx
)
{
        (void) ctx;

        fonts_unload_custom_font(font_small);
        fonts_unload_custom_font(font_big);
		fonts_unload_custom_font(font_temperature);
}


void
pbl_main(
        void * const params
)
{
        PebbleAppHandlers handlers = {
                .init_handler   = &handle_init,
                .deinit_handler = &handle_deinit,
                .tick_info      = {
                        .tick_handler = &handle_tick,
                        .tick_units = MINUTE_UNIT,
                },
					.messaging_info = {
		.buffer_sizes = {
			.inbound = 124,
			.outbound = 256,
		}
	}
        };

        app_event_loop(params, &handlers);
}

void request_weather() {
	
	if(!located) {
		http_location_request();
		return;
	}
	
	// Build the HTTP request
	DictionaryIterator *body;
	HTTPResult result = http_out_get("http://www.zone-mr.net/api/weather.php", WEATHER_HTTP_COOKIE, &body);
	if(result != HTTP_OK) {
		return;
	}
	
	dict_write_int32(body, WEATHER_KEY_LATITUDE, our_latitude);
	dict_write_int32(body, WEATHER_KEY_LONGITUDE, our_longitude);
	dict_write_cstring(body, WEATHER_KEY_UNIT_SYSTEM, UNIT_SYSTEM);
	
	// Send it.
	if(http_out_send() != HTTP_OK) {
		return;
	}
	
	// Request updated Time
	http_time_request();
}