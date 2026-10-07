idf_component_register(
    SRCS
        "pid_controller.cpp"
        "pid_autotuner.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        "QuickPID-3.1.9_Library"
        "sTune-2.4.0_Library"
        "config"
    PRIV_REQUIRES
        "log"
)
