/**
 * Decima-8 Tile Model
 * 
 * Simulates a single tile with:
 * - 8-lane VSB input with weights
 * - Accumulator with thr_lo/thr_hi thresholds
 * - Decay mechanism
 * - Fire state
 */

export class Tile {
    constructor() {
        // Tile parameters (matching d8_tile_params_t from C code)
        this.accumulator = 0;       // Current accumulator value (-32768 to 32767)
        this.thr_lo = -500;         // Low threshold for activation
        this.thr_hi = 500;          // High threshold for firing
        this.decay16 = 100;         // Decay rate (0-65535)
        this.domain_id = 0;         // Domain ID (0-15)
        this.priority = 0;          // Priority (0-255)
        
        // VSB weights for 8 lanes (-128 to 127)
        // Positive = adds to accumulator, negative = subtracts
        this.weights = new Int8Array(8).fill(0);
        
        // Routing masks for neighbors (N/E/S/W/NE/SE/SW/NW)
        this.routingMasks = 0;
        
        // State
        this.fired = false;         // Whether tile fired this cycle
        this.phase = 'idle';        // Current phase: idle, read, write
        
        // Initialize with default weights for demo
        this._initDefaultWeights();
    }
    
    _initDefaultWeights() {
        // Set up some interesting default weights for demo
        this.weights[0] = 64;   // Lane 0: strong positive
        this.weights[1] = 32;   // Lane 1: moderate positive
        this.weights[2] = -48;  // Lane 2: moderate negative
        this.weights[3] = 16;   // Lane 3: weak positive
        this.weights[4] = -32;  // Lane 4: moderate negative
        this.weights[5] = 48;   // Lane 5: moderate positive
        this.weights[6] = -16;  // Lane 6: weak negative
        this.weights[7] = 80;   // Lane 7: strong positive
    }
    
    /**
     * Run one simulation step
     * @param {Uint8Array} vsb - 8-element array with VSB input values (0-15 per lane)
     * @returns {Object} Step result with fire status and new accumulator value
     */
    step(vsb) {
        if (!vsb || vsb.length !== 8) {
            throw new Error('VSB must be 8-element array');
        }
        
        // Check if in LOCK state (accumulator between thr_lo and thr_hi)
        const inLock = this.accumulator >= this.thr_lo && this.accumulator <= this.thr_hi;
        
        // Phase READ: Read VSB inputs and apply weights
        this.phase = 'read';
        
        let weightedSum = 0;
        
        // Only accumulate if NOT in lock state
        if (!inLock) {
            for (let i = 0; i < 8; i++) {
                // Weight = signed value, VSB = unsigned 0-15
                // Contribution = weight * vsb[i]
                weightedSum += this.weights[i] * vsb[i];
            }
        }
        
        // Phase WRITE: Update accumulator
        this.phase = 'write';
        
        // Add weighted sum to accumulator (only if not in lock)
        this.accumulator += weightedSum;
        
        // Clamp accumulator to int16 range
        this.accumulator = Math.max(-32768, Math.min(32767, this.accumulator));
        
        // Apply decay: move accumulator toward zero (always applies, even in lock)
        this._applyDecay();
        
        // Check fire condition (only fires when exiting lock zone upward)
        this.fired = !inLock && this.accumulator > this.thr_hi;
        
        // Reset if fired
        if (this.fired) {
            this.accumulator = 0;
        }
        
        this.phase = 'idle';
        
        return {
            fired: this.fired,
            accumulator: this.accumulator,
            weightedSum: weightedSum,
            inLock: inLock
        };
    }
    
    _applyDecay() {
        // Decay rate: 0 = no decay, 65535 = instant decay to zero
        // decay16 is a 16-bit fixed point value
        if (this.decay16 === 0) {
            return; // No decay
        }
        
        // Calculate decay factor (0.0 to 1.0)
        const decayFactor = this.decay16 / 65536.0;
        
        // Move accumulator toward zero
        if (this.accumulator > 0) {
            this.accumulator = Math.max(0, this.accumulator - Math.ceil(this.accumulator * decayFactor));
        } else if (this.accumulator < 0) {
            this.accumulator = Math.min(0, this.accumulator + Math.ceil(Math.abs(this.accumulator) * decayFactor));
        }
    }
    
    /**
     * Set tile parameters
     */
    setParams(params) {
        if (params.thr_lo !== undefined) this.thr_lo = params.thr_lo;
        if (params.thr_hi !== undefined) this.thr_hi = params.thr_hi;
        if (params.decay16 !== undefined) this.decay16 = params.decay16;
        if (params.domain_id !== undefined) this.domain_id = params.domain_id;
        if (params.priority !== undefined) this.priority = params.priority;
        if (params.accumulator !== undefined) this.accumulator = params.accumulator;
    }
    
    /**
     * Set weights for all 8 lanes
     */
    setWeights(weights) {
        if (!weights || weights.length !== 8) {
            throw new Error('Weights must be 8-element array');
        }
        for (let i = 0; i < 8; i++) {
            this.weights[i] = weights[i];
        }
    }
    
    /**
     * Get current state for rendering
     */
    getState() {
        const inLock = this.accumulator >= this.thr_lo && this.accumulator <= this.thr_hi;
        return {
            accumulator: this.accumulator,
            thr_lo: this.thr_lo,
            thr_hi: this.thr_hi,
            decay16: this.decay16,
            domain_id: this.domain_id,
            priority: this.priority,
            weights: Array.from(this.weights),
            fired: this.fired,
            inLock: inLock,
            phase: this.phase
        };
    }
}

/**
 * Generate test VSB pattern
 * @returns {Uint8Array} 8-element VSB array
 */
export function generateTestVSB(time) {
    const vsb = new Uint8Array(8);
    
    // Generate interesting patterns based on time
    const t = time * 0.001; // Convert to seconds
    
    vsb[0] = Math.floor((Math.sin(t * 2) + 1) * 7.5);      // Slow sine
    vsb[1] = Math.floor((Math.cos(t * 3) + 1) * 7.5);      // Faster cosine
    vsb[2] = Math.floor((Math.sin(t * 5 + 1) + 1) * 7.5);  // Even faster
    vsb[3] = Math.floor((Math.sin(t * 1.5 + 2) + 1) * 7.5);
    vsb[4] = Math.floor((Math.cos(t * 4 + 0.5) + 1) * 7.5);
    vsb[5] = Math.floor((Math.sin(t * 2.5 + 1.5) + 1) * 7.5);
    vsb[6] = Math.floor((Math.cos(t * 3.5 + 2) + 1) * 7.5);
    vsb[7] = Math.floor((Math.sin(t * 4.5 + 0.5) + 1) * 7.5);
    
    return vsb;
}
