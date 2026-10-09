## BGA Reflow Controller — Quick Start Guide

---

### Step 1 — Flash the Firmware

1. Open **Google Chrome** (required for Web Serial API)
2. Navigate to the online flasher: **https://esptool.spacehuhn.com/**
3. Click **Connect** and select your **ESP32-S3 N16R8** from the port list

   > Tip: If no port appears, hold the BOOT button on the board, press RST once,
   > then release BOOT to enter download mode.

4. Set **Offset** to: 0x0
5. Upload the firmware file
6. Click **Flash / Upload**

   > Note: Flashing via the online flasher may take 2–5 minutes or longer — this is normal.
   > The following settings are pre-configured in the image and should not need to be changed:
   > Flash Mode: DIO · Flash Size: 16 MB · Frequency: 80 MHz

7. Once flashing is complete, press RST to reboot the controller.

---

### Step 2 — Connect to the Controller

The controller boots into Wi-Fi Access Point mode automatically.

  Wi-Fi Network (SSID) : BGA Reflow Controller
  Password             : reflow123
  Web Interface        : http://192.168.4.1
  mDNS (alternative)   : http://reflow.local

1. On your phone or laptop, connect to the Wi-Fi network "BGA Reflow Controller"
2. Open your browser and go to http://192.168.4.1
3. The reflow controller web interface will load

The Wi-Fi password can be changed in the Settings → Security section of the web UI.
By default, the controller starts in Simulation Mode — no heat output will occur until disabled in Settings.
