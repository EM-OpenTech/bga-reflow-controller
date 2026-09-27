idf_component_register(
    SRCS "output_manager.cpp" "burst_fire.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config driver esp_driver_gpio esp_timer
    PRIV_REQUIRES log
)
