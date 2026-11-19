idf_component_register(
    SRCS "main.c" "spi_link.c" "usb_device.c" "p4_s3_protocol.c" "s3_proxy_reports.c"
    INCLUDE_DIRS "."
    REQUIRES esp_tinyusb driver esp_timer
)
