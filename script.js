document.addEventListener('DOMContentLoaded', function() {
    // Elements
    const armButton = document.getElementById('arm-button');
    const armStatus = document.getElementById('arm-status');
    const throttleSlider = document.getElementById('throttle-slider');
    const throttleValue = document.getElementById('throttle-value');
    const yawSlider = document.getElementById('yaw-slider');
    const yawValue = document.getElementById('yaw-value');
    const joystickArea = document.getElementById('joystick-area');
    const joystickKnob = document.getElementById('joystick-knob');
    const pitchControl = document.getElementById('pitch-control');
    const rollControl = document.getElementById('roll-control');
    
    // Sensor display elements
    const pitchValueDisplay = document.getElementById('pitch-value');
    const rollValueDisplay = document.getElementById('roll-value');
    const yawValueDisplay = document.getElementById('yaw-value');
    
    // Motor display elements
    const motorFL = document.getElementById('motor-fl');
    const motorFR = document.getElementById('motor-fr');
    const motorBL = document.getElementById('motor-bl');
    const motorBR = document.getElementById('motor-br');
    const motorFLValue = document.getElementById('motor-fl-value');
    const motorFRValue = document.getElementById('motor-fr-value');
    const motorBLValue = document.getElementById('motor-bl-value');
    const motorBRValue = document.getElementById('motor-br-value');
    
    // Control state
    let isArmed = false;
    let throttle = 0;
    let pitch = 0;
    let roll = 0;
    let yaw = 0;
    
    // Initialize joystick position
    const joystickRect = joystickArea.getBoundingClientRect();
    const joystickCenterX = joystickRect.width / 2;
    const joystickCenterY = joystickRect.height / 2;
    let isDragging = false;
    
    // Update control function
    function updateControls() {
        // Send control values to ESP32
        const url = `/control?throttle=${throttle}&pitch=${pitch}&roll=${roll}&yaw=${yaw}&arm=${isArmed ? 1 : 0}`;
        
        fetch(url)
            .then(response => response.json())
            .then(data => {
                // Update motor displays
                const flPercent = Math.round(data.motorFL / 2.55);
                const frPercent = Math.round(data.motorFR / 2.55);
                const blPercent = Math.round(data.motorBL / 2.55);
                const brPercent = Math.round(data.motorBR / 2.55);
                
                motorFL.style.width = `${flPercent}%`;
                motorFR.style.width = `${frPercent}%`;
                motorBL.style.width = `${blPercent}%`;
                motorBR.style.width = `${brPercent}%`;
                
                motorFLValue.textContent = `${flPercent}%`;
                motorFRValue.textContent = `${frPercent}%`;
                motorBLValue.textContent = `${blPercent}%`;
                motorBRValue.textContent = `${brPercent}%`;
            })
            .catch(error => console.error('Error:', error));
    }
    
    // Update sensor data function
    function updateSensorData() {
        fetch('/sensor')
            .then(response => response.json())
            .then(data => {
                // Update orientation displays
                pitchValueDisplay.textContent = `${data.pitch.toFixed(1)}°`;
                rollValueDisplay.textContent = `${data.roll.toFixed(1)}°`;
                yawValueDisplay.textContent = `${data.yaw.toFixed(1)}°`;
            })
            .catch(error => console.error('Error:', error));
    }
    
    // Arm/Disarm button event
    armButton.addEventListener('click', function() {
        isArmed = !isArmed;
        
        if (isArmed) {
            armButton.textContent = 'DISARM';
            armButton.className = 'btn disarm';
            armStatus.textContent = 'ARMED';
            armStatus.className = 'armed';
        } else {
            armButton.textContent = 'ARM';
            armButton.className = 'btn arm';
            armStatus.textContent = 'DISARMED';
            armStatus.className = 'disarmed';
            
            // Reset controls when disarming
            throttle = 0;
            throttleSlider.value = 0;
            throttleValue.textContent = '0%';
            
            pitch = 0;
            roll = 0;
            yaw = 0;
            yawSlider.value = 0;
            yawValue.textContent = '0';
            
            // Reset joystick position
            joystickKnob.style.top = '50%';
            joystickKnob.style.left = '50%';
            pitchControl.textContent = '0';
            rollControl.textContent = '0';
        }
        
        // Send arm/disarm command
        updateControls();
    });
    
    // Throttle slider event
    throttleSlider.addEventListener('input', function() {
        throttle = parseInt(this.value);
        throttleValue.textContent = `${throttle}%`;
        updateControls();
    });
    
    // Yaw slider event
    yawSlider.addEventListener('input', function() {
        yaw = parseInt(this.value);
        yawValue.textContent = yaw;
        updateControls();
    });
    
    // Joystick touch/mouse events with improved handling for mobile
    joystickKnob.addEventListener('mousedown', startDrag);
    joystickKnob.addEventListener('touchstart', startDrag, { passive: false });
    
    function startDrag(e) {
        e.preventDefault();
        isDragging = true;
        
        if (e.type === 'touchstart') {
            document.addEventListener('touchmove', handleDrag, { passive: false });
            document.addEventListener('touchend', stopDrag);
        } else {
            document.addEventListener('mousemove', handleDrag);
            document.addEventListener('mouseup', stopDrag);
        }
    }
    
    function handleDrag(e) {
        if (!isDragging) return;
        e.preventDefault();
        
        // Get position
        const joystickRect = joystickArea.getBoundingClientRect();
        let clientX, clientY;
        
        if (e.type === 'touchmove') {
            clientX = e.touches[0].clientX;
            clientY = e.touches[0].clientY;
        } else {
            clientX = e.clientX;
            clientY = e.clientY;
        }
        
        // Calculate position relative to joystick center
        let x = clientX - joystickRect.left;
        let y = clientY - joystickRect.top;
        
        // Calculate distance from center
        const centerX = joystickRect.width / 2;
        const centerY = joystickRect.height / 2;
        const distance = Math.sqrt(Math.pow(x - centerX, 2) + Math.pow(y - centerY, 2));
        
        // Limit joystick movement to the circular area
        const maxDistance = joystickRect.width / 2 - joystickKnob.offsetWidth / 2;
        if (distance > maxDistance) {
            const angle = Math.atan2(y - centerY, x - centerX);
            x = centerX + maxDistance * Math.cos(angle);
            y = centerY + maxDistance * Math.sin(angle);
        }
        
        // Position the joystick knob
        joystickKnob.style.left = `${x}px`;
        joystickKnob.style.top = `${y}px`;
        
        // Calculate roll and pitch values (-50 to 50)
        roll = Math.round(((x - centerX) / maxDistance) * 50);
        pitch = Math.round(((y - centerY) / maxDistance) * 50);
        
        // Update display
        rollControl.textContent = roll;
        pitchControl.textContent = pitch;
        
        // Update drone control
        updateControls();
    }
    
    function stopDrag() {
        if (!isDragging) return;
        isDragging = false;
        
        document.removeEventListener('mousemove', handleDrag);
        document.removeEventListener('touchmove', handleDrag);
        document.removeEventListener('mouseup', stopDrag);
        document.removeEventListener('touchend', stopDrag);
        
        // Return joystick to center
        joystickKnob.style.top = '50%';
        joystickKnob.style.left = '50%';
        
        // Reset roll and pitch
        roll = 0;
        pitch = 0;
        rollControl.textContent = '0';
        pitchControl.textContent = '0';
        
        // Update drone control
        updateControls();
    }
    
    // Start regular updates for sensor data and controls
    setInterval(updateSensorData, 200);  // Update sensor data every 200ms
    setInterval(updateControls, 100);    // Ensure control values are sent regularly
    
    // Initial update
    updateControls();
    updateSensorData();
    
    // Prevent default touch actions for the entire control panel to improve mobile experience
    document.querySelector('.controls').addEventListener('touchmove', function(e) {
        e.preventDefault();
    }, { passive: false });
});