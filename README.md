# Tick-Tac with LVGL

This project is a Tick-Tac game built with [LVGL](https://lvgl.io/) - a lightweight and versatile graphics library for embedded systems.

## Project Structure

```
.
├── src/
│   └── main.c              # Main application entry point
├── lv_conf.h               # LVGL configuration file
├── CMakeLists.txt          # CMake build configuration
├── package.json            # Project metadata
├── .lvglproject            # LVGL web editor project file
└── README.md               # This file
```

## Prerequisites

- CMake 3.16 or later
- C compiler (GCC, Clang, or MSVC)
- LVGL library (v9.6.0 or compatible)

## Setup

1. Clone the LVGL repository as a subdirectory (or install it as a dependency):
```bash
cd Tick-Tac
git clone https://github.com/lvgl/lvgl.git
```

2. Create a build directory and configure the project:
```bash
cmake -B build
```

3. Build the project:
```bash
cmake --build build
```

## LVGL Web Editor

This project is compatible with the [LVGL web editor](https://lvgl.io/tools/online-editor). To use it:

1. Open the LVGL web editor in your browser
2. Import this project by uploading the `.lvglproject` file
3. Edit your UI visually and export the generated code

## Configuration

Key LVGL settings can be modified in `lv_conf.h`:

- **LV_COLOR_DEPTH**: Color depth (16 bits by default)
- **LV_DPI_DEF**: Default DPI for responsive sizing
- **LV_DISPLAY_***: Display-related settings

Adjust display dimensions in `package.json` and `.lvglproject` to match your target hardware.

## License

MIT
