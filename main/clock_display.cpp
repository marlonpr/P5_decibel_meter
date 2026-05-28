#include "clock_display.h"

#include <stdio.h>

#include "led_panel.h"
#include "logo.h"

// =============================== DATE TEXT ===============================

static const char *dias_semana[] = {
    "DOMINGO",
    "LUNES",
    "MARTES",
    "MIERCOLES",
    "JUEVES",
    "VIERNES",
    "SABADO"
};

static const char *meses[] = {
    "ENERO",
    "FEBRERO",
    "MARZO",
    "ABRIL",
    "MAYO",
    "JUNIO",
    "JULIO",
    "AGOSTO",
    "SEPTIEMBRE",
    "OCTUBRE",
    "NOVIEMBRE",
    "DICIEMBRE"
};

static int get_weekday_index(const ds3231_time_t *time)
{
    if (!time) {
        return 0;
    }

    if (time->day_of_week < 1 || time->day_of_week > 7) {
        return 0;
    }

    return time->day_of_week - 1;
}

static int get_month_index(const ds3231_time_t *time)
{
    if (!time) {
        return 0;
    }

    if (time->month < 1 || time->month > 12) {
        return 0;
    }

    return time->month - 1;
}

void clock_display_make_date_scroll_text(const ds3231_time_t *time,
                                         char *buf,
                                         size_t size)
{
    if (!time || !buf || size == 0) {
        return;
    }

    int weekday_index = get_weekday_index(time);
    int month_index = get_month_index(time);

    snprintf(buf,
             size,
             "%s %d %s %04d",
             dias_semana[weekday_index],
             time->day,
             meses[month_index],
             time->year);
}

// =============================== TEXT HELPERS ===============================

static int text_width_5x7(const char *text)
{
    if (!text) {
        return 0;
    }

    int len = 0;

    while (*text++) {
        len++;
    }

    return len * 6; // 5 pixels glyph + 1 pixel spacing
}

int clock_display_center_x_5x7(const char *text)
{
    int width = text_width_5x7(text);

    if (width >= 64) {
        return 0;
    }

    return (64 - width) / 2;
}


static int text_width_6x9(const char *text)
{
    if (!text) {
        return 0;
    }

    int len = 0;

    while (*text++) {
        len++;
    }

    return len * 7; // 6 pixels glyph + 1 pixel spacing
}

int clock_display_center_x_6x9(const char *text)
{
    int width = text_width_6x9(text);

    if (width >= 64) {
        return 0;
    }

    return (64 - width) / 2;
}










// =============================== CLOCK / TEMP HELPERS ===============================

static int get_display_hour(const ds3231_time_t *time, hour_format_t format)
{
    int hour = (int)time->hour;

    if (hour < 0) {
        hour = 0;
    }

    if (hour > 23) {
        hour = 23;
    }

    if (format == FORMAT_24H) {
        return hour;
    }

    int hour12 = hour % 12;

    if (hour12 == 0) {
        hour12 = 12;
    }

    return hour12;
}

static int safe_minute(const ds3231_time_t *time)
{
    int minute = (int)time->minute;

    if (minute < 0) {
        minute = 0;
    }

    if (minute > 59) {
        minute = 59;
    }

    return minute;
}

static int safe_second(const ds3231_time_t *time)
{
    int second = (int)time->second;

    if (second < 0) {
        second = 0;
    }

    if (second > 59) {
        second = 59;
    }

    return second;
}

static void get_temp_color(float temp_c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!r || !g || !b) {
        return;
    }

    if (temp_c < 10.0f) {
        *r = 255;
        *g = 255;
        *b = 255;
    } else if (temp_c < 20.0f) {
        *r = 0;
        *g = 255;
        *b = 255;
    } else if (temp_c < 30.0f) {
        *r = 255;
        *g = 65;
        *b = 0;
    } else {
        *r = 255;
        *g = 0;
        *b = 0;
    }
}

static void make_temp_text(char *buf,
                           size_t size,
                           float temp_c,
                           bool temp_valid,
                           bool with_c)
{
    if (!buf || size == 0) {
        return;
    }

    if (temp_valid) {
        int temp_int = (int)temp_c;

        if (with_c) {
            snprintf(buf, size, "%d*C", temp_int);
        } else {
            snprintf(buf, size, "%d", temp_int);
        }
    } else {
		if (with_c) {
		    snprintf(buf, size, "-");
		} else {
		    snprintf(buf, size, "T E");
		}
    }
}


static int get_weekday_x_position(int weekday_index)
{
    /*
     * Adjust these for your 6x9 font.
     *
     * dias_semana:
     * 0 DOMINGO    7 chars
     * 1 LUNES      5 chars
     * 2 MARTES     6 chars
     * 3 MIERCOLES  9 chars
     * 4 JUEVES     6 chars
     * 5 VIERNES    7 chars
     * 6 SABADO     6 chars
     *
     * With 6x9 font, each char advances about 7 px.
     * Long names cannot truly center on 64 px, so some start at x=0.
     */
    switch (weekday_index)
    {
        case 0: return 6;   // DOMINGO
        case 1: return 14;  // LUNES
        case 2: return 10;  // MARTES
        case 3: return 0;   // MIERCOLES
        case 4: return 10;  // JUEVES
        case 5: return 6;   // VIERNES
        case 6: return 10;  // SABADO
        default: return 0;
    }
}




// =============================== SCREENS ===============================


void clock_display_draw_mode_test(Hub75Driver *driver,
                               const ds3231_time_t *time,
                               float temp_c,
                               bool temp_valid,
                               hour_format_t format, int dB_value)
{
    if (!driver || !time) {
        return;
    }

    char line1[32];
    char line2[16];
	
	char line3[16];
	
	char line4[32];

    static char date_scroll_text[64];

	clock_display_make_date_scroll_text(time,
	                                    date_scroll_text,
	                                    sizeof(date_scroll_text));

	scroll_start_if_needed(date_scroll_text, 12, 0, 255, 0, 10);
	scroll_update(*driver);

    int hour = get_display_hour(time, format);
    int minute = safe_minute(time);
    int second = safe_second(time);

    if (hour < 10) {  //if (format == FORMAT_12H &&
        snprintf(line1, sizeof(line1),
                 " %1d:%02d:%02d",
                 hour,
                 minute,
                 second);
    } else {
        snprintf(line1, sizeof(line1),
                 "%02d:%02d:%02d",
                 hour,
                 minute,
                 second);
    }

    draw_string(*driver, 4, 1, line1, 255, 255, 255);

    uint8_t r_temp = 255;
    uint8_t g_temp = 255;
    uint8_t b_temp = 255;

    get_temp_color(temp_c, &r_temp, &g_temp, &b_temp);
    make_temp_text(line2, sizeof(line2), temp_c, temp_valid, true);
	
	//make_temp_text(line3, sizeof(line3), dB_value, temp_valid, true);
	
	
	
	snprintf(line3, sizeof(line3),
	         "%d#dB",
	         dB_value);
			 
			 
			 
/*
snprintf(line4, sizeof(line4),
         "%s %s",
         line2, line3);
*/
	
	
	

    if (temp_valid) {
        //draw_string(*driver, 20, 22, line2, r_temp, g_temp, b_temp);
		//draw_string(*driver, 1, 22, line4, r_temp, g_temp, b_temp);
		
		draw_string(*driver, 2, 22, line2, r_temp, g_temp, b_temp);
		draw_string(*driver, 35, 22, line3, 255, 0, 0);
		
		
    } else {
        //draw_string(*driver, 20, 22, "T E", 255, 0, 0);
		draw_string(*driver, 18, 22, line3, 255, 0, 0);
    }
}














void clock_display_draw_mode_1(Hub75Driver *driver,
                               const ds3231_time_t *time,
                               float temp_c,
                               bool temp_valid,
                               hour_format_t format)
{
    if (!driver || !time) {
        return;
    }

    char line1[32];
    char line2[16];

    static char date_scroll_text[64];

	clock_display_make_date_scroll_text(time,
	                                    date_scroll_text,
	                                    sizeof(date_scroll_text));

	scroll_start_if_needed(date_scroll_text, 12, 0, 255, 0, 10);
	scroll_update(*driver);

    int hour = get_display_hour(time, format);
    int minute = safe_minute(time);
    int second = safe_second(time);

    if (hour < 10) {  //if (format == FORMAT_12H &&
        snprintf(line1, sizeof(line1),
                 " %1d:%02d:%02d",
                 hour,
                 minute,
                 second);
    } else {
        snprintf(line1, sizeof(line1),
                 "%02d:%02d:%02d",
                 hour,
                 minute,
                 second);
    }

    draw_string(*driver, 4, 1, line1, 255, 255, 255);

    uint8_t r_temp = 255;
    uint8_t g_temp = 255;
    uint8_t b_temp = 255;

    get_temp_color(temp_c, &r_temp, &g_temp, &b_temp);
    make_temp_text(line2, sizeof(line2), temp_c, temp_valid, true);

    if (temp_valid) {
        draw_string(*driver, 20, 22, line2, r_temp, g_temp, b_temp);
    } else {
        draw_string(*driver, 20, 22, "T E", 255, 0, 0);
    }
}

void clock_display_draw_mode_2(Hub75Driver *driver,
                               const ds3231_time_t *time,
                               float temp_c,
                               bool temp_valid,
                               hour_format_t format)
{
    if (!driver || !time) {
        return;
    }

    char buf_hour[4];
    char buf_minute[8];
    char buf_second[8];
    char buf_temp[20];

    int pos_hour = 0;

    int hour = get_display_hour(time, format);
    int minute = safe_minute(time);
    int second = safe_second(time);

    bool colon_on = (second % 2) == 0;

    uint8_t r_temp = 255;
    uint8_t g_temp = 255;
    uint8_t b_temp = 255;

    get_temp_color(temp_c, &r_temp, &g_temp, &b_temp);

    /*
     * Top scrolling date line.
     * Same behavior as old:
     * start_date_scroll_if_needed(time, 2, 0, 0, 255, 10);
     */
 
    static char date_scroll_text[64];

	clock_display_make_date_scroll_text(time,
	                                    date_scroll_text,
	                                    sizeof(date_scroll_text));

	scroll_start_if_needed(date_scroll_text, 2, 0, 0, 255, 10);
	scroll_update(*driver);

    /*
     * Right-side seconds or AM/PM indicator.
     *
     * Old behavior:
     * - 12H mode: show AM/PM
     * - 24H mode: show seconds
     */
    if (format == FORMAT_12H) {
        if (time->hour >= 12) {
            draw_string_5x5(*driver, 51, 16, "&$", 255, 255, 255); // PM
        } else {
            draw_string_5x5(*driver, 51, 16, "#$", 255, 255, 255); // AM
        }
    } else {
        snprintf(buf_second, sizeof(buf_second), "%02d", second);
        draw_string_5x5(*driver, 51, 16, buf_second, 255, 255, 255);
    }

    /*
     * Big hour/minute.
     */
    snprintf(buf_minute, sizeof(buf_minute), "%02d", minute);

    if ((minute % 10) == 1) {
        pos_hour = 2;
    }

	if (hour < 10) {
	    buf_hour[0] = '0' + hour;
	    buf_hour[1] = '\0';

	    /*
	     * One-digit hour.
	     * Adjust these X positions to visually center the full clock.
	     */
	    pos_hour -= 1;

	    draw_string_10x15(*driver,
	                      9 + pos_hour,
	                      14,
	                      buf_hour,
	                      255,
	                      255,
	                      255);

	    draw_string_10x15(*driver,
	                      27 + pos_hour,
	                      14,
	                      buf_minute,
	                      255,
	                      255,
	                      255);
	} else {
        buf_hour[0] = '0' + (hour / 10);
        buf_hour[1] = '0' + (hour % 10);
        buf_hour[2] = '\0';

        if (hour > 19) {
            pos_hour += 1;

            if ((minute % 10) == 1) {
                pos_hour -= 1;
            }
        }

        draw_string_10x15(*driver,
                        pos_hour,
                        14,
                        buf_hour,
                        255,
                        255,
                        255);

        draw_string_10x15(*driver,
                        27 + pos_hour,
                        14,
                        buf_minute,
                        255,
                        255,
                        255);
    }

    /*
     * Colon.
     *
     * Old behavior:
     * - 12H mode: blinking colon
     * - 24H mode: solid colon
     *
     * If your symbol font uses "!" as colon, keep "!".
     * Otherwise use ":".
     */
	 int colon_x;

	 if (hour < 10) {
	     colon_x = 22 + pos_hour;
	 } else {
	     colon_x = 23 + pos_hour;
	 }

	 if (format == FORMAT_12H) {
	     draw_string_2x9(*driver,
	                     colon_x,
	                     17,
	                     colon_on ? "!" : " ",
	                     255,
	                     255,
	                     255);
	 } else {
	     draw_string_2x9(*driver,
	                     colon_x,
	                     17,
	                     "!",
	                     255,
	                     255,
	                     255);
	 }

    /*
     * Temperature at lower-right.
     *
     * Old behavior:
     * draw_text_6(50, 26, buf_temp, ...)
     * draw_text_4(57, 26, "#", ...)
     * draw_text_6(60, 26, "$", ...)
     *
     * If your font already has '*' as degree symbol and C as normal C,
     * just draw "%d*C".
     */
    make_temp_text(buf_temp,
                   sizeof(buf_temp),
                   temp_c,
                   temp_valid,false);

    if (temp_valid) {
        draw_string_3x5(*driver,
                        50,
                        26,
                        buf_temp,
                        r_temp,
                        g_temp,
                        b_temp);

        draw_string_2x9(*driver,
                        57,
                        26,
                        "#",
                        r_temp,
                        g_temp,
                        b_temp);
						
		draw_string_3x5(*driver,
		                60,
		                26,
		                "$",
		                r_temp,
		                g_temp,
		                b_temp);
    } else {
        draw_string_3x5(*driver,
                        50,
                        26,
                        "--",
                        255,
                        0,
                        0);
    }
}

void clock_display_draw_mode_3(Hub75Driver *driver,
                               const ds3231_time_t *time,
                               float temp_c,
                               bool temp_valid,
                               hour_format_t format)
{
    if (!driver || !time) {
        return;
    }

    uint8_t r_temp = 255;
    uint8_t g_temp = 255;
    uint8_t b_temp = 255;

    get_temp_color(temp_c, &r_temp, &g_temp, &b_temp);

    int weekday_index = get_weekday_index(time);
    int pos_day = get_weekday_x_position(weekday_index);

    char buf_day[32];

    snprintf(buf_day, sizeof(buf_day), "%s", dias_semana[weekday_index]);

    draw_string(*driver,
                1 + pos_day,
                1,
                buf_day,
                0,
                255,
                0);

    char buf_date[32];

    snprintf(buf_date,
             sizeof(buf_date),
             "%02d-%02d-%02d",
             time->day,
             time->month,
             time->year - 2000);

    draw_string(*driver,
                4,
                11,
                buf_date,
                0,
                0,
                255);

    int hour = get_display_hour(time, format);
    int minute = safe_minute(time);
    int second = safe_second(time);

    bool colon_on = (second % 2) == 0;

    char buf_time[24];

    if (hour < 10) {
        snprintf(buf_time,
                 sizeof(buf_time),
                 colon_on ? " %1d:%02d" : " %1d %02d",
                 hour,
                 minute);
    } else {
        snprintf(buf_time,
                 sizeof(buf_time),
                 colon_on ? "%02d:%02d" : "%02d %02d",
                 hour,
                 minute);
    }

    draw_string(*driver,
                2,
                22,
                buf_time,
                255,
                255,
                255);

    char buf_temp[16];

    make_temp_text(buf_temp,
                   sizeof(buf_temp),
                   temp_c,
                   temp_valid,
                   false);

    if (temp_valid) {
        draw_string(*driver,
                    43,
                    22,
                    buf_temp,
                    r_temp,
                    g_temp,
                    b_temp);
					
		draw_string(*driver,
		            57,
		            22,
		            "*",
		            r_temp,
		            g_temp,
		            b_temp);
    } else {
        draw_string(*driver,
                    43,
                    22,
                    "TE",
                    255,
                    0,
                    0);
    }
}

void clock_display_draw_logo(Hub75Driver *driver)
{
    if (!driver) {
        return;
    }

    draw_bitmap_rgb32(*driver,
                      0,
                      0,
                      logo_bitmap,
                      LOGO_WIDTH,
                      LOGO_HEIGHT);
}

void clock_display_draw_startup(Hub75Driver *driver,
                                int display_mode,
                                int brightness_level,
                                hour_format_t format)
{
    if (!driver) {
        return;
    }

    char line1[16];
    char line2[16];
    char line3[16];

    snprintf(line1, sizeof(line1), "MODO:%d", display_mode);
    snprintf(line2, sizeof(line2), "BRILLO:%d", brightness_level);
    snprintf(line3, sizeof(line3), "%s",
             format == FORMAT_24H ? "24HRS:ON" : "24HRS:OFF");

    draw_string_5x7(*driver, clock_display_center_x_5x7(line1), 1,  line1, 255, 0, 0);
    draw_string_5x7(*driver, clock_display_center_x_5x7(line2), 11, line2, 0, 255, 0);
    draw_string_5x7(*driver, clock_display_center_x_5x7(line3), 22, line3, 0, 255, 255);
}



//==== mode 1 test===
/*
if (format == FORMAT_12H) {
    if (time->hour >= 12) {
        draw_string(*driver, 51, 22, "PM", 255, 255, 255);
    } else {
        draw_string(*driver, 51, 22, "AM", 255, 255, 255);
    }
} else {
    draw_string(*driver, 51, 22, "24", 255, 255, 255);
}
*/