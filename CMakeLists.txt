if(CONFIG_TPS65_PROBE)
  zephyr_library()
  zephyr_library_sources(src/tps65_gpio_identify.c)
endif()
