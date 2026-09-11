/*
 * Turn the RGB underglow on while the half is powered from a cable, and off
 * again as soon as the cable is removed.
 *
 * ZMK's own CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB cannot do this. Its state
 * machine in rgb_underglow.c starts with `is_awake = true` while the underglow
 * itself starts off, so the first "USB connected" event is swallowed as a
 * no-op and, after one unplug, the remembered pre-sleep state is latched to
 * false and the underglow can never come back on. That option is disabled for
 * the halves in the matching .conf files so the two do not fight.
 *
 * The halves run as split peripherals, so CONFIG_ZMK_USB is forced off
 * (ZMK_USB depends on `!ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL`) and there is no
 * USB HID. CONFIG_USB_DEVICE_STACK is still enabled by default on nRF52840
 * (`default y if HAS_HW_NRF_USBD`), so zmk/usb.c is compiled, usb_enable() is
 * called, and zmk_usb_is_powered() reports cable presence just fine.
 *
 * We poll rather than subscribe to zmk_usb_conn_state_changed so that the
 * state at boot is picked up regardless of SYS_INIT ordering. The 1 Hz cadence
 * adds no new wakeups: activity.c already runs a 1 Hz timer on every board.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zmk/rgb_underglow.h>
#include <zmk/usb.h>

LOG_MODULE_REGISTER(rgb_vbus_sync, CONFIG_ZMK_LOG_LEVEL);

/* UNDERGLOW_EFFECT_SWIRL: hue spread across the LED chain, i.e. a rainbow. */
#define RAINBOW_EFFECT 3

/* Wanted brightness as a percentage of full output. The driver scales the
 * stored brightness against CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX (see
 * hsb_scale_min_max in rgb_underglow.c), so undo that scaling here to keep
 * this number meaning what it says. */
#define RAINBOW_BRIGHTNESS_PERCENT 15
#define RAINBOW_BRIGHTNESS_VALUE                                                                   \
    (RAINBOW_BRIGHTNESS_PERCENT * 100 / CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX)

BUILD_ASSERT(CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN == 0,
             "Brightness scaling below assumes a minimum brightness of 0");
BUILD_ASSERT(RAINBOW_BRIGHTNESS_VALUE <= 100,
             "CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX is too low to reach the wanted brightness");

/* Delay before the first sync, so the underglow driver, the settings
 * subsystem and the USB stack have all finished initialising. */
#define INITIAL_SYNC_DELAY K_SECONDS(2)
#define POLL_INTERVAL K_SECONDS(1)

static int last_power_state = -1;
static bool appearance_applied;

/* Applied once per boot, so a stored brightness or effect from a previous
 * session cannot leave the underglow looking different after a reboot. */
static void apply_appearance(void) {
    struct zmk_led_hsb color = {
        .h = CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
        .s = CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
        .b = RAINBOW_BRIGHTNESS_VALUE,
    };

    zmk_rgb_underglow_set_hsb(color);
    zmk_rgb_underglow_select_effect(RAINBOW_EFFECT);
}

static void rgb_vbus_sync_work_handler(struct k_work *work) {
    int powered = zmk_usb_is_powered() ? 1 : 0;

    if (powered != last_power_state) {
        last_power_state = powered;

        if (powered) {
            if (!appearance_applied) {
                apply_appearance();
                appearance_applied = true;
            }
            LOG_INF("Cable connected, enabling RGB underglow");
            zmk_rgb_underglow_on();
        } else {
            LOG_INF("Cable disconnected, disabling RGB underglow");
            zmk_rgb_underglow_off();
        }
    }

    k_work_reschedule(k_work_delayable_from_work(work), POLL_INTERVAL);
}

static K_WORK_DELAYABLE_DEFINE(rgb_vbus_sync_work, rgb_vbus_sync_work_handler);

static int rgb_vbus_sync_init(void) {
    k_work_reschedule(&rgb_vbus_sync_work, INITIAL_SYNC_DELAY);
    return 0;
}

SYS_INIT(rgb_vbus_sync_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
