# MM32F0020 UART IO Expander

Firmware for an MM32F0020 used as a UART I/O expander.

- `IOX_F0020/`: firmware sources (`src/`), Keil project (`KEILPRJ/iox_f0020.uvprojx`), host-side driver (`host/`), protocol doc (`PROTOCOL.md`) and Python test tools (`tools/`)
- `LIB_Files/MM32F0020/`: MindMotion HAL, CMSIS and startup files
- `SYSTEM/`, `HARDWARE/`: shared UART, WDG, delay, GPIO and LED drivers used by the project
- `mm32f0020_uart_io_expander_spec.md`: design spec

Open `IOX_F0020/KEILPRJ/iox_f0020.uvprojx` in Keil MDK to build.
