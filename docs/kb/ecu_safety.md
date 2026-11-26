# ECU Safety & Read-Only Specification (Rule R8)

> **ระดับความสำคัญ:** วิกฤตสูงสุด (Critical Mandate)  
> **นโยบาย:** DashStat เป็นอุปกรณ์ "อ่านอย่างเดียว" (Observer Only) 100% ห้ามแก้ไขสถานะใด ๆ ของรถ

---

## 1. ชั้นการป้องกัน 3 ระดับ (3-Tier Protection)
1. **Tier 1 — Policy Level:** ไม่มีฟังก์ชันการลบโค้ด DTC (Mode 04), สั่ง Actuator ทำงาน (Mode 08) หรือเขียน Memory บรรจุอยู่ในโค้ด
2. **Tier 2 — Code Gate (`CommandGuard`):** ทุกคำสั่งที่จะส่งออกผ่าน Bluetooth Transport ต้องผ่าน `CommandGuard::validate()` เท่านั้น หากไม่ผ่าน Whitelist ฟังก์ชันส่งข้อมูลจะปฏิเสธทันที
3. **Tier 3 — Automated Unit Test:** มี Unit Test บนสภาวะจำลอง (`pio test -e native`) ตรวจสอบว่าคำสั่งต้องห้ามทุกตัวต้องถูกบล็อก และการทดสอบนี้ต้องผ่าน 100% ทุกครั้งที่มีการ Build

---

## 2. Whitelist Matrix (คำสั่งที่อนุญาต)
| โหมด / คำสั่ง | ความหมาย | วัตถุประสงค์ |
|---|---|---|
| `01xx` | OBD Mode 01 Request | อ่านค่า Realtime Diagnostics (RPM, Speed, Coolant, Load ฯลฯ) |
| `03` | OBD Mode 03 Request | อ่านรายการโค้ดปัญหาที่บันทึกไว้ (Stored DTC) |
| `07` | OBD Mode 07 Request | อ่านรายการโค้ดปัญหาที่รอยืนยัน (Pending DTC) |
| `09xx` | OBD Mode 09 Request | อ่านข้อมูลยานพาหนะ (VIN, Calibration ID) |
| `ATZ / ATWS` | Reset ELM327 | รีเซ็ตสถานะชิป ELM327 เท่านั้น (ไม่มีผลต่อ ECU) |
| `ATE0 / ATE1` | Echo Control | ปิด/เปิดการสะท้อนคำสั่งของ ELM327 |
| `ATL0 / ATL1` | Line Feed Control | ปิด/เปิดการขึ้นบรรทัดใหม่ |
| `ATS0 / ATS1` | Spaces Control | ปิด/เปิดช่องว่างใน Payload |
| `ATH0 / ATH1` | Header Control | ปิด/เปิด CAN Header |
| `ATSPx` | Set Protocol | ล็อกโพรโทคอล เช่น ATSP6 (CAN 11-bit 500k) |
| `ATATx` | Adaptive Timing | ปรับ Timing ของ ELM327 ให้ตอบสนองเร็วขึ้น |
| `ATSTxx` | Set Timeout | กำหนด Timeout ป้องกันรอข้อมูลนานเกินไป |
| `ATRV` | Read Voltage | อ่านแรงดันไฟเลี้ยงที่ขา OBD2 ของตัวแปลง |
| `ATI / AT@1` | Chip Info | อ่านเวอร์ชันและ Signature ของตัวแปลง |

---

## 3. Blacklist Matrix (คำสั่งต้องห้ามถาวร — ห้ามปลดล็อกเด็ดขาด)
| โหมด / คำสั่ง | ความหมาย | ความเสี่ยงต่อยานพาหนะ |
|---|---|---|
| `04` / `14` | Clear DTCs / Reset Monitors | ลบประวัติบกพร่อง, รีเซ็ตสถานะความพร้อมมลพิษ |
| `08` | Control On-board System | สั่งกระตุ้น Actuator โดยตรง (เช่น ปั๊มติ๊ก, พัดลม) |
| `10` | Diagnostic Session Control | บังคับให้ ECU เปลี่ยนโหมดการทำงาน |
| `11` | ECU Reset | รีเซ็ตกล่อง ECU ขณะรถอาจกำลังวิ่งอยู่ |
| `27` | Security Access | ปลดล็อกระดับความปลอดภัยเพื่อเตรียมเขียน Flash |
| `28` | Communication Control | สั่งปิดการสื่อสารของกล่อง ECU ในระบบ CAN |
| `2E` | Write Data By Identifier | เขียนค่าหรือพารามิเตอร์ลงหน่วยความจำของ ECU |
| `2F` | Input/Output Control | ควบคุมสัญญาณ I/O แทน ECU |
| `31` | Routine Control | สั่งประมวลผล Routine ในกล่อง ECU |
| `34` – `37`, `3D` | Memory Download/Upload | แฟลชรอมหรือเขียนโปรแกรมทับ ECU |
| `85` | Control DTC Setting | ปิดการบันทึกข้อผิดพลาดของ ECU |
| `ATMA`, `ATBD`, `STPX` | Raw CAN Inject / Dump | ส่งข้อมูล CAN ดิบที่อาจเลี่ยงการตรวจของ Guard |
