

# ESP32 Neopixel Chaser Controler

An ESP32-powered light controller for WS2812/Neopixel strips. At its core are multiple independent "chasers": animation tracks that move across the strip, each carrying its own six-color palette, range of LEDs, speed and delay, repeat interval, and blending mode. By layering these chasers, the controller produces complex, dynamic lighting effects that go far beyond a single pattern. Configuration is flexible, users can adjust everything through a Wi-Fi web interface, or push new settings directly via HTTP POST.

# Configure

- Create `config.h` in `main` folder with the following:

>     #define CONFIG_WIFI_SSID "SSID Name"
>     #define CONFIG_WIFI_PASSWORD "Password"
>     #define CONFIG_LED_GPIO 27
>     #define CONFIG_LED_COUNT 128

- This should all be self-explanatory.

# Build Steps

- `cd html` then `bash -c ./compile.sh` — to compile the javascript, and minify the css.
-  Alternatively, you can create your own `j.js` and `s.css` files that the IDF will use.
- `idf.py build` or `idf.py flash` to build or flash the ESP32

# Usage

- Once it's running, navigate a browser to http address of your ESP32 to configure it, or send the binary configuration to it using an HTTP POST. The configuration data is just 34 bytes per chaser.
- If you have any computer on your network, you can schedule a cron job to send a pre-formed HTTP POST with the configuration at given times so have the Chaser Controller change its configuration.
   
# Local Editing HTML

- Go into the `html` folder and launch your favorite http/https server to [serve](https://github.com/E4/Serve) the pages locally.
- Point your browser to localhost, and you can see/edit all the HTML, Javascript and CSS right there.


# Screenshot

![Screenshot](/docs/screenshot.png?raw=true)