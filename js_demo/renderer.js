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
        
        // Brightness control
        this.brightness = 1.0;
        this.mainLight = null;
        
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
        this.decayPipe = null;
        this.particles = [];
        this.flowParticles = [];  // For pipe flow animation
        this.phaseMarkers = [];   // Phase read/write markers on pipes
        
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
        
        // Mouse events
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
        
        // Touch events for mobile
        let lastTouchDistance = 0;
        
        canvas.addEventListener('touchstart', (e) => {
            e.preventDefault();
            
            if (e.touches.length === 1) {
                // Single touch - rotate
                this.isDragging = true;
                this.previousMousePosition = { 
                    x: e.touches[0].clientX, 
                    y: e.touches[0].clientY 
                };
            } else if (e.touches.length === 2) {
                // Two fingers - pinch zoom
                this.isDragging = false;
                lastTouchDistance = Math.hypot(
                    e.touches[0].clientX - e.touches[1].clientX,
                    e.touches[0].clientY - e.touches[1].clientY
                );
            }
        }, { passive: false });
        
        canvas.addEventListener('touchmove', (e) => {
            e.preventDefault();
            
            if (e.touches.length === 1 && this.isDragging) {
                // Single touch - rotate
                const deltaX = e.touches[0].clientX - this.previousMousePosition.x;
                const deltaY = e.touches[0].clientY - this.previousMousePosition.y;
                
                this.cameraAngle.y += deltaX * 0.01;
                this.cameraAngle.x += deltaY * 0.01;
                
                // Clamp vertical angle
                this.cameraAngle.x = Math.max(-Math.PI / 2 + 0.1, Math.min(Math.PI / 2 - 0.1, this.cameraAngle.x));
                
                this._updateCameraPosition();
                
                this.previousMousePosition = { 
                    x: e.touches[0].clientX, 
                    y: e.touches[0].clientY 
                };
            } else if (e.touches.length === 2) {
                // Two fingers - pinch zoom
                const currentDistance = Math.hypot(
                    e.touches[0].clientX - e.touches[1].clientX,
                    e.touches[0].clientY - e.touches[1].clientY
                );
                
                const delta = currentDistance - lastTouchDistance;
                this.cameraDistance -= delta * 0.05;
                this.cameraDistance = Math.max(3, Math.min(15, this.cameraDistance));
                this._updateCameraPosition();
                
                lastTouchDistance = currentDistance;
            }
        }, { passive: false });
        
        canvas.addEventListener('touchend', () => {
            this.isDragging = false;
        });
        
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
        this.mainLight = dirLight;  // Store reference for brightness control
        
        // Blue rim light
        const rimLight = new THREE.DirectionalLight(0x4040ff, 0.5);
        rimLight.position.set(-3, 2, -3);
        this.scene.add(rimLight);
        
        // Point light near barrel
        const pointLight = new THREE.PointLight(0x7a7aff, 0.8, 10);
        pointLight.position.set(0, 1, 2);
        this.scene.add(pointLight);
    }
    
    /**
     * Set scene brightness
     */
    setBrightness(value) {
        this.brightness = value;
        if (this.mainLight) {
            this.mainLight.intensity = value;
        }
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
        
        // Create float and crossbar assembly
        this._createFloatAssembly(barrelGroup);
        
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
        
        // thr_lo marker (bright red flat ring around barrel)
        const thrLoGeometry = new THREE.RingGeometry(1.01, 1.04, 32);
        const thrLoMaterial = new THREE.MeshBasicMaterial({ 
            color: 0xff4444,
            transparent: true,
            opacity: 1.0,
            side: THREE.DoubleSide
        });
        this.thrLoMarker = new THREE.Mesh(thrLoGeometry, thrLoMaterial);
        this.thrLoMarker.position.y = 1.25;  // Will be updated based on thr_lo
        this.thrLoMarker.rotation.x = Math.PI / 2;
        barrelGroup.add(this.thrLoMarker);
        
        // thr_hi marker (bright green flat ring around barrel)
        const thrHiGeometry = new THREE.RingGeometry(1.01, 1.04, 32);
        const thrHiMaterial = new THREE.MeshBasicMaterial({ 
            color: 0x44ff44,
            transparent: true,
            opacity: 1.0,
            side: THREE.DoubleSide
        });
        this.thrHiMarker = new THREE.Mesh(thrHiGeometry, thrHiMaterial);
        this.thrHiMarker.position.y = 1.25;  // Will be updated based on thr_hi
        this.thrHiMarker.rotation.x = Math.PI / 2;
        barrelGroup.add(this.thrHiMarker);
        
        // Lock zone (semi-transparent cylinder between thr_lo and thr_hi)
        const lockZoneGeometry = new THREE.CylinderGeometry(1.0, 1.0, 0.1, 32, 1, true);
        const lockZoneMaterial = new THREE.MeshBasicMaterial({ 
            color: 0x44ff44,  // Green to match thr markers
            transparent: true,
            opacity: 0.15,
            side: THREE.DoubleSide
        });
        this.lockZone = new THREE.Mesh(lockZoneGeometry, lockZoneMaterial);
        this.lockZone.position.y = 1.25;  // Will be updated
        this.lockZone.scale.y = 0;  // Will be updated
        barrelGroup.add(this.lockZone);
    }
    
    /**
     * Create float assembly with cable and crossbar
     */
    _createFloatAssembly(barrelGroup) {
        const floatGroup = new THREE.Group();
        
        // Float (cylinder that moves up/down with liquid level)
        const floatGeometry = new THREE.CylinderGeometry(0.3, 0.3, 0.4, 16);
        const floatMaterial = new THREE.MeshStandardMaterial({
            color: 0xffaa00,
            metalness: 0.7,
            roughness: 0.2
        });
        this.float = new THREE.Mesh(floatGeometry, floatMaterial);
        this.float.position.y = 1.25;  // Start at middle (zero level)
        floatGroup.add(this.float);
        
        // Cable (thin line from float down through barrel bottom)
        const cableGeometry = new THREE.CylinderGeometry(0.02, 0.02, 3.0, 8);
        const cableMaterial = new THREE.MeshStandardMaterial({
            color: 0x333333,
            metalness: 0.5,
            roughness: 0.4
        });
        this.cable = new THREE.Mesh(cableGeometry, cableMaterial);
        this.cable.position.y = -0.25;  // Extend below float
        floatGroup.add(this.cable);
        
        // Crossbar (horizontal bar at bottom that connects to pipes)
        const crossbarGeometry = new THREE.CylinderGeometry(0.04, 0.04, 1.2, 16);
        const crossbarMaterial = new THREE.MeshStandardMaterial({
            color: 0x888888,
            metalness: 0.6,
            roughness: 0.3
        });
        this.crossbar = new THREE.Mesh(crossbarGeometry, crossbarMaterial);
        this.crossbar.position.y = -1.5;  // Below barrel
        this.crossbar.rotation.z = Math.PI / 2;  // Horizontal
        floatGroup.add(this.crossbar);
        
        // Connection rods from crossbar to pipe valves (4 rods for cardinal directions)
        const rodLength = 0.5;
        const rodGeometry = new THREE.CylinderGeometry(0.02, 0.02, rodLength, 8);
        const rodMaterial = new THREE.MeshStandardMaterial({
            color: 0x666666,
            metalness: 0.5,
            roughness: 0.4
        });
        
        // Create 4 rods (N/E/S/W directions)
        const rodPositions = [
            { x: 0, z: 0.6 },   // North
            { x: 0.6, z: 0 },   // East
            { x: 0, z: -0.6 },  // South
            { x: -0.6, z: 0 }   // West
        ];
        
        this.connectionRods = [];
        for (const pos of rodPositions) {
            const rod = new THREE.Mesh(rodGeometry, rodMaterial);
            rod.position.set(pos.x, -1.5, pos.z);
            rod.rotation.x = Math.PI / 2;  // Horizontal
            floatGroup.add(rod);
            this.connectionRods.push(rod);
        }
        
        barrelGroup.add(floatGroup);
        this.floatGroup = floatGroup;
    }
    
    _createPipes() {
        const pipeGroup = new THREE.Group();

        // 8 pipes arranged in a circle under the barrel
        const barrelRadius = 1.0;
        const basePipeRadius = 0.02;   // Minimum radius (weight 0 = line)
        const maxPipeRadius = 0.14;    // Max radius (|weight| 56) - doubled from before
        // Pipes positioned well inside barrel radius so always covered by liquid
        const pipeOffset = barrelRadius * 0.6;  // 0.6 - pipes near center, always under liquid
        const pipeLength = 1.6;  // Doubled from 0.8

        for (let i = 0; i < 8; i++) {
            const angle = (i / 8) * Math.PI * 2;
            const x = Math.cos(angle) * pipeOffset;
            const z = Math.sin(angle) * pipeOffset;

            // Pipe radius will be updated based on weight magnitude
            const pipeMaterial = new THREE.MeshPhysicalMaterial({
                color: 0xaaaaaa,  // Light gray initially
                metalness: 0.2,
                roughness: 0.1,
                transparent: true,
                opacity: 0.6,
                transmission: 0.6,  // Glass-like
                thickness: 0.5
            });

            // Pipe: vertical cylinder (radius will be updated)
            const pipeGeometry = new THREE.CylinderGeometry(basePipeRadius, basePipeRadius, pipeLength, 16);
            const pipe = new THREE.Mesh(pipeGeometry, pipeMaterial);

            // Position: pipe goes from y=-pipeLength to y=0 (barrel bottom)
            pipe.position.set(x, -pipeLength / 2, z);
            
            // Store initial scale for updates
            pipe.userData.baseRadius = basePipeRadius;
            pipe.userData.maxRadius = maxPipeRadius;

            pipeGroup.add(pipe);
            this.pipes.push(pipe);
            
            // Create flow particle for this pipe
            const flowParticleGeometry = new THREE.SphereGeometry(0.04, 8, 8);
            const flowParticleMaterial = new THREE.MeshBasicMaterial({
                color: 0xffffff,
                transparent: true,
                opacity: 0
            });
            const flowParticle = new THREE.Mesh(flowParticleGeometry, flowParticleMaterial);
            flowParticle.userData = {
                pipeIndex: i,
                y: -pipeLength,  // Start at bottom
                speed: 0,
                active: false
            };
            pipeGroup.add(flowParticle);
            this.flowParticles.push(flowParticle);
            
            // Create phase_read marker (torus at bottom of pipe)
            const readMarkerGeometry = new THREE.TorusGeometry(0.08, 0.02, 8, 16);
            const readMarkerMaterial = new THREE.MeshBasicMaterial({
                color: 0x4444ff,
                transparent: true,
                opacity: 0.3
            });
            const readMarker = new THREE.Mesh(readMarkerGeometry, readMarkerMaterial);
            readMarker.position.y = -pipeLength + 0.2;  // Near bottom
            readMarker.rotation.x = Math.PI / 2;
            readMarker.userData = { baseOpacity: 0.3 };
            pipeGroup.add(readMarker);
            
            // Create phase_write marker (torus near top of pipe)
            const writeMarkerGeometry = new THREE.TorusGeometry(0.08, 0.02, 8, 16);
            const writeMarkerMaterial = new THREE.MeshBasicMaterial({
                color: 0xff4444,
                transparent: true,
                opacity: 0.3
            });
            const writeMarker = new THREE.Mesh(writeMarkerGeometry, writeMarkerMaterial);
            writeMarker.position.y = -0.2;  // Near top (close to barrel)
            writeMarker.rotation.x = Math.PI / 2;
            writeMarker.userData = { baseOpacity: 0.3 };
            pipeGroup.add(writeMarker);
            
            this.phaseMarkers.push({ read: readMarker, write: writeMarker });
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
        const pipeMaterial = new THREE.MeshPhysicalMaterial({
            color: 0x4444aa,  // Blue color for decay pipe
            metalness: 0.3,
            roughness: 0.1,
            transparent: true,
            opacity: 0.4,
            transmission: 0.5,
            thickness: 0.5
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

        // Decay flow particle
        const decayFlowGeometry = new THREE.SphereGeometry(0.06, 8, 8);
        const decayFlowMaterial = new THREE.MeshBasicMaterial({
            color: 0x00ffff,
            transparent: true,
            opacity: 0
        });
        this.decayFlowParticle = new THREE.Mesh(decayFlowGeometry, decayFlowMaterial);
        this.decayFlowParticle.position.set(1.3, 2.0, 0);  // Match decay pipe position
        this.decayFlowParticle.userData = { y: 2.0 };
        decayGroup.add(this.decayFlowParticle);
        
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
     * Trigger flow animation on pipes based on weights
     */
    triggerPipeFlow(weights, decay16) {
        for (let i = 0; i < 8; i++) {
            const weight = weights[i];
            const particle = this.flowParticles[i];
            
            if (weight !== 0) {
                // Activate flow particle
                particle.userData.active = true;
                particle.userData.speed = Math.abs(weight) / 56 * 0.5;  // Speed based on weight
                particle.userData.direction = weight > 0 ? 1 : -1;  // Up for positive, down for negative
                particle.material.opacity = 0.8;
                particle.material.color.setRGB(
                    weight > 0 ? 0.2 : 0.8,
                    weight > 0 ? 0.8 : 0.2,
                    0.5
                );
                // Start at bottom for positive (flowing up), top for negative (flowing down)
                particle.userData.y = weight > 0 ? -0.8 : 0;
            }
        }
        
        // Trigger decay flow animation
        if (decay16 > 0) {
            this.decayFlowParticle.userData.active = true;
            this.decayFlowParticle.userData.y = 2.0;  // Start at top
            this.decayFlowParticle.userData.speed = (decay16 / 65535) * 0.3;
            this.decayFlowParticle.material.opacity = 0.8;
        }
    }
    
    /**
     * Update flow particle animations
     */
    _updateFlowParticles(deltaTime) {
        // Update pipe flow particles
        for (let i = 0; i < this.flowParticles.length; i++) {
            const particle = this.flowParticles[i];
            
            if (particle.userData.active) {
                // Move particle along pipe
                particle.userData.y += particle.userData.speed * particle.userData.direction;
                
                // Reset when out of bounds
                if (particle.userData.y > 0 || particle.userData.y < -1.6) {
                    particle.userData.active = false;
                    particle.material.opacity = 0;
                }
                
                // Position particle
                particle.position.y = particle.userData.y;
            }
        }
        
        // Update decay flow particle
        if (this.decayFlowParticle.userData.active) {
            this.decayFlowParticle.userData.y -= this.decayFlowParticle.userData.speed;
            
            if (this.decayFlowParticle.userData.y < 0.5) {
                this.decayFlowParticle.userData.active = false;
                this.decayFlowParticle.material.opacity = 0;
            }
            
            this.decayFlowParticle.position.y = this.decayFlowParticle.userData.y;
            this.decayFlowParticle.position.x = 1.3;  // Match decay pipe X
        }
    }
    
    /**
     * Update phase markers based on current phase
     */
    _updatePhaseMarkers(phase) {
        for (const markers of this.phaseMarkers) {
            if (phase === 'read') {
                // phase_read: blue marker at bottom lights up
                markers.read.material.opacity = 0.9;
                markers.write.material.opacity = markers.write.userData.baseOpacity;
            } else if (phase === 'write') {
                // phase_write: red marker at top lights up
                markers.read.material.opacity = markers.read.userData.baseOpacity;
                markers.write.material.opacity = 0.9;
            } else {
                // idle: both dim
                markers.read.material.opacity = markers.read.userData.baseOpacity;
                markers.write.material.opacity = markers.write.userData.baseOpacity;
            }
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
        
        // Update float position to match liquid level
        this._updateFloatPosition(tileState.accumulator);
        
        // Update threshold markers (thr_lo, thr_hi)
        this._updateThresholdMarkers(tileState.thr_lo, tileState.thr_hi);
        
        // Update liquid color based on value and fire/lock state
        this._updateLiquidColor(tileState.accumulator, tileState.fired, tileState.inLock);
        
        // Update pipe colors based on weights
        this._updatePipeColors(tileState.weights);
        
        // Update phase markers
        this._updatePhaseMarkers(tileState.phase);
        
        // Update flow particles animation
        this._updateFlowParticles(deltaTime);

        // Update decay pipe visibility
        this._updateDecayPipe(tileState.decay16);

        // Animate particles
        this._animateParticles(deltaTime);

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
    
    _updateFloatPosition(accumulator) {
        // Float moves with liquid level
        // Map accumulator to float Y position (0 to barrelHeight)
        const maxRange = 32768;
        const barrelHeight = 2.5;
        const zeroLevel = barrelHeight / 2;  // 1.25
        
        let floatY;
        if (accumulator >= 0) {
            const normalized = Math.min(accumulator / maxRange, 1);
            floatY = zeroLevel + normalized * zeroLevel;
        } else {
            const normalized = Math.min(Math.abs(accumulator) / maxRange, 1);
            floatY = zeroLevel - normalized * zeroLevel;
        }
        
        // Float sits on top of liquid
        this.float.position.y = floatY;
        
        // Cable extends from float down
        const cableLength = floatY + 1.5;  // From float to below barrel
        this.cable.scale.y = cableLength / 3.0;  // Original cable is 3.0 units
        this.cable.position.y = floatY - cableLength / 2;
        
        // Crossbar and rods move with cable
        this.crossbar.position.y = -1.5 + (floatY - 1.25) * 0.3;  // Slight movement
        
        // Update rod positions to match crossbar
        for (const rod of this.connectionRods) {
            rod.position.y = this.crossbar.position.y;
        }
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
    
    _updateLiquidColor(accumulator, fired, inLock) {
        // Color based on accumulator value and state
        // Negative: dark blue, Zero: cyan, Positive: light blue/white
        // LOCK: bright white
        // FIRE: bright white
        
        let r, g, b;
        const normalized = accumulator / 32768;
        
        if (fired || inLock) {
            // Bright white when fired OR in lock
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

            // Color based on weight sign
            if (weight > 0) {
                // Positive weight: green (inflow)
                const intensity = Math.min(1, weight / 56);
                pipe.material.color.setRGB(0.3 + intensity * 0.2, 0.6 + intensity * 0.4, 0.3);
            } else if (weight < 0) {
                // Negative weight: red (outflow)
                const intensity = Math.min(1, Math.abs(weight) / 56);
                pipe.material.color.setRGB(0.6 + intensity * 0.4, 0.3, 0.3);
            } else {
                // Zero weight: gray (inactive)
                pipe.material.color.setRGB(0.6, 0.6, 0.6);
            }
            
            // Scale pipe based on weight magnitude (0-56 visual scale)
            // Map |weight| (0-56) to scale (0-1)
            const weightMagnitude = Math.abs(weight) / 56;
            const baseRadius = pipe.userData.baseRadius;
            const maxRadius = pipe.userData.maxRadius;
            const currentRadius = baseRadius + weightMagnitude * (maxRadius - baseRadius);
            
            // Scale the pipe mesh (original geometry has radius = baseRadius)
            const scale = currentRadius / baseRadius;
            pipe.scale.set(scale, 1, scale);
        }
    }
    
    _updateDecayPipe(decay16) {
        // Show decay pipe activity based on decay16 value
        const activity = decay16 / 65535;

        this.decayPipe.material.opacity = 0.3 + activity * 0.5;
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
