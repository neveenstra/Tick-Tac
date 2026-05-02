idf_component_register(SRCS "main.c" "midi_clock.c" "usb_midi.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES
                        esp_tinyusb
                        nvs_flash
                        esp_bsp
                        lvgl_ui
                        espressif__esp_lvgl_port
                        esp_timer
                        driver
                        log
                        freertos)
