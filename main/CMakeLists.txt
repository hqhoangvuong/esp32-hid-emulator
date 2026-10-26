idf_component_register(
    SRCS "main.c" "human.c" "macro.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_hw_support
    EMBED_TXTFILES "macros/default.txt"
)
