"""
PlatformIO pre-build script: load .env and inject values as preprocessor
macros. Numbers and booleans are emitted as raw C literals; strings are
properly quoted. Any value containing an unescaped " is rejected to
prevent breaking the compile command line.
"""
import os
import sys

from dotenv import load_dotenv
from SCons.Script import DefaultEnvironment

load_dotenv()
env = DefaultEnvironment()

# (env_var_name, default, kind)
#   kind: "str" | "int" | "bool"
SPEC = [
    ("WIFI_SSID",          "",                "str"),
    ("WIFI_PASS",          "",                "str"),
    ("MQTT_HOST",          "localhost",       "str"),
    ("MQTT_PORT",          "1883",            "int"),
    ("MQTT_USERNAME",      "",                "str"),
    ("MQTT_PASSWORD",      "",                "str"),
    ("MQTT_TOPIC",         "default/topic",   "str"),
    ("MQTT_CLIENT_ID",     "default_client",  "str"),
    ("MQTT_QOS",           "1",               "int"),
    ("MQTT_RETAIN",        "0",               "bool"),
    ("MQTT_CLEAN_SESSION", "1",               "bool"),
    ("MQTT_TLS",           "0",               "bool"),
    ("MQTT_TLS_INSECURE",  "0",               "bool"),
]


def _to_bool(v):
    return "1" if str(v).strip().lower() in ("1", "true", "yes", "on") else "0"


for name, default, kind in SPEC:
    raw = os.getenv(name, default)
    if raw is None:
        raw = default

    if kind == "int":
        try:
            value_macro = str(int(str(raw).strip()))
        except ValueError:
            print(f"load_env: ERROR {name}='{raw}' is not an integer", file=sys.stderr)
            sys.exit(1)
        env.Append(CPPDEFINES=[(name, value_macro)])

    elif kind == "bool":
        env.Append(CPPDEFINES=[(name, _to_bool(raw))])

    else:  # str
        s = str(raw)
        if '"' in s:
            print(f"load_env: ERROR {name} contains a double-quote; not supported", file=sys.stderr)
            sys.exit(1)
        # StringifyMacro wraps the value in quotes for the C preprocessor.
        env.Append(CPPDEFINES=[(name, env.StringifyMacro(s))])

    # Avoid printing secret values; only confirm the macro was set.
    print(f"load_env: defined {name}")
