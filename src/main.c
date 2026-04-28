#include "lvgl/lvgl.h"
#include <unistd.h>

/*A simple button with an event*/
void button_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        LV_LOG_USER("Button clicked");
    }
    else if(code == LV_EVENT_VALUE_CHANGED) {
        LV_LOG_USER("Button toggled");
    }
}

void lv_example_get_started_1(void)
{
    lv_obj_t * btn = lv_button_create(lv_screen_active());  /*Add a button the current screen*/
    lv_obj_set_pos(btn, 10, 10);                            /*Set its position*/
    lv_obj_set_size(btn, 120, 50);                          /*Set its size*/
    lv_obj_add_event_cb(btn, button_event_handler, LV_EVENT_ALL, NULL);           /*Assign a callback to the button*/

    lv_obj_t * label = lv_label_create(btn);                /*Add a label to the button*/
    lv_label_set_text(label, "Button");                     /*Set the labels text*/
    lv_obj_center(label);
}

int main(void)
{
    lv_init();

    /*Create a display with 320x240 resolution*/
    lv_display_t * disp = lv_display_create(320, 240);
    lv_display_set_buffers(disp, NULL, NULL, 320 * 240, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /*Create UI*/
    lv_example_get_started_1();

    /*Handle LVGL tasks*/
    while(1) {
        lv_timer_handler();
        usleep(5000);
    }

    return 0;
}
