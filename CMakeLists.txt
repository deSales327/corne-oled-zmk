if(CONFIG_CORNE_POWER_TEST)
  zephyr_library()
  zephyr_library_sources(src/power_test.c)
endif()
