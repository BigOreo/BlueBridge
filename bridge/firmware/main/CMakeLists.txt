idf_component_register(
    SRCS "main.c" "bridge.c" "commands.c" "reports.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bt nvs_flash esp_driver_uart esp_driver_usb_serial_jtag
)
