import os
from dotenv import load_dotenv
load_dotenv()
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

# Get environment variables with default values
env_vars = {
    'WIFI_SSID': os.getenv('WIFI_SSID', 'default_ssid'),
    'WIFI_PASS': os.getenv('WIFI_PASS', 'default_password'),
    'MQTT_HOST': os.getenv('MQTT_HOST', 'localhost'),
    'MQTT_PORT': os.getenv('MQTT_PORT', '1883'),
    'MQTT_USERNAME': os.getenv('MQTT_USERNAME', ''),
    'MQTT_PASSWORD': os.getenv('MQTT_PASSWORD', ''),
    'MQTT_TOPIC': os.getenv('MQTT_TOPIC', 'default/topic'),
    'MQTT_CLIENT_ID': os.getenv('MQTT_CLIENT_ID', 'default_client'),
    'MQTT_QOS': os.getenv('MQTT_QOS', '1'),
    'MQTT_RETAIN': os.getenv('MQTT_RETAIN', '0'),
    # 'MQTT_KEEPALIVE': os.getenv('MQTT_KEEPALIVE', '60'),
    'MQTT_CLEAN_SESSION': os.getenv('MQTT_CLEAN_SESSION', '1'),
    'MQTT_TLS': os.getenv('MQTT_TLS', '0'),
    'MQTT_TLS_INSECURE': os.getenv('MQTT_TLS_INSECURE', '0'),
    'MQTT_TLS_CERT_REQS': os.getenv('MQTT_TLS_CERT_REQS', '0'),
    'MQTT_TLS_VERSION': os.getenv('MQTT_TLS_VERSION', '0')
}

# Append the Macros to the PlatformIO build environment
for key, value in env_vars.items():
    env.Append(CPPDEFINES=[(key, env.StringifyMacro(value))])
    print(f"Processing {key}={value}")
    if not value:
        print(f"Warning: {key} is not set")
    else:
        os.environ[key] = value

# (Optional) List all exported environment variables
for key, value in os.environ.items():
    print(f"±±± Exported Environment variable {key}={value}")