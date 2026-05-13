"""
calibration.py
==============
Use this script to refit the regression coefficients A, B, C
after collecting paired (R_avg, Hb_lab) measurements from your sensor.

How to collect data:
1. Open Arduino Serial Monitor at 9600 baud
2. For each subject, note the R_avg value once stable=6 is reached
3. Get the subject's Hb from a lab blood test (CBC report)
4. Add both values to the lists below
5. Run this script — copy the printed A, B, C into HbMonitor.ino

Minimum recommended: 15 subjects spanning Hb 8–17 g/dL
"""

import numpy as np
import matplotlib.pyplot as plt

# ── Add your collected data here ──────────────────────────────
# R_avg values from Serial Monitor (stable readings only)
R_vals = [
    0.393,  # subject 1
    0.452,  # subject 2
    0.511,  # subject 3
    0.528,  # subject 4
    0.576,  # subject 5
    # add more...
]

# Corresponding lab Hb values (g/dL) from blood test
Hb_vals = [
    11.2,   # subject 1
    12.5,   # subject 2
    13.0,   # subject 3
    13.4,   # subject 4
    14.2,   # subject 5
    # add more...
]
# ─────────────────────────────────────────────────────────────

assert len(R_vals) == len(Hb_vals), "R_vals and Hb_vals must have same length"

R = np.array(R_vals)
H = np.array(Hb_vals)

# Fit degree-2 polynomial
coeffs = np.polyfit(R, H, 2)
A, B, C = coeffs

print("=" * 45)
print("  REGRESSION RESULTS")
print("=" * 45)
print(f"  Hb = {A:.4f}*R^2 + {B:.4f}*R + {C:.4f}")
print()
print("  Copy these into HbMonitor.ino:")
print(f"  const float A = {A:.2f};")
print(f"  const float B = {B:.2f};")
print(f"  const float C = {C:.2f};")
print("=" * 45)

# Accuracy metrics
pred = np.polyval(coeffs, R)
mae  = np.mean(np.abs(pred - H))
rmse = np.sqrt(np.mean((pred - H) ** 2))
ss_res = np.sum((pred - H) ** 2)
ss_tot = np.sum((H - np.mean(H)) ** 2)
r2 = 1 - ss_res / ss_tot

print(f"\n  MAE  = {mae:.3f} g/dL")
print(f"  RMSE = {rmse:.3f} g/dL")
print(f"  R²   = {r2:.4f}")

# ── Predictions vs actual ──
print("\n  Subject predictions:")
print(f"  {'R_avg':>8}  {'Hb_lab':>8}  {'Hb_pred':>8}  {'Error':>8}")
for r, h, p in zip(R_vals, Hb_vals, pred):
    print(f"  {r:8.4f}  {h:8.1f}  {p:8.1f}  {p-h:+8.2f}")

# ── Plot ──
R_line = np.linspace(min(R) - 0.05, max(R) + 0.05, 200)
H_line = np.polyval(coeffs, R_line)

plt.figure(figsize=(7, 5))
plt.scatter(R_vals, Hb_vals, color='royalblue', s=60, zorder=5, label='Measured data')
plt.plot(R_line, H_line, color='tomato', linewidth=2,
         label=f'Fit: Hb={A:.2f}R²+{B:.2f}R+{C:.2f}')
plt.xlabel('R_avg (AC/DC ratio)', fontsize=12)
plt.ylabel('Hemoglobin (g/dL)', fontsize=12)
plt.title('Hemoglobin Regression Calibration', fontsize=13)
plt.legend()
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.savefig('calibration_curve.png', dpi=150)
plt.show()
print("\n  Plot saved as calibration_curve.png")
