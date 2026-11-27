# Implemented next-four features

## SSD1306

Full printable ASCII 5x7 font, text drawing, pixel drawing and progress bar. The status renderer uses seven pages and keeps OLED updates in the existing low-priority dirty-refresh task.

## P4-S3 STATUS

The link now correlates the one-transaction-delayed STATUS frame by sequence. When no command is queued but a status is pending, it clocks a NOP after 2 ms. CRC, sequence, timeout and latency metrics are updated.

## Output, Feature and EP0

A 56-byte fragment payload fits the existing 64-byte P4/S3 frame after an 8-byte fragment header. Output and Feature SET reports are reassembled on P4. Feature GET and EP0 use response fragments. The exact physical USB Host functions are registered as callbacks, keeping the transport independent from the selected host library.

## SD and logs

FAT32 SDMMC 1-bit mount, no automatic formatting, 32 KiB allocation unit, standard directory creation, free-space query, timed capture, queue, periodic flush, 8 MiB default file rotation and 256 MiB session budget. New sessions require at least 64 MiB free.
