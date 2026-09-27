idf_component_register(
    SRCS "safety_watchdog.cpp"
    INCLUDE_DIRS "include"
    REQUIRES sensor output esp_timer
    PRIV_REQUIRES log
)
