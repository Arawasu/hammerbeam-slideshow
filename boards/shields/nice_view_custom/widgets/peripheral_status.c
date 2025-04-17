/*
 *  Hammerbeam right‑hand display widget
 *
 *  © 2023‑2025  The ZMK Contributors  – MIT License
 *
 *  Re‑worked by Arawasu
 *
 *  Key changes
 *  ───────────
 *  • Replaces lv_animimg with an LVGL timer (fixes e‑paper corruption).
 *  • Uses Fisher–Yates shuffle to show all 30 frames once in random order,
 *    then reshuffles and starts again – no repeats until the full set cycles.
 *  • Keeps original battery/Wi‑Fi status canvas untouched.
 *  • Interval is still driven by CONFIG_CUSTOM_ANIMATION_SPEED.
 */

 #include <zephyr/kernel.h>
 #include <zephyr/random/random.h>
 #include <zephyr/logging/log.h>
 LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
 
 #include <zmk/battery.h>
 #include <zmk/display.h>
 #include <zmk/events/usb_conn_state_changed.h>
 #include <zmk/event_manager.h>
 #include <zmk/events/battery_state_changed.h>
 #include <zmk/split/bluetooth/peripheral.h>
 #include <zmk/events/split_peripheral_status_changed.h>
 #include <zmk/usb.h>
 #include <zmk/ble.h>
 
 #include "peripheral_status.h"
 
 /* ───────────────────────────────  Hammerbeam art assets  ─────────────────────────────── */
 
 LV_IMG_DECLARE(hammerbeam1);
 LV_IMG_DECLARE(hammerbeam2);
 LV_IMG_DECLARE(hammerbeam3);
 LV_IMG_DECLARE(hammerbeam4);
 LV_IMG_DECLARE(hammerbeam5);
 LV_IMG_DECLARE(hammerbeam6);
 LV_IMG_DECLARE(hammerbeam7);
 LV_IMG_DECLARE(hammerbeam8);
 LV_IMG_DECLARE(hammerbeam9);
 LV_IMG_DECLARE(hammerbeam10);
 LV_IMG_DECLARE(hammerbeam11);
 LV_IMG_DECLARE(hammerbeam12);
 LV_IMG_DECLARE(hammerbeam13);
 LV_IMG_DECLARE(hammerbeam14);
 LV_IMG_DECLARE(hammerbeam15);
 LV_IMG_DECLARE(hammerbeam16);
 LV_IMG_DECLARE(hammerbeam17);
 LV_IMG_DECLARE(hammerbeam18);
 LV_IMG_DECLARE(hammerbeam19);
 LV_IMG_DECLARE(hammerbeam20);
 LV_IMG_DECLARE(hammerbeam21);
 LV_IMG_DECLARE(hammerbeam22);
 LV_IMG_DECLARE(hammerbeam23);
 LV_IMG_DECLARE(hammerbeam24);
 LV_IMG_DECLARE(hammerbeam25);
 LV_IMG_DECLARE(hammerbeam26);
 LV_IMG_DECLARE(hammerbeam27);
 LV_IMG_DECLARE(hammerbeam28);
 LV_IMG_DECLARE(hammerbeam29);
 LV_IMG_DECLARE(hammerbeam30);
 
 static const lv_img_dsc_t *art_images[] = {
     &hammerbeam1,  &hammerbeam2,  &hammerbeam3,  &hammerbeam4,  &hammerbeam5,
     &hammerbeam6,  &hammerbeam7,  &hammerbeam8,  &hammerbeam9,  &hammerbeam10,
     &hammerbeam11, &hammerbeam12, &hammerbeam13, &hammerbeam14, &hammerbeam15,
     &hammerbeam16, &hammerbeam17, &hammerbeam18, &hammerbeam19, &hammerbeam20,
     &hammerbeam21, &hammerbeam22, &hammerbeam23, &hammerbeam24, &hammerbeam25,
     &hammerbeam26, &hammerbeam27, &hammerbeam28, &hammerbeam29, &hammerbeam30,
 };
 
 #define ART_FRAME_COUNT      (ARRAY_SIZE(art_images))
 #define ART_ROTATE_INTERVAL  CONFIG_CUSTOM_ANIMATION_SPEED /* ms */
 
 /* ───────────────────────────────  Status‑bar bookkeeping  ─────────────────────────────── */
 
 static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
 
 struct peripheral_status_state {
     bool connected;
 };
 
 /* … existing draw_top(), battery‑/connection‑update callbacks stay intact … */
 
 /* ───────────────────────────────  Art‑slideshow helpers  ─────────────────────────────── */
 
 static lv_obj_t *art_box;          /* container that holds the current image             */
 static lv_obj_t *art_img;          /* the <img> object itself                            */
 static uint8_t   order[ART_FRAME_COUNT]; /* shuffle order                                 */
 static uint8_t   order_pos;        /* next index inside ‘order’                          */
 
 /* Fisher‑Yates shuffle using Zephyr RNG */
 static void shuffle_order(void)
 {
     for (uint8_t i = 0; i < ART_FRAME_COUNT; ++i) {
         order[i] = i;
     }
     for (int i = ART_FRAME_COUNT - 1; i > 0; --i) {
         uint32_t j = sys_rand32_get() % (i + 1);
         uint8_t  tmp = order[i];
         order[i] = order[j];
         order[j] = tmp;
     }
     order_pos = 0;
 }
 
 /* Timer callback – rotates to the next art frame */
 static void rotate_art_cb(lv_timer_t *t)
 {
     /* When we’ve shown everything, reshuffle */
     if (order_pos >= ART_FRAME_COUNT) {
         shuffle_order();
     }
 
     /* Erase previous frame (just the art box, not the whole widget) */
     lv_obj_clean(art_box);
 
     /* Draw next frame */
     art_img = lv_img_create(art_box);
     lv_img_set_src(art_img, art_images[order[order_pos++]]);
     lv_obj_align(art_img, LV_ALIGN_TOP_LEFT, 0, 0);
 }
 
 /* ───────────────────────────────  Widget initialisation  ─────────────────────────────── */
 
 int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent)
 {
     /* ‣ Root container ‣────────────────────────────────────────────────────────────── */
     widget->obj = lv_obj_create(parent);
     lv_obj_set_size(widget->obj, 160, 68);
 
     /* ‣ Status bar canvas (battery + Wi‑Fi) – unchanged ‣──────────────────────────── */
     lv_obj_t *top = lv_canvas_create(widget->obj);
     lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
     lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
 
     /* ‣ ART AREA – instead of lv_animimg we use our own timer‑driven slideshow ‣────── */
     art_box = lv_obj_create(widget->obj);
     lv_obj_clear_flag(art_box, LV_OBJ_FLAG_SCROLLABLE);
     lv_obj_set_size(art_box, 140, 68);           /* same as your art dimensions          */
     lv_obj_align(art_box, LV_ALIGN_TOP_LEFT, 0, 0);
 
     shuffle_order();                             /* initialise first random cycle        */
     rotate_art_cb(NULL);                         /* draw first image immediately         */
     lv_timer_create(rotate_art_cb, ART_ROTATE_INTERVAL, NULL);
 
     /* ‣ Hook into ZMK battery / connection events – original code intact ‣─────────── */
     sys_slist_append(&widgets, &widget->node);
     widget_battery_status_init();
     widget_peripheral_status_init();
 
     return 0;
 }
 
 lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget)
 {
     return widget->obj;
 }
 