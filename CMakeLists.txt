# Extra firmware sources contributed by this zmk-config repository.
# Registered because the ZMK build passes this repo via -DZMK_EXTRA_MODULES
# whenever a zephyr/module.yml is present at the repo root.

if(CONFIG_ZMK_RGB_UNDERGLOW AND CONFIG_USB_DEVICE_STACK AND CONFIG_ZMK_SPLIT AND NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  message(STATUS "zmk-halcyon-config: building RGB VBUS sync")

  zephyr_library_named(halcyon_config)
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(src/rgb_vbus_sync.c)
endif()
