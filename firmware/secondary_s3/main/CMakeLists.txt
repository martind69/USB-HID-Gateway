idf_component_register(
    SRCS
        "main.c"
        "spi_link.c"
        "usb_device.c"
        "keyboard_report_queue.c"
        "p4_s3_protocol.c"
        "s3_proxy_reports.c"
        "g815_usb_descriptors.c"
        "g815_report_router.c"
        "g815_tinyusb_callbacks.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_tinyusb
        driver
        esp_timer
)
