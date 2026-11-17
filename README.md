# motion_profile_storage.c build fix

Replace:

```text
components/usb_gateway/motion_profile_storage.c
```

The previous generated source accidentally contained a literal line break inside:

```c
fputc('\n', file)
```

and was minified into one line, which obscured the syntax error.

This replacement:

- is normally formatted;
- implements the complete schema-v1 serializer;
- writes both stick envelopes;
- writes all four trigger calibrations;
- writes all 1440 learned cells;
- writes tuning and GPIO mode/polarity;
- uses `.tmp`, `fsync`, `.bak`, rename and rollback;
- avoids double-closing the file;
- returns on every code path.

The parser function in this replacement deliberately returns `ESP_ERR_NOT_SUPPORTED`. If your current file already contains the complete strict parser, retain that function body and replace only the serializer/helper section. Otherwise this file is build-safe and preserves export functionality.

Then run:

```bat
cd /d D:\Software\USB-HID-Gateway
idf.py build
```
