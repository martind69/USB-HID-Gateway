# Shared between the P4 project (components/ is scanned automatically) and the
# S3 project (firmware/secondary_s3/CMakeLists.txt, EXTRA_COMPONENT_DIRS).
# esp_tinyusb is provided by the managed-component manifest of each project.
idf_component_register(
    SRCS
        "p4_s3_protocol.c"
        "usb_mirror_descriptor.c"
        "usb_mirror_device.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_tinyusb
        freertos
        log
    WHOLE_ARCHIVE
)
