# Storage and verbose capture

- SD filesystem: FAT32, one MBR partition, 32 KiB clusters, volume label `USB_GATEWAY`.
- Mount point: `/sdcard`.
- exFAT is intentionally not used.
- JSON is authoritative; binary profile cache is derived.
- Verbose preset uses a 600 s capture window, 8 MiB files and 256 MiB total budget.
- Binary records are queued in RAM and written by a low-priority task.
- Wi-Fi is neither initialized nor referenced. ESP32-P4 has no integrated Wi-Fi; ESP32-S3 Wi-Fi remains unused.
