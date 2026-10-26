idf_component_register(
    SRCS "main.c" "hid_out_usb.c" "keys.c" "rhythm.c" "pointer.c" "typing.c" "script.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_hw_support esp_timer
    EMBED_TXTFILES "macros/developer.txt" "macros/snippets.txt"
)
