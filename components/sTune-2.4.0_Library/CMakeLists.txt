idf_component_register(
    SRCS "sTan.cpp" "sTune.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer esp_driver_gpio log
)
