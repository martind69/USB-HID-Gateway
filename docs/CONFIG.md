# SD card configuration

## Layout

    /sdcard/config/gateway.json      device configuration (USB ports, intercept rules)
    /sdcard/config/profile-01.json   motion profile (tasks 2 and 3)
    /sdcard/motion/profile-01.bin    binary cache of the motion profile
    /sdcard/captures/                USB capture sessions

All paths are defined in `components/usb_gateway/gateway_paths.h`.
All JSON files are parsed with cJSON and strict key checking.

## Migration

At boot `gateway_config_prepare_storage()` creates `/sdcard/config` and moves
`/sdcard/motion/profile-01.json` to `/sdcard/config/profile-01.json` if the
target does not exist. If both exist, the file in `/sdcard/config` is used.

## gateway.json (schemaVersion 1)

| Key | Type | Meaning |
|---|---|---|
| `usb.ports[]` | array, 0..16 | port assignment, first match wins |
| `usb.ports[].match` | object | exactly one of `{vid[,pid]}`, `{class}`, `{any:true}` |
| `vid`, `pid` | string, 1..4 hex digits | USB vendor / product ID |
| `class` | `"keyboard"` \| `"mouse"` | interface class from boot protocol or report usage |
| `usb.ports[].target` | `"p4_fs"` \| `"s3"` \| `"tap"` \| `"none"` | mirror target |
| `usb.intercept` | array | reserved for M5, must be empty |

`p4_fs` and `s3` accept one device each. If `gateway.json` is missing or
invalid, the default rules apply: keyboard -> s3, mouse -> p4_fs, rest -> none.

## Status

M1: file loaded and validated at boot. Port claiming is consumed by the
neutral USB host client in M2.
