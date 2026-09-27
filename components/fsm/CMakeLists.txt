idf_component_register(
    SRCS "reflow_fsm.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config output pid
    PRIV_REQUIRES esp_timer log
)
