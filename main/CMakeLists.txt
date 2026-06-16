idf_component_register(
    SRCS
        "app_main.cpp"
        "audio_engine.cpp"
        "patterns.cpp"
        "transport.cpp"
        "ui_controller.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        json
        esp_timer
        esp_lcd
        espressif__bmi270
        lvgl
        m5stack_tab5
        tab5_bsp_adapter
    EMBED_TXTFILES
        "patterns.json"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
