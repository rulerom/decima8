# Decima-8 Tile Visualization Demo

Interactive 3D visualization of a single Decima-8 neuromorphic tile.

## Overview

This demo visualizes one tile from the Decima-8 core as a 3D barrel with:
- **8 VSB input pipes** at the bottom (one per lane)
- **Ring valves** on each pipe (rotate based on VSB value)
- **Central decay pipe** at the middle (drains/fills to zero)
- **Liquid level** showing the accumulator value
- **Color coding**: blue (negative) → green (zero) → yellow/orange/red (positive)

## Quick Start

### Option 1: Local Web Server (Recommended)

Due to ES6 modules, you need to serve the files via HTTP:

```bash
# Using Python
cd js_demo
python -m http.server 8080

# Using Node.js (npx)
npx serve js_demo

# Using PHP
php -S localhost:8080 -t js_demo
```

Then open: http://localhost:8080

### Option 2: VS Code Live Server

1. Install "Live Server" extension
2. Right-click `index.html`
3. Select "Open with Live Server"

## Controls

### Camera
- **Left mouse drag**: Rotate camera around the tile
- **Scroll wheel**: Zoom in/out

### Interactive Panel

#### VSB Inputs (0-15)
Set the input value for each of the 8 lanes:
- Enter values 0-15 for each lane
- Changes apply immediately

#### Weights (-127 to 127)
Set the weight for each lane:
- **Positive weight**: pipe adds to accumulator (green)
- **Negative weight**: pipe subtracts from accumulator (red)
- Enter values -127 to 127 for each lane

#### Tile Parameters
- **thr_lo**: Low threshold for activation
- **thr_hi**: High threshold — tile fires when accumulator exceeds this
- **decay16**: Decay rate (0-65535) — how fast accumulator returns to zero
- **domain**: Domain ID (0-15)

#### Buttons
- **FLASH**: Execute one flash cycle — applies VSB inputs × weights to accumulator
- **RESET**: Set accumulator back to zero

## How It Works

### Flash Cycle

1. **Phase READ**: Read 8 VSB input values (0-15 per lane)
2. **Apply Weights**: Multiply each VSB by its weight, sum all contributions
   ```
   delta = Σ(vsb[i] × weight[i]) for i=0..7
   ```
3. **Phase WRITE**: Add weighted sum to accumulator
   ```
   accumulator += delta
   ```
4. **Apply Decay**: Move accumulator toward zero based on `decay16`
5. **Check Fire**: If `accumulator > thr_hi`, tile fires and resets to zero

### Visual Mapping

| Element | Represents |
|---------|------------|
| Liquid level | Accumulator value |
| Liquid color | Signal polarity (blue=-, green=0, red=+) |
| Pipe color | Weight sign (green=positive, red=negative) |
| Valve rotation | VSB input activity |
| Decay pipe glow | Decay rate |
| Barrel flash | Tile fired |

## File Structure

```
js_demo/
├── index.html      # HTML page with canvas and UI panel
├── style.css       # Styling for UI overlay
├── main.js         # Entry point, animation loop, UI handling
├── tile.js         # Tile simulation model
├── renderer.js     # Three.js 3D renderer
└── README.md       # This file
```

## Technologies

- **Three.js** (r160) - 3D rendering via WebGL
- **ES6 Modules** - Modern JavaScript
- **WebGL** - Hardware-accelerated graphics

## License

Same as Decima-8 Core: Boost Software License - Version 1.0

(c) Decima-8 Core Team / ORDEN (c) 2026
