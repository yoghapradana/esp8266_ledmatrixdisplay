function toggleBrightnessMode() {
    const mode = document.getElementById('brightnessMode').value;
    const manualSettings = document.getElementById('manualBrightness');
    const autoSettings = document.getElementById('autoSettings');

    if (mode === 'auto') {
        manualSettings.style.display = 'none';
        autoSettings.style.display = 'block';
        updateSunTimesUI(); // Refresh times when switched to auto
    } else {
        manualSettings.style.display = 'block';
        autoSettings.style.display = 'none';
    }
}

// --- JS implementation of your C++ calculateSunEvent function ---
function formatDecimalHour(decimalHour) {
    if (isNaN(decimalHour)) return "--:--";
    // Handle 24-hour wrap
    if (decimalHour < 0) decimalHour += 24;
    if (decimalHour >= 24) decimalHour -= 24;

    const h = Math.floor(decimalHour);
    const m = Math.floor(Math.round((decimalHour - h) * 60));

    // Adjust if minutes round to 60
    const finalH = m === 60 ? (h + 1) % 24 : h;
    const finalM = m === 60 ? 0 : m;

    return `${finalH.toString().padStart(2, '0')}:${finalM.toString().padStart(2, '0')}`;
}

function updateSunTimesUI() {
    const lat = parseFloat(document.getElementById('latitude').value) || 0;
    const lon = parseFloat(document.getElementById('longitude').value) || 0;
    const timeOffsetSec = parseFloat(document.getElementById('timeOffset').value) || 0;

    // Calculate day of year
    const now = new Date();
    const start = new Date(now.getFullYear(), 0, 0);
    const diff = now - start;
    const oneDay = 1000 * 60 * 60 * 24;
    const dayOfYear = Math.floor(diff / oneDay);

    // C++ Math Port
    const declination = -23.45 * Math.cos(2 * Math.PI * (dayOfYear + 10) / 365.0);

    // Prevent Math.acos NaN for extreme latitudes (polar regions)
    let tanCalc = -Math.tan(lat * Math.PI / 180.0) * Math.tan(declination * Math.PI / 180.0);
    if (tanCalc < -1) tanCalc = -1;
    if (tanCalc > 1) tanCalc = 1;

    const hourAngle = Math.acos(tanCalc) * 180.0 / Math.PI;
    const timeOffsetHours = timeOffsetSec / 3600.0;
    const solarNoon = 12.0 + timeOffsetHours - (lon / 15.0);

    const sunriseHour = solarNoon - (hourAngle / 15.0);
    const sunsetHour = solarNoon + (hourAngle / 15.0);

    document.getElementById('calcSunrise').innerText = formatDecimalHour(sunriseHour);
    document.getElementById('calcSunset').innerText = formatDecimalHour(sunsetHour);
}
// --------------------------------------------------------------

function setQuickTimezone(offset) {
    if (offset) {
        document.getElementById('timeOffset').value = offset;
        updateSunTimesUI(); // Refresh times when timezone changes
    }
}

function showMessage(message, type) {
    const container = document.getElementById("toast");
    const msg = document.createElement("div");
    msg.className = "toast-message toast-" + type;
    msg.textContent = message;
    container.appendChild(msg);
    setTimeout(() => msg.remove(), 3000);
}

function saveWiFi() {
    const ssid = document.getElementById('ssid').value;
    const password = document.getElementById('password').value;

    if (!ssid) return showMessage('Please enter an SSID', 'error');

    // Create a plain object
    const data = {
        ssid: ssid,
        password: password
    };

    fetch('/saveCredentials', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json' // Crucial: tells the ESP what to expect
        },
        body: JSON.stringify(data)
    })
        .then(response => {
            if (!response.ok) throw new Error();

            showMessage('WiFi settings saved! Device rebooting...', 'success');
            let count = 5;
            const timer = setInterval(() => {
                showMessage(`Rebooting in ${count--} seconds...`, 'success');
                if (count < 0) {
                    clearInterval(timer);
                    location.reload();
                }
            }, 1000);
        })
        .catch(() => showMessage('Failed to save WiFi settings', 'error'));
}

function saveAllSettings() {

    // validate all form inputs first
    const inputs = document.querySelectorAll('input, select');

    for (const input of inputs) {

        // skip hidden/disabled inputs if needed
        if (input.disabled || input.offsetParent === null) {
            continue;
        }

        if (!input.checkValidity()) {
            input.reportValidity();
            input.focus();
            return;
        }
    }

    const mode = document.getElementById('brightnessMode').value;
    const settings = {
        //auto=1 manual=0
        autoBrt: mode === 'auto',
        showHjr: document.getElementById('showHijri').checked,
        showPsr: document.getElementById('showPasaran').checked,
        tmOft: parseInt(document.getElementById('timeOffset').value),
        is24: document.getElementById('is24h').checked,
        ntpSrv: document.getElementById('ntpServer').value
    };

    if (document.getElementById('showHijri').checked) {
        settings.hjrOft = parseInt(document.getElementById('hijriOffset').value);
    }

    if (mode === 'manual') {
        settings.dST = timeToMinutes(document.getElementById('dayStartTime').value);
        settings.nST = timeToMinutes(document.getElementById('nightStartTime').value);
        settings.dBrt = parseInt(document.getElementById('dayBrightness').value);
        settings.ntBrt = parseInt(document.getElementById('nightBrightness').value);
    } else {
        settings.lat = parseFloat(document.getElementById('latitude').value);
        settings.long = parseFloat(document.getElementById('longitude').value);
        settings.dBrt = parseInt(document.getElementById('dayBrightness').value);
        settings.ntBrt = parseInt(document.getElementById('nightBrightness').value);
    }

    fetch('/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(settings)
    })
        .then(response => {
            if (!response.ok) {
                // This triggers if the server sends 400 (JSON Error)
                throw new Error('Server returned ' + response.status);
            }
            return response.text();
        })
        .then(() => {
            showMessage('Settings saved successfully!', 'success');
        })
        .catch((error) => {
            // This now catches BOTH network failures AND the 400 error from above
            console.error('Save failed:', error);
            showMessage('Failed to save settings: ' + error.message, 'error');
        });
}

window.onload = function () {
    fetch('/getsettings')
        .then(response => response.json())
        .then(data => {
            document.getElementById('ssidCurrent').textContent = data.ssid || 'Not set';
            document.getElementById('wifiStatus').textContent = data.wifiStat || 'Disconnected';
            document.getElementById('ipAddress').textContent = data.ip || '0.0.0.0';
            document.getElementById('timeOffsetCurrent').textContent = `Current: ${data.tmOft || 0} seconds / ${(data.tmOft ? data.tmOft / 3600 : 0).toFixed(2)} hours`;
            document.getElementById('brightnessMode').value = data.autoBrt ? 'auto' : 'manual';
            // Add padStart(2, '0') to the Hour part
            document.getElementById('dayStartTime').value = `${String(data.dSH || 6).padStart(2, '0')}:${String(data.dSM || 0).padStart(2, '0')}`;
            document.getElementById('nightStartTime').value = `${String(data.nSH || 18).padStart(2, '0')}:${String(data.nSM || 0).padStart(2, '0')}`;
            document.getElementById('dayBrightness').value = data.dBrt ?? 8;
            document.getElementById('nightBrightness').value = data.ntBrt ?? 1;

            document.getElementById('latitude').value = data.lat || -7.2575;
            document.getElementById('longitude').value = data.long || 112.7521;

            document.getElementById('showHijri').checked = data.showHjr !== false;
            document.getElementById('hijriOffset').value = data.hjrOft || 0;
            document.getElementById('showPasaran').checked = data.showPsr !== false;
            document.getElementById('timeOffset').value = data.tmOft || 25200;
            document.getElementById('is24h').checked = data.is24 !== false;
            document.getElementById('ntpServer').value = data.ntpSrv || 'pool.ntp.org';
            toggleBrightnessMode();
            updateSunTimesUI();
            toggleHijriOffset();
        })
        .catch(() => {
            console.log('Using default settings');
            toggleBrightnessMode();
            updateSunTimesUI();
            toggleHijriOffset();
        });
};

function toggleHijriOffset() {
    const showHijri = document.getElementById('showHijri').checked;
    const offsetWrapper = document.getElementById('hijriOffsetWrapper');

    if (showHijri) {
        offsetWrapper.style.display = 'block';
    } else {
        offsetWrapper.style.display = 'none';
    }
}

function timeToMinutes(timeString) {
    if (!timeString) return 0; // Default or fallback value
    const [hh, mm] = timeString.split(':');
    return (parseInt(hh, 10) * 60) + parseInt(mm, 10);
}

document.querySelectorAll('.num-input').forEach(input => {
    input.addEventListener('blur', function () {
        input.reportValidity();
    });
});