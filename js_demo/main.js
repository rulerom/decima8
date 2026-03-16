/**
 * Decima-8 Tile Demo - Main Entry Point
 * 
 * Interactive mode with manual VSB/weights control and FLASH/RESET buttons.
 */

import { Tile } from './tile.js';
import { TileRenderer } from './renderer.js';

// Global state
let tile = null;
let renderer = null;
let lastTime = 0;

// UI elements
const uiElements = {};

// Current VSB and weights values
let currentVSB = new Uint8Array(8).fill(0);
let currentWeights = new Int8Array([0, 10, 20, 40, -40, -20, -10, 80]);

/**
 * Check if accumulator is in lock state (between thr_lo and thr_hi)
 */
function isLocked(accumulator, thr_lo, thr_hi) {
    return accumulator >= thr_lo && accumulator <= thr_hi;
}

/**
 * Initialize UI element references
 */
function initUI() {
    // VSB inputs
    uiElements.vsbContainer = document.getElementById('vsb-inputs');
    uiElements.vsbInputs = [];
    for (let i = 0; i < 8; i++) {
        const group = document.createElement('div');
        group.className = 'input-group';
        
        const label = document.createElement('label');
        label.textContent = `Lane ${i}`;
        
        const input = document.createElement('input');
        input.type = 'number';
        input.min = '0';
        input.max = '15';
        input.value = '0';
        input.id = `vsb-input-${i}`;
        
        input.addEventListener('change', (e) => {
            let val = parseInt(e.target.value) || 0;
            val = Math.max(0, Math.min(15, val));
            currentVSB[i] = val;
            e.target.value = val;
            updateRendererWeights();
        });
        
        group.appendChild(label);
        group.appendChild(input);
        uiElements.vsbContainer.appendChild(group);
        uiElements.vsbInputs.push(input);
    }
    
    // Weight inputs
    uiElements.weightContainer = document.getElementById('weight-inputs');
    uiElements.weightInputs = [];
    for (let i = 0; i < 8; i++) {
        const group = document.createElement('div');
        group.className = 'input-group';
        
        const label = document.createElement('label');
        label.textContent = `W${i}`;
        
        const input = document.createElement('input');
        input.type = 'number';
        input.min = '-127';
        input.max = '127';
        input.value = currentWeights[i].toString();
        input.id = `weight-input-${i}`;
        
        input.addEventListener('change', (e) => {
            let val = parseInt(e.target.value) || 0;
            val = Math.max(-127, Math.min(127, val));
            currentWeights[i] = val;
            e.target.value = val;
            tile.setWeights(Array.from(currentWeights));
            updateRendererWeights();
        });
        
        group.appendChild(label);
        group.appendChild(input);
        uiElements.weightContainer.appendChild(group);
        uiElements.weightInputs.push(input);
    }
    
    // Tile parameters
    uiElements.thrLo = document.getElementById('thr-lo-input');
    uiElements.thrHi = document.getElementById('thr-hi-input');
    uiElements.decay = document.getElementById('decay-input');
    uiElements.domain = document.getElementById('domain-input');
    
    // Parameter change handlers
    uiElements.thrLo.addEventListener('change', () => {
        tile.setParams({ thr_lo: parseInt(uiElements.thrLo.value) || 0 });
    });
    
    uiElements.thrHi.addEventListener('change', () => {
        tile.setParams({ thr_hi: parseInt(uiElements.thrHi.value) || 0 });
    });
    
    uiElements.decay.addEventListener('change', () => {
        tile.setParams({ decay16: parseInt(uiElements.decay.value) || 0 });
    });
    
    uiElements.domain.addEventListener('change', () => {
        tile.setParams({ domain_id: parseInt(uiElements.domain.value) || 0 });
    });
    
    // Status displays
    uiElements.accumulator = document.getElementById('accumulator-value');
    uiElements.lock = document.getElementById('lock-value');
    uiElements.phase = document.getElementById('phase-value');
    uiElements.fire = document.getElementById('fire-value');
    
    // Buttons
    document.getElementById('flash-btn').addEventListener('click', doFlash);
    document.getElementById('reset-btn').addEventListener('click', doReset);
}

/**
 * Update renderer with current weights
 */
function updateRendererWeights() {
    // Just trigger a visual update by setting weights again
    tile.setWeights(Array.from(currentWeights));
}

/**
 * Perform FLASH cycle
 */
function doFlash() {
    // Run tile simulation step with current VSB values
    const result = tile.step(currentVSB);
    
    // Update UI
    updateUI(tile.getState());
    
    // Force renderer update
    renderer.update(tile.getState(), currentVSB, 0.016);
    renderer.render();
    
    console.log('FLASH:', result);
}

/**
 * RESET accumulator to zero
 */
function doReset() {
    tile.setParams({ accumulator: 0 });
    tile.fired = false;
    
    // Update UI
    updateUI(tile.getState());
    
    // Force renderer update
    renderer.update(tile.getState(), currentVSB, 0.016);
    renderer.render();
    
    console.log('RESET: accumulator = 0');
}

/**
 * Update UI with current tile state
 */
function updateUI(tileState) {
    // Update accumulator
    uiElements.accumulator.textContent = tileState.accumulator;

    // Update lock status (use tileState.inLock directly)
    uiElements.lock.textContent = tileState.inLock ? 'YES' : 'NO';
    uiElements.lock.className = tileState.inLock ? 'lock-yes' : 'lock-no';

    // Update phase with color
    uiElements.phase.textContent = tileState.phase;
    uiElements.phase.style.color =
        tileState.phase === 'read' ? '#4aff4a' :
        tileState.phase === 'write' ? '#ffff4a' : '#aaaaff';

    // Update fire status
    uiElements.fire.textContent = tileState.fired ? 'YES' : 'NO';
    uiElements.fire.className = tileState.fired ? 'fire-yes' : 'fire-no';

    // Sync input values with tile state
    uiElements.thrLo.value = tileState.thr_lo;
    uiElements.thrHi.value = tileState.thr_hi;
    uiElements.decay.value = tileState.decay16;
    uiElements.domain.value = tileState.domain_id;
}

/**
 * Animation loop (for visual updates only)
 */
function animate(currentTime) {
    requestAnimationFrame(animate);
    
    // Calculate delta time
    const deltaTime = (currentTime - lastTime) / 1000;
    lastTime = currentTime;
    
    // Update renderer with current state (for animations)
    const tileState = tile.getState();
    renderer.update(tileState, currentVSB, deltaTime);
    renderer.render();
}

/**
 * Initialize the demo
 */
function init() {
    console.log('Decima-8 Tile Demo - Initializing...');
    
    // Get canvas
    const canvas = document.getElementById('glcanvas');
    if (!canvas) {
        console.error('Canvas not found!');
        return;
    }
    
    // Initialize UI
    initUI();
    
    // Create tile
    tile = new Tile();
    
    // Set initial weights from our array
    tile.setWeights(Array.from(currentWeights));
    
    // Configure tile with demo parameters
    tile.setParams({
        thr_lo: -2000,
        thr_hi: 3000,
        decay16: 0,  // No auto-decay in manual mode
        accumulator: 0
    });
    
    // Create renderer
    renderer = new TileRenderer(canvas);
    
    // Initial UI update
    updateUI(tile.getState());
    
    // Start animation loop
    lastTime = performance.now();
    requestAnimationFrame(animate);
    
    console.log('Decima-8 Tile Demo - Running!');
    console.log('Set VSB values and weights, then click FLASH');
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
