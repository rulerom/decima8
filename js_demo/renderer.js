/**
 * Decima-8 Tile 3D Renderer
 * 
 * Renders a single tile as a 3D barrel with:
 * - 8 VSB pipes at the bottom with valves
 * - Central decay pipe
 * - Liquid level showing accumulator value
 */

import * as THREE from 'three';

export class TileRenderer {
    constructor(canvas) {
        this.canvas = canvas;
        
        // Scene setup
        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x0a0a0f);
        
        // Camera
        this.camera = new THREE.PerspectiveCamera(
            60,
            canvas.clientWidth / canvas.clientHeight,
            0.1,
            1000
        );
        this.camera.position.set(3, 4, 5);  // Higher camera position
        this.camera.lookAt(0, 2.3, 0);  // Look higher (adjusted for scene raise)
        
        // Mouse control state
        this.isDragging = false;
        this.previousMousePosition = { x: 0, y: 0 };
        this.cameraAngle = { x: Math.PI / 6, y: Math.PI / 4 };
        this.cameraDistance = 6;
        
        // Setup mouse controls
        this._setupMouseControls();
        
        // Renderer
        this.renderer = new THREE.WebGLRenderer({ 
            canvas, 
            antialias: true,
            alpha: true
        });
        this.renderer.setSize(canvas.clientWidth, canvas.clientHeight);
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        
        // Lighting
        this._setupLighting();
        
        // Tile components
        this.barrel = null;
        this.liquid = null;
        this.pipes = [];
        this.valves = [];
        this.decayPipe = null;
        this.particles = [];
        
        // Build the tile visualization
        this._createBarrel();
        this._createPipes();
        this._createDecayPipe();
        this._createParticles();
        
        // Raise entire scene so barrel is higher on mobile
        this.scene.position.y = 0.8;
        
        // Animation state
        this.time = 0;
        this.currentPhase = 'idle';
        
        // Handle resize
        window.addEventListener('resize', () => this._onResize());
    }
    
    _setupMouseControls() {
        const canvas = this.canvas;
        
        canvas.addEventListener('mousedown', (e) => {
            this.isDragging = true;
            this.previousMousePosition = { x: e.clientX, y: e.clientY };
        });
        
        canvas.addEventListener('mousemove', (e) => {
            if (!this.isDragging) return;
            
            const deltaX = e.clientX - this.previousMousePosition.x;
            const deltaY = e.clientY - this.previousMousePosition.y;
            
            this.cameraAngle.y += deltaX * 0.01;
            this.cameraAngle.x += deltaY * 0.01;
            
            // Clamp vertical angle
            this.cameraAngle.x = Math.max(-Math.PI / 2 + 0.1, Math.min(Math.PI / 2 - 0.1, this.cameraAngle.x));
            
            this._updateCameraPosition();
            
            this.previousMousePosition = { x: e.clientX, y: e.clientY };
        });
        
        canvas.addEventListener('mouseup', () => {
            this.isDragging = false;
        });
        
        canvas.addEventListener('mouseleave', () => {
            this.isDragging = false;
        });
        
        canvas.addEventListener('wheel', (e) => {
            e.preventDefault();
            this.cameraDistance += e.deltaY * 0.01;
            this.cameraDistance = Math.max(3, Math.min(15, this.cameraDistance));
            this._updateCameraPosition();
        }, { passive: false });
        
        // Initial camera position
        this._updateCameraPosition();
    }
    
    _updateCameraPosition() {
        this.camera.position.x = this.cameraDistance * Math.cos(this.cameraAngle.x) * Math.sin(this.cameraAngle.y);
        this.camera.position.y = this.cameraDistance * Math.sin(this.cameraAngle.x) + 1;
        this.camera.position.z = this.cameraDistance * Math.cos(this.cameraAngle.x) * Math.cos(this.cameraAngle.y);
        this.camera.lookAt(0, 1, 0);
    }
    
    _setupLighting() {
        // Ambient light
        const ambient = new THREE.AmbientLight(0x404060, 0.5);
        this.scene.add(ambient);
        
        // Main directional light
        const dirLight = new THREE.DirectionalLight(0xffffff, 1);
        dirLight.position.set(3, 5, 3);
        this.scene.add(dirLight);
        
        // Blue rim light
        const rimLight = new THREE.DirectionalLight(0x4040ff, 0.5);
        rimLight.position.set(-3, 2, -3);
        this.scene.add(rimLight);
        
        // Point light near barrel
        const pointLight = new THREE.PointLight(0x7a7aff, 0.8, 10);
        pointLight.position.set(0, 1, 2);
        this.scene.add(pointLight);
    }
    
    _createBarrel() {
        const barrelGroup = new THREE.Group();
        
        // Glass barrel (cylinder) - height 2.5, centered at y=1.25 (bottom at y=0)
        const barrelGeometry = new THREE.CylinderGeometry(1, 1, 2.5, 32, 1, true);
        const barrelMaterial = new THREE.MeshPhysicalMaterial({
            color: 0x88aaff,
            transparent: true,
            opacity: 0.15,
            side: THREE.DoubleSide,
            roughness: 0.1,
            metalness: 0.1,
            transmission: 0.9,
            thickness: 0.5
        });
        this.barrel = new THREE.Mesh(barrelGeometry, barrelMaterial);
        this.barrel.position.y = 1.25;
        barrelGroup.add(this.barrel);
        
        // Barrel rims (top and bottom)
        const rimGeometry = new THREE.TorusGeometry(1.02, 0.05, 16, 32);
        const rimMaterial = new THREE.MeshStandardMaterial({
            color: 0x555577,
            metalness: 0.8,
            roughness: 0.2
        });
        
        const topRim = new THREE.Mesh(rimGeometry, rimMaterial);
        topRim.position.y = 2.5;
        topRim.rotation.x = Math.PI / 2;
        barrelGroup.add(topRim);
        
        const bottomRim = new THREE.Mesh(rimGeometry, rimMaterial);
        bottomRim.position.y = 0;
        bottomRim.rotation.x = Math.PI / 2;
        barrelGroup.add(bottomRim);
        
        // Liquid inside barrel - cylinder with full barrel height, scaled to show level
        // Radius matches barrel inner radius (no gap)
        const liquidGeometry = new THREE.CylinderGeometry(1.0, 1.0, 2.5, 32);
        const liquidMaterial = new THREE.MeshPhysicalMaterial({
            color: 0x00aaff,  // Blue/cyan instead of green
            transparent: true,
            opacity: 0.8,
            roughness: 0.1,
            metalness: 0.0,
            transmission: 0.3
        });
        this.liquid = new THREE.Mesh(liquidGeometry, liquidMaterial);
        this.liquid.position.y = 1.25;  // Center of full-height cylinder
        this.liquid.scale.y = 0.5;  // Start at middle (zero level)
        barrelGroup.add(this.liquid);
        
        // Measurement marks on barrel (including thr_lo/thr_hi)
        this._createMeasurementMarks(barrelGroup);
        
        this.scene.add(barrelGroup);
        this.barrelGroup = barrelGroup;
    }
    
    _createMeasurementMarks(barrelGroup) {
        // Percentage marks (white/gray)
        const markMaterial = new THREE.MeshBasicMaterial({ 
            color: 0x666688,
            transparent: true,
            opacity: 0.7
        });
        
        // Create horizontal marks at different heights (0%, 25%, 50%, 75%, 100%)
        const heights = [0, 0.625, 1.25, 1.875, 2.5];
        
        for (let i = 0; i < heights.length; i++) {
            const markGeometry = new THREE.RingGeometry(1.0, 1.03, 32, 1, 0, Math.PI * 2);
            const mark = new THREE.Mesh(markGeometry, markMaterial);
            mark.position.y = heights[i];
            mark.rotation.x = Math.PI / 2;
            barrelGroup.add(mark);
        }
        
        // thr_lo marker (red ring)
        const thrLoGeometry = new THREE.TorusGeometry(1.05, 0.03, 8, 32);
        const thrLoMaterial = new THREE.MeshBasicMaterial({ 
            color: 0xff4444,
            transparent: true,
            opacity: 0.8
        });
        this.thrLoMarker = new THREE.Mesh(thrLoGeometry, thrLoMaterial);
        this.thrLoMarker.position.y = 1.25;  // Will be updated based on thr_lo
        this.thrLoMarker.rotation.x = Math.PI / 2;
        barrelGroup.add(this.thrLoMarker);
        
        // thr_hi marker (green ring)
        const thrHiGeometry = new THREE.TorusGeometry(1.05, 0.03, 8, 32);
        const thrHiMaterial = new THREE.MeshBasicMaterial({ 
            color: 0x44ff44,
            transparent: true,
            opacity: 0.8
        });
        this.thrHiMarker = new THREE.Mesh(thrHiGeometry, thrHiMaterial);
        this.thrHiMarker.position.y = 1.25;  // Will be updated based on thr_hi
        this.thrHiMarker.rotation.x = Math.PI / 2;
        barrelGroup.add(this.thrHiMarker);
        
        // Lock zone (semi-transparent cylinder between thr_lo and thr_hi)
        const lockZoneGeometry = new THREE.CylinderGeometry(1.0, 1.0, 0.1, 32, 1, true);
        const lockZoneMaterial = new THREE.MeshBasicMaterial({ 
            color: 0x00aaff,  // Blue to match liquid
            transparent: true,
            opacity: 0.15,
            side: THREE.DoubleSide
        });
        this.lockZone = new THREE.Mesh(lockZoneGeometry, lockZoneMaterial);
        this.lockZone.position.y = 1.25;  // Will be updated
        this.lockZone.scale.y = 0;  // Will be updated
        barrelGroup.add(this.lockZone);
    }
    
    _createPipes() {
        const pipeGroup = new THREE.Group();

        // 8 pipes arranged in a circle under the barrel
        const barrelRadius = 1.0;
        const pipeRadius = 0.035;  // Smaller radius pipes
        // Pipes positioned well inside barrel radius so always covered by liquid
        const pipeOffset = barrelRadius * 0.6;  // 0.6 - pipes near center, always under liquid
        const pipeLength = 0.8;

        for (let i = 0; i < 8; i++) {
            const angle = (i / 8) * Math.PI * 2;
            const x = Math.cos(angle) * pipeOffset;
            const z = Math.sin(angle) * pipeOffset;

            // Pipe: vertical cylinder from y=0 up to barrel bottom (y=0)
            const pipeGeometry = new THREE.CylinderGeometry(pipeRadius, pipeRadius, pipeLength, 16);

            // Color based on weight sign (will be updated later)
            const pipeMaterial = new THREE.MeshStandardMaterial({
                color: i % 2 === 0 ? 0x44ff44 : 0xff4444,
                metalness: 0.6,
                roughness: 0.3
            });

            const pipe = new THREE.Mesh(pipeGeometry, pipeMaterial);

            // Position: pipe goes from y=-pipeLength to y=0 (barrel bottom)
            pipe.position.set(x, -pipeLength / 2, z);

            pipeGroup.add(pipe);
            this.pipes.push(pipe);

            // Valve: torus (ring valve) on each pipe, positioned along the pipe
            const valveGeometry = new THREE.TorusGeometry(0.08, 0.03, 8, 16);
            const valveMaterial = new THREE.MeshStandardMaterial({
                color: 0x8888aa,
                metalness: 0.7,
                roughness: 0.2
            });

            const valve = new THREE.Mesh(valveGeometry, valveMaterial);
            valve.position.set(x, -pipeLength * 0.4, z);
            valve.rotation.x = Math.PI / 2;

            pipeGroup.add(valve);
            this.valves.push(valve);
        }

        this.scene.add(pipeGroup);
        this.pipeGroup = pipeGroup;
    }
    
    _createDecayPipe() {
        const decayGroup = new THREE.Group();
        
        // Decay pipe: vertical pipe on the SIDE of the barrel (outside)
        const pipeRadius = 0.12;
        const pipeHeight = 2.0;
        const pipeOffset = 1.3;  // Distance from center (outside barrel)
        
        const pipeGeometry = new THREE.CylinderGeometry(pipeRadius, pipeRadius, pipeHeight, 16);
        const pipeMaterial = new THREE.MeshStandardMaterial({
            color: 0x6666aa,
            metalness: 0.5,
            roughness: 0.3,
            transparent: true,
            opacity: 0.8
        });
        
        this.decayPipe = new THREE.Mesh(pipeGeometry, pipeMaterial);
        this.decayPipe.position.set(pipeOffset, pipeHeight / 2, 0);
        decayGroup.add(this.decayPipe);
        
        // Connection to barrel (horizontal pipe)
        const connGeometry = new THREE.CylinderGeometry(pipeRadius * 0.8, pipeRadius * 0.8, pipeOffset - 1.0, 16);
        const connPipe = new THREE.Mesh(connGeometry, pipeMaterial);
        connPipe.position.set((pipeOffset + 1.0) / 2, 1.25, 0);
        connPipe.rotation.z = Math.PI / 2;
        decayGroup.add(connPipe);
        
        // Decay valve (larger) on vertical pipe
        const valveGeometry = new THREE.TorusGeometry(0.2, 0.06, 8, 16);
        const valveMaterial = new THREE.MeshStandardMaterial({
            color: 0x555577,
            metalness: 0.6,
            roughness: 0.3
        });
        
        const decayValve = new THREE.Mesh(valveGeometry, valveMaterial);
        decayValve.position.set(pipeOffset, 1.5, 0);
        decayValve.rotation.x = Math.PI / 2;
        decayGroup.add(decayValve);
        
        this.decayValve = decayValve;
        
        // Flow indicator (ring that pulses when decay is active)
        const ringGeometry = new THREE.TorusGeometry(0.25, 0.04, 8, 32);
        const ringMaterial = new THREE.MeshBasicMaterial({
            color: 0x00ffff,
            transparent: true,
            opacity: 0.5
        });
        
        const flowRing = new THREE.Mesh(ringGeometry, ringMaterial);
        flowRing.position.set(pipeOffset, 1.5, 0);
        flowRing.rotation.x = Math.PI / 2;
        decayGroup.add(flowRing);
        
        this.flowRing = flowRing;
        
        this.scene.add(decayGroup);
        this.decayGroup = decayGroup;
    }
    
    _createParticles() {
        // Small particles floating in the liquid
        const particleGeometry = new THREE.SphereGeometry(0.03, 8, 8);
        const particleMaterial = new THREE.MeshBasicMaterial({
            color: 0xaaffff,
            transparent: true,
            opacity: 0.6
        });
        
        for (let i = 0; i < 20; i++) {
            const particle = new THREE.Mesh(particleGeometry, particleMaterial);
            particle.position.set(
                (Math.random() - 0.5) * 1.5,
                0.5 + Math.random() * 1.5,
                (Math.random() - 0.5) * 1.5
            );
            particle.userData = {
                baseY: particle.position.y,
                speed: 0.5 + Math.random() * 0.5,
                offset: Math.random() * Math.PI * 2
            };
            this.particles.push(particle);
            this.barrelGroup.add(particle);
        }
    }
    
    /**
     * Update visualization based on tile state
     */
    update(tileState, vsbInputs, deltaTime) {
        this.time += deltaTime;
        this.currentPhase = tileState.phase;
        
        // Update liquid level based on accumulator
        this._updateLiquidLevel(tileState.accumulator);
        
        // Update threshold markers (thr_lo, thr_hi)
        this._updateThresholdMarkers(tileState.thr_lo, tileState.thr_hi);
        
        // Update liquid color based on value and fire state
        this._updateLiquidColor(tileState.accumulator, tileState.fired);
        
        // Update pipe colors based on weights
        this._updatePipeColors(tileState.weights);
        
        // Animate valves based on VSB input
        this._updateValves(vsbInputs);
        
        // Update decay pipe visibility
        this._updateDecayPipe(tileState.decay16);
        
        // Animate particles
        this._animateParticles(deltaTime);

        // Pulse flow ring when decay is active
        this._animateFlowRing(tileState.decay16, deltaTime);
        
        // Flash barrel when fired
        if (tileState.fired) {
            this._flashBarrel();
        }
    }
    
    _updateLiquidLevel(accumulator) {
        // Map accumulator (-32768 to 32767) to liquid level
        // Zero accumulator = middle of barrel (y=1.25)
        // Max positive = top of barrel (y=2.5)
        // Max negative = bottom of barrel (y=0)
        
        const maxRange = 32768;
        const barrelHeight = 2.5;
        const zeroLevel = barrelHeight / 2;  // 1.25 (middle)
        
        // Calculate liquid height from bottom (0 to barrelHeight)
        let liquidHeight;
        if (accumulator >= 0) {
            // Positive: liquid level from middle to top
            const normalized = Math.min(accumulator / maxRange, 1);
            liquidHeight = zeroLevel + normalized * zeroLevel;
        } else {
            // Negative: liquid level from bottom to middle
            const normalized = Math.min(Math.abs(accumulator) / maxRange, 1);
            liquidHeight = zeroLevel - normalized * zeroLevel;
        }
        
        // Scale Y: liquidHeight / barrelHeight
        // The liquid mesh has height = barrelHeight, so scale.y = liquidHeight/barrelHeight
        const scaleY = Math.max(0.02, liquidHeight / barrelHeight);
        
        // Position: the scaled cylinder's center should be at liquidHeight/2
        // Since original cylinder is centered at 1.25 with height 2.5,
        // after scaling to height liquidHeight, center should be at liquidHeight/2
        this.liquid.scale.y = scaleY;
        this.liquid.scale.x = 0.95;
        this.liquid.scale.z = 0.95;
        
        // Position Y: center of the scaled cylinder
        this.liquid.position.y = liquidHeight / 2;
    }
    
    _updateThresholdMarkers(thr_lo, thr_hi) {
        // Map thresholds to barrel height positions
        const maxRange = 32768;
        const barrelHeight = 2.5;
        
        // thr_lo position
        const thrLoNorm = Math.max(-1, Math.min(1, thr_lo / maxRange));
        const thrLoY = (thrLoNorm + 1) / 2 * barrelHeight;
        this.thrLoMarker.position.y = thrLoY;
        
        // thr_hi position
        const thrHiNorm = Math.max(-1, Math.min(1, thr_hi / maxRange));
        const thrHiY = (thrHiNorm + 1) / 2 * barrelHeight;
        this.thrHiMarker.position.y = thrHiY;
        
        // Lock zone: cylinder between thr_lo and thr_hi
        const lockZoneCenter = (thrLoY + thrHiY) / 2;
        const lockZoneHeight = Math.abs(thrHiY - thrLoY);
        const lockZoneScaleY = lockZoneHeight / barrelHeight;
        
        this.lockZone.position.y = lockZoneCenter;
        this.lockZone.scale.y = Math.max(0.01, lockZoneScaleY);
    }
    
    _updateLiquidColor(accumulator, fired) {
        // Color based on accumulator value
        // Negative: dark blue, Zero: cyan, Positive: light blue/white
        
        let r, g, b;
        const normalized = accumulator / 32768;
        
        if (fired) {
            // Bright white when fired
            r = 1;
            g = 1;
            b = 1;
        } else if (normalized < 0) {
            // Dark blue to cyan for negative values
            r = 0;
            g = 0.5 + (1 - Math.abs(normalized)) * 0.5;
            b = 1;
        } else if (normalized < 0.5) {
            // Cyan to light blue
            r = normalized * 0.5;
            g = 0.8 + normalized * 0.2;
            b = 1;
        } else {
            // Light blue to white
            r = 0.5 + normalized * 0.5;
            g = 0.8 + (1 - normalized) * 0.2;
            b = 1;
        }
        
        this.liquid.material.color.setRGB(r, g, b);
    }
    
    _updatePipeColors(weights) {
        for (let i = 0; i < 8; i++) {
            const weight = weights[i];
            const pipe = this.pipes[i];
            
            if (weight > 0) {
                // Positive weight: green (inflow)
                const intensity = Math.min(1, weight / 127);
                pipe.material.color.setRGB(0.2, 0.4 + intensity * 0.6, 0.2);
            } else if (weight < 0) {
                // Negative weight: red (outflow)
                const intensity = Math.min(1, Math.abs(weight) / 127);
                pipe.material.color.setRGB(0.4 + intensity * 0.6, 0.2, 0.2);
            } else {
                // Zero: gray
                pipe.material.color.setRGB(0.4, 0.4, 0.4);
            }
        }
    }
    
    _updateValves(vsbInputs) {
        for (let i = 0; i < 8; i++) {
            const vsb = vsbInputs[i];
            const valve = this.valves[i];

            // Valve rotates based on VSB input (0-15)
            const openAmount = vsb / 15;

            // Rotate valve ring to show opening
            valve.rotation.z = openAmount * Math.PI * 2;
        }
    }
    
    _updateDecayPipe(decay16) {
        // Show decay pipe activity based on decay16 value
        const activity = decay16 / 65535;
        
        this.decayPipe.material.opacity = 0.3 + activity * 0.5;
        this.decayValve.material.emissive.setRGB(activity * 0.3, activity * 0.3, activity * 0.5);
    }
    
    _animateParticles(deltaTime) {
        // Get current liquid level for particle bounds
        const liquidY = this.liquid.position.y;
        const liquidHeight = this.liquid.scale.y;
        const liquidTop = liquidY + liquidHeight / 2;
        const liquidBottom = liquidY - liquidHeight / 2;
        
        for (const particle of this.particles) {
            // Bob up and down within the liquid bounds
            const bobRange = Math.max(0.1, liquidHeight * 0.3);
            const bobCenter = (liquidTop + liquidBottom) / 2;
            
            particle.position.y = bobCenter + 
                Math.sin(this.time * particle.userData.speed + particle.userData.offset) * bobRange;
            
            // Clamp to liquid bounds
            particle.position.y = Math.max(liquidBottom + 0.05, Math.min(liquidTop - 0.05, particle.position.y));
            
            // Slight horizontal movement
            particle.position.x += Math.sin(this.time * 0.5 + particle.userData.offset) * 0.001;
            particle.position.z += Math.cos(this.time * 0.5 + particle.userData.offset) * 0.001;
        }
    }
    
    _animateFlowRing(decay16, deltaTime) {
        const activity = decay16 / 65535;
        const pulse = Math.sin(this.time * 5) * 0.3 + 0.5;

        this.flowRing.material.opacity = activity * pulse * 0.8;
        this.flowRing.rotation.z += deltaTime * 2;
    }
    
    _flashBarrel() {
        // Brief flash effect on barrel
        this.barrel.material.emissive.setRGB(0.5, 0.5, 1);
        setTimeout(() => {
            if (this.barrel) {
                this.barrel.material.emissive.setRGB(0, 0, 0);
            }
        }, 100);
    }
    
    _onResize() {
        const width = this.canvas.clientWidth;
        const height = this.canvas.clientHeight;
        
        this.camera.aspect = width / height;
        this.camera.updateProjectionMatrix();
        
        this.renderer.setSize(width, height);
    }
    
    /**
     * Render the scene
     */
    render() {
        this.renderer.render(this.scene, this.camera);
    }
    
    /**
     * Cleanup resources
     */
    dispose() {
        this.renderer.dispose();
        
        // Dispose geometries and materials
        this.scene.traverse((object) => {
            if (object.geometry) {
                object.geometry.dispose();
            }
            if (object.material) {
                object.material.dispose();
            }
        });
    }
}
