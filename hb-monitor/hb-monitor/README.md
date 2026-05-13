# Non-Invasive Hemoglobin Monitor
# Non-Invasive Hemoglobin Monitor

![Full Setup](images/setup.jpg)

> Full hardware setup — Arduino Uno, MAX30102, 1.3" OLED, SPDT switch

## Result on OLED

![OLED Reading](images/oled_result.jpg)

## Correct Finger Placement

![Finger Placement](images/finger.jpg)

An Arduino-based non-invasive hemoglobin (Hb) estimator using the MAX30102 pulse oximeter sensor and a 1.3" OLED display. Estimates blood hemoglobin concentration in g/dL using an AC/DC photoplethysmography (PPG) ratio and a polynomial regression model.

---

## Features

- Non-invasive Hb estimation using red (660 nm) and IR (940 nm) PPG signals
- Male / Female mode via SPDT toggle switch with gender-specific WHO thresholds
- Signal quality filtering — rejects ambient light and weak pulse automatically
- Sensor settling on finger placement to discard noisy startup samples
- 6-sample R smoothing for stable readings
- Live OLED display with Hb value, anemia classification, and perfusion index bar
- Serial output at 9600 baud for calibration data collection

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | Arduino Uno / Nano |
| Sensor | MAX30102 Pulse Oximeter + Proximity |
| Display | 1.3" OLED 128x64 (SH1106 or SSD1306) |
| Switch | SPDT Toggle Switch (3-pin) |

---

## Wiring

### MAX30102 → Arduino

| MAX30102 | Arduino |
|---|---|
| VIN | 3.3V (NOT 5V — damages sensor) |
| GND | GND |
| SDA | A4 |
| SCL | A5 |

### 1.3" OLED → Arduino

| OLED | Arduino |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | A4 |
| SCL | A5 |

Both modules share the same I2C bus (A4/A5). MAX30102 address: 0x57, OLED: 0x3C or 0x3D.

### SPDT Switch → Arduino

| Switch Pin | Arduino | Meaning |
|---|---|---|
| COM (middle) | GND | Common ground |
| Left pin | D2 | Female mode |
| Right pin | D3 | Male mode |

No external resistors needed — INPUT_PULLUP uses the Arduino internal 20kΩ pull-up.

---

## Libraries

Install both via Arduino IDE → Tools → Manage Libraries:

1. **SparkFun MAX3010x Pulse and Proximity Sensor Library** — by SparkFun Electronics
2. **U8g2** — by oliver

---

## How It Works

### Signal extraction

The MAX30102 emits red (660 nm) and infrared (940 nm) light through the fingertip.
From 50 collected samples per cycle:
- DC component = mean of the buffer (bulk tissue absorption)
- AC component = peak-to-peak of the buffer (pulsatile blood component)

### Ratio of ratios (R)

```
R = (AC_red / DC_red) / (AC_ir / DC_ir)
```

This ratio isolates the pulsatile blood signal. For healthy adults, R typically falls between 0.35 and 0.65.

### Regression model

```
Hb (g/dL) = -16.15 x R^2 + 35.02 x R - 0.63
```

Coefficients fitted via polynomial regression on Abuzairi et al. 2023 Mendeley dataset (MAX30102, 68 subjects), recalibrated for the observed R range 0.30–0.90.

### Perfusion Index (PI)

```
PI (%) = (AC_ir / DC_ir) x 100
```

PI indicates signal quality. Readings with PI < 0.4% (weak pulse) or PI > 10% (ambient light leak) are rejected.

---

## Normal Ranges (WHO)

| Gender | Normal Hb (g/dL) | Mild Anemia | Moderate | Severe |
|---|---|---|---|---|
| Male | 13.5 – 17.5 | 11.0 – 13.5 | 8.0 – 11.0 | < 8.0 |
| Female | 12.0 – 15.5 | 11.0 – 12.0 | 8.0 – 11.0 | < 8.0 |

---

## Serial Monitor Output

Set baud to 9600.

```
=== Hb Monitor v7 ===
Switch: FEMALE
Settling...
Settled. Measuring.
[F] R_raw=0.4612 R_avg=0.4612  Hb=13.8 g/dL  PI=2.14%  redAC=1240  irAC=2890  stable=1
[F] R_raw=0.4589 R_avg=0.4600  Hb=13.9 g/dL  PI=2.08%  redAC=1198  irAC=2810  stable=2
```

---

## Calibration

To improve accuracy with your specific sensor unit:

1. Record R_avg from Serial Monitor for each test subject
2. Get the subject's lab-measured Hb from a blood test
3. Run polynomial regression:

```python
import numpy as np
R_vals  = [0.45, 0.52, 0.48, ...]
Hb_vals = [14.2, 13.1, 13.8, ...]
coeffs = np.polyfit(R_vals, Hb_vals, 2)
print(f"A={coeffs[0]:.2f}, B={coeffs[1]:.2f}, C={coeffs[2]:.2f}")
```

Replace A, B, C in HbMonitor.ino with the output values.

---

## Usage Instructions

1. Set SPDT switch to correct gender position before measuring
2. Place fingertip pad (soft underside, not nail) firmly over sensor
3. Cover sensor and finger with black tape to block ambient light
4. Hold completely still — movement resets the reading
5. Wait for "Pass X of 6" to complete
6. Hb value and classification appear on OLED

---

## Disclaimer

This device is a research and educational prototype only.
It is NOT a medical device and must NOT be used for clinical diagnosis or treatment.
Always consult a healthcare professional for medical haemoglobin testing.

---

## Project Structure

```
hb-monitor/
├── src/
│   └── HbMonitor/
│       └── HbMonitor.ino
├── docs/
│   └── calibration.py
├── README.md
└── LICENSE
```

---

## References

- Abuzairi et al. (2023). MAX30102 PPG Dataset for Hemoglobin Estimation. Mendeley Data.
- World Health Organization (2011). Haemoglobin concentrations for diagnosis of anaemia.
