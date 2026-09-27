idf_component_register(
    SRCS "storage_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES "config" "freertos" "joltwallet__littlefs"
    PRIV_REQUIRES "cjson" "nvs_flash" "log"
)
