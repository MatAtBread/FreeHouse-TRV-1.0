# TRV-1

This is a full hardware (3D CAD, schematic, PCB) and software (Arduino/ESP32) implementation of a smart TRV. It was inspired by the crappy firmware, apps and hardware design of the graveyard of commercial smart TRVs that I have, that have all failed in various ways - typically poor mechanical design whereby the TRV splits when operating the valve due the stress on the plunger screw. I also find the over/under-shoots of temperature control exceptionally irritating as it kind of makes the commercial offerings pointless if they can't actually control the temperature adequately.

Right now it's a work in progress. It works, but it needs Zigbee fixes (especially HVAC reporting) and the CAD needs more testing to ensure resilience.

I also need to document sources for the external components (6V 60rpm N20 motor, brassware, screws, SEEED XIAO ESP32C6, AA format-LiPo 3.7v battery + holder). Note that the current schematics have a small error that I correct by hand (since I had JLCPCB make 10 of them for me!).