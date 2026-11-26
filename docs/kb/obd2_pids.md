# OBD2 PIDs & ELM327 Tuning Reference

> **อ้างอิง:** มาตรฐาน SAE J1979 / ISO 15765-4 CAN 11-bit 500 kbaud  
> **เป้าหมาย:** สรุปรายการ PID สำคัญ, สูตรการแปลงค่า, และเทคนิคการลด Latency

---

## 1. สูตรการแปลงค่า PID มาตรฐาน (Mode 01)
| PID | คำอธิบาย | ไบต์ข้อมูล | สูตรคำนวณ | หน่วย | ช่วงค่า | ความสำคัญใน DashStat |
|---|---|---|---|---|---|---|
| `010C` | Engine RPM | 2 (A, B) | `((A * 256) + B) / 4` | RPM | 0 – 16,383.75 | **สูงสุด (10–20 Hz)** |
| `010D` | Vehicle Speed | 1 (A) | `A` | km/h | 0 – 255 | **สูง (10–20 Hz)** |
| `0105` | Engine Coolant Temp | 1 (A) | `A - 40` | °C | -40 – 215 | **ปานกลาง (2–5 Hz)** |
| `0111` | Throttle Position | 1 (A) | `(A * 100) / 255` | % | 0 – 100 | **สูง (10–20 Hz)** |
| `0104` | Calculated Engine Load | 1 (A) | `(A * 100) / 255` | % | 0 – 100 | **ปานกลาง (2–5 Hz)** |
| `010F` | Intake Air Temp | 1 (A) | `A - 40` | °C | -40 – 215 | **ต่ำ (1 Hz)** |
| `012F` | Fuel Tank Level | 1 (A) | `(A * 100) / 255` | % | 0 – 100 | **ต่ำ (0.2 Hz)** |
| `ATRV` | Battery / Alt Voltage | String | อ่านตรงจาก ELM (เช่น `14.2V`) | Volt | 0 – 24.0 | **ต่ำ (1 Hz)** |

---

## 2. Multi-PID Request บนระบบ CAN Bus
ระบบ CAN รองรับการส่งคำขอหลาย PID พร้อมกันในเฟรมเดียว (Request Frame ขนาด 8 ไบต์):
- ส่ง: `010C0D051`
  - `01`: Mode 01
  - `0C`: RPM (2 ไบต์)
  - `0D`: Speed (1 ไบต์)
  - `05`: Coolant (1 ไบต์)
  - `1`: Hint แจ้ง ELM327 ว่าคาดหวัง 1 Response frame (ตัด Latency การรอ Timeout)
- ผลลัพธ์: ECU ตอบกลับเฟรมเดียวที่มีทั้ง 3 ค่าพร้อมกัน ลดรอบการสื่อสาร (Round-Trip) จาก 3 ครั้งเหลือเพียง 1 ครั้งทันที!

---

## 3. Zero-Allocation Hex Parser Architecture
เพื่อป้องกัน Heap Fragmentation บน ESP32 ที่ไม่มี PSRAM:
- ห้ามใช้ `String.substring()` หรือ `strtok()`
- แปลงรหัส ASCII Hex ตรงเข้าตัวแปรตัวเลขแบบ In-Place:
```cpp
inline uint8_t hex2byte(char high, char low) {
    auto h = (high >= 'A') ? (high - 'A' + 10) : (high - '0');
    auto l = (low >= 'A') ? (low - 'A' + 10) : (low - '0');
    return (h << 4) | l;
}
```
