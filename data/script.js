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
    if (!ssid || !password) return showMessage('Please enter both SSID and password', 'error');

    const formData = new FormData();
    formData.append('ssid', ssid);
    formData.append('password', password);

    fetch('/save', { method: 'POST', body: formData })
        .then(() => {
            showMessage('WiFi settings saved! Device rebooting...', 'success');
            let count = 5;
            const timer = setInterval(() => {
                showMessage(`Rebooting in ${count--} seconds...`, 'success');
                if (count < 0) { clearInterval(timer); location.reload(); }
            }, 1000);
        })
        .catch(() => showMessage('Failed to save WiFi settings', 'error'));
}

function saveAllSettings() {
    const mode = document.getElementById('brightnessMode').value;
    const settings = {
        //auto=1 manual=0
        brtMode: mode === 'auto',
        showHjr: document.getElementById('showHijri').checked,
        showPsr: document.getElementById('showPasaran').checked,
        tmOft: parseInt(document.getElementById('timeOffset').value),
    };

    if (document.getElementById('showHijri').checked) {
        settings.hjrOft = parseInt(document.getElementById('hijriOffset').value);
    }

    if (mode === 'manual') {
        settings.dSH = parseInt(document.getElementById('dayStartHour').value);
        settings.nSN = parseInt(document.getElementById('nightStartHour').value);
        settings.dayBrt = parseInt(document.getElementById('dayBrightness').value);
        settings.nightBrt = parseInt(document.getElementById('nightBrightness').value);
    } else {
        settings.lat = parseFloat(document.getElementById('latitude').value).toFixed(4);
        settings.long = parseFloat(document.getElementById('longitude').value).toFixed(4);
        settings.dayBrt = parseInt(document.getElementById('dayBrightnessAuto').value);
        settings.nightBrt = parseInt(document.getElementById('nightBrightnessAuto').value);
    }

    fetch('/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(settings)
    })
        .then(() => showMessage('Settings saved successfully!', 'success'))
        .catch(() => showMessage('Failed to save settings', 'error'));
}

window.onload = function () {
    fetch('/getsettings')
        .then(response => response.json())
        .then(data => {
            document.getElementById('brightnessMode').value = data.brtMode ? 'auto' : 'manual';
            document.getElementById('dayStartHour').value = data.dSH || 6;
            document.getElementById('nightStartHour').value = data.nSH || 18;
            document.getElementById('dayBrightness').value = data.dayBrt || 8;
            document.getElementById('nightBrightness').value = data.nightBrt || 1;

            document.getElementById('latitude').value = data.lat || -7.2575;
            document.getElementById('longitude').value = data.long || 112.7521;
            document.getElementById('dayBrightnessAuto').value = data.dayBrt || 8;
            document.getElementById('nightBrightnessAuto').value = data.nightBrt || 1;

            document.getElementById('showHijri').checked = data.showHjr !== false;
            document.getElementById('hijriOffset').value = data.hjrOft || 0;
            document.getElementById('showPasaran').checked = data.showPsr !== false;
            document.getElementById('timeOffset').value = data.tmOft || 25200;

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