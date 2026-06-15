idf_component_register(
    SRCS "tab5_bsp_adapter.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_codec_dev
        esp_timer
        esp_lvgl_port
        espressif__esp_io_expander
        lvgl
        m5stack_tab5
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
