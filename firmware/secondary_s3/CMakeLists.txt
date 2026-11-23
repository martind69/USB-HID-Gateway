cmake_minimum_required(VERSION 3.16)

# Shared mirror component (protocol, descriptor analysis, TinyUSB mirror).
# Only this component directory is added, not the whole P4 components/ tree.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components/usb_mirror")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(usb_hid_gateway_secondary_s3)
