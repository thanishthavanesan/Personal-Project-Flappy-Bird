# Personal-Project-Flappy-Bird
A custom-built version of Flappy Bird running entirely on an ESP32 — no game engine, no shortcuts, just hardware. It plays on a small OLED screen, keeps score on a separate 4-digit display (driven through a shift register), and you control it with a joystick plus three buttons for flap, pause, and quit.

Most of the work was in the debugging. The score display flickered until I figured out it needed its own hardware timer running in the background, independent of everything else the chip was doing. Scoring itself was buggy too — it kept missing points because it was checking for one exact pixel position instead of just checking whether the pipe had passed. And the shift register wiring didn't match any online tutorial, so I had to test it pin-by-pin myself to figure out the actual order.

Once the breadboard version worked, I also designed and routed a proper PCB for it in KiCad, going through schematic capture, matching footprints, and cleaning it up until it passed all design checks.

Built with: C/C++ (Arduino), ESP32 (GPIO/ADC/I2C), hardware timers, shift register multiplexing, SSD1306 OLED, KiCad.
 
 
** KiCad files are named under "Hobbies" folder and .ino file is named under "ThanishThavanesan_flappy_bird **
