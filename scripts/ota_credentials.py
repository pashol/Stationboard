Import("env")

import os


def c_string(value):
    return '\\"' + value.replace('\\', '\\\\').replace('"', '\\"') + '\\"'


username = os.environ.get("OTA_USERNAME", "")
password = os.environ.get("OTA_PASSWORD", "")

# Do not provide partial credentials. ota.cpp defaults both macros to empty,
# which leaves the physical OTA entry point disabled for this build.
if username and password:
    env.Append(BUILD_FLAGS=[
        "-DOTA_USERNAME=" + c_string(username),
        "-DOTA_PASSWORD=" + c_string(password),
    ])
