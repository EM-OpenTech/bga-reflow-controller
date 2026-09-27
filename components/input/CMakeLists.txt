idf_component_register(
    SRCS "input_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio esp_timer config
    PRIV_REQUIRES log
)
