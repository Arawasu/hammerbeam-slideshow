/*
 *  Hammerbeam right‑hand display widget
 *
 *  © 2023‑2025  The ZMK Contributors – MIT License
 *
 *  Re‑worked by Arawasu (Apr 2025)
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
 
 static const lv_img_dsc_t *anim_imgs[] = {
     &hammerbeam1,  &hammerbeam2,  &hammerbeam3,  &hammerbeam4,  &hammerbeam5,
     &hammerbeam6,  &hammerbeam7,  &hammerbeam8,  &hammerbeam9,  &hammerbeam10,
     &hammerbeam11, &hammerbeam12, &hammerbeam13, &hammerbeam14, &hammerbeam15,
     &hammerbeam16, &hammerbeam17, &hammerbeam18, &hammerbeam19, &hammerbeam20,
     &hammerbeam21, &hammerbeam22, &hammerbeam23, &hammerbeam24, &hammerbeam25,
     &hammerbeam26, &hammerbeam27, &hammerbeam28, &hammerbeam29, &hammerbeam30,
 };
 
 #define ART_FRAME_COUNT      (ARRAY_SIZE(anim_imgs))
 #define ART_ROTATE_INTERVAL  CONFIG_CUSTOM_ANIMATION_SPEED /* ms */
 
 /* ───────────────────────────────  Status‑bar bookkeeping  ─────────────────────────────── */
 
 static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
 
 struct peripheral_status_state {
     bool connected;
 };
 
 /* ───── Slideshow helpers (random order, no repeats) ───── */
 
 static lv_obj_t *art_box;                 /* container for the Hammerbeam frame  */
 static uint8_t  order[ART_FRAME_COUNT];
 static uint8_t  order_pos;
 
 static void shuffle_order(void)
 {
     for (uint8_t i = 0; i < ART_FRAME_COUNT; i++) {
         order[i] = i;
     }
     for (int i = ART_FRAME_COUNT - 1; i > 0; --i) {
         uint32_t j = sys_rand32_get() % (i + 1);
         uint8_t tmp = order[i];
         order[i]   = order[j];
         order[j]   = tmp;
     }
     order_pos = 0;
 }
 
 static void slideshow_cb(lv_timer_t *t)
 {
     if (order_pos >= ART_FRAME_COUNT) {
         shuffle_order();
     }
 
     /* clear only the art container, preserve status bar */
     lv_obj_clean(art_box);
 
     lv_obj_t *img = lv_img_create(art_box);
     lv_img_set_src(img, anim_imgs[order[order_pos++]]);
     lv_obj_align(img, LV_ALIGN_TOP_LEFT, 0, 0);
 }
 
 /* ───────────────────────────────  Status bar drawing  ─────────────────────────────── */
 
 static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
     lv_obj_t *canvas = lv_obj_get_child(widget, 0);
 
     lv_draw_label_dsc_t label_dsc;
     init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
     lv_draw_rect_dsc_t rect_black_dsc;
     init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
 
     // Fill background
     lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);
 
     // Draw battery
     draw_battery(canvas, state);
 
     // Draw output status
     lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                         state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
 
     // Rotate canvas
     rotate_canvas(canvas, cbuf);
 }
 
 static void set_battery_status(struct zmk_widget_status *widget,
                                struct battery_status_state state) {
 #if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
     widget->state.charging = state.usb_present;
 #endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
 
     widget->state.battery = state.level;
 
     draw_top(widget->obj, widget->cbuf, &widget->state);
 }
 
 static void battery_status_update_cb(struct battery_status_state state) {
     struct zmk_widget_status *widget;
     SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
         set_battery_status(widget, state);
     }
 }
 
 static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
     return (struct battery_status_state) {
         .level = zmk_battery_state_of_charge(),
 #if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
         .usb_present = zmk_usb_is_powered(),
 #endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
     };
 }
 
 ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                             battery_status_update_cb, battery_status_get_state)
 
 ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
 #if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
 ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
 #endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
 
 static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
     return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
 }
 
 static void set_connection_status(struct zmk_widget_status *widget,
                                   struct peripheral_status_state state) {
     widget->state.connected = state.connected;
 
     draw_top(widget->obj, widget->cbuf, &widget->state);
 }
 
 static void output_status_update_cb(struct peripheral_status_state state) {
     struct zmk_widget_status *widget;
     SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
         set_connection_status(widget, state);
     }
 }
 
 ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                             output_status_update_cb, get_state)
 ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);
 
 int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent)
 {
     widget->obj = lv_obj_create(parent);
     lv_obj_set_size(widget->obj, 160, 68);
 
     lv_obj_t *top = lv_canvas_create(widget->obj);
     lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
     lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
 
     art_box = lv_obj_create(widget->obj);
     lv_obj_clear_flag(art_box, LV_OBJ_FLAG_SCROLLABLE);
     lv_obj_set_size(art_box, 140, 68);
     lv_obj_align(art_box, LV_ALIGN_TOP_LEFT, 0, 0);
 
     shuffle_order();
     slideshow_cb(NULL);
     lv_timer_create(slideshow_cb, ART_ROTATE_INTERVAL, NULL);
 
     sys_slist_append(&widgets, &widget->node);
     widget_battery_status_init();
     widget_peripheral_status_init();
 
     return 0;
 }
 
 lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) {
     return widget->obj;
 }
 