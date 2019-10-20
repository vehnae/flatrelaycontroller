# IP Switch / Alnitak flat panel emulation firmware for KMTronic DINo board

This firmware implements a combined IP switch functionality along with Alnitak flat panel compatible serial interface.

## Alnitak flat panel

The device is visible as an USB serial interface. Point your Alnitak flat panel -compatible application (e.g. SGP) to this serial port.

## HTTP API

The individual outputs can be toggled by sending a POST request to `http://your.ip/relay/1/on` or `http://your.ip/relay/1/off` where `1` is the relay number (1 to 4). Relays can be pulsed for 1000ms by calling `http://your.ip/relay/1/pulse`. 

## Customizing the HTML page

The HTML page is embedded in the firmware. You probably need to customize that, atleast for the descriptions. Edit the `index.html` and then run `make` inside the `src` directory to regenerate the header file.

## Authorization

If you need to have authentication set a base64-encoded `user:password` string into the `AUTH_PASSWORD` definition in `config.h`.
