# DashStat Hardware Profile & Specification (CYD ESP32-2432S028R)

> **บอร์ดเป้าหมาย:** Cheap Yellow Display (CYD) รุ่น ESP32-2432S028R  
> **สถานะ:** Verified Hardware Profile สำหรับ DashStat

---

## 1. ข้อมูลชิปและประมวลผล (Processor & Memory)
| รายการ | สเปก / ค่าจริง | ผลต่อการออกแบบสถาปัตยกรรม DashStat |
|---|---|---|
| **ชิปหลัก (SoC)** | `ESP32-D0WD-V3` (Revision 3.1) | สถาปัตยกรรม Dual-Core Xtensa 32-bit LX6 @ 240 MHz |
| **Flash Memory** | 4 MB (DIO Mode, 80 MHz) | เพียงพอสำหรับ Firmware + LVGL Assets + Bluetooth Stack |
| **SRAM ภายใน** | ~520 KB (Free Heap ตอนบูต ~320 KB) | มีจำกัด ต้องเฝ้าระวัง Heap Fragmentation อย่างเคร่งครัด |
| **PSRAM** | **0 bytes (ไม่มี PSRAM)** | **กฎเหล็ก:** ห้ามใช้ Full Framebuffer เด็ดขาด บังคับใช้ Partial DMA Buffer |
| **Core 0 Allocation** | BT Transport, CommandGuard, ELM327 Parser | รันการสื่อสารและตัวตรวจความปลอดภัยแยกจาก UI |
| **Core 1 Allocation** | LVGL 9, LovyanGFX DMA, Touch Polling | รัน UI Rendering (~30 FPS) อิสระ ไม่ถูกบล็อกโดย I/O |

---

## 2. ข้อมูลจอแสดงผล (Display & Touch)
| ส่วนประกอบ | รายละเอียด | GPIO Pin Mapping |
|---|---|---|
| **พาเนลจอ (LCD)** | 2.8 นิ้ว TFT LCD ความละเอียด 320×240 (แนวนอน) | Driver: **ILI9341** |
| **SPI Host ของจอ** | SPI2_HOST (HSPI) พร้อมเปิดใช้งาน DMA | SCK: 14, MOSI: 13, MISO: 12, CS: 15, DC: 2 |
| **Backlight Control**| PWM ความถี่ 44.1 kHz (Active-HIGH) | BL: **GPIO 21** |
| **ระบบสัมผัส (Touch)** | Resistive Touch Controller **XPT2046** | SCK: 25, MOSI: 32, MISO: 39, CS: 33, IRQ: 36 |
| **Touch Calibration**| X: 300 - 3900, Y: 3700 - 200 | ค่า Calibration มาตรฐาน (ปรับตั้งใหม่ได้ในเมนู Touch Calibrate) |

---

## 3. อุปกรณ์เสริมบนบอร์ด (On-board Peripherals)
| ชิ้นส่วน | GPIO Pin | สถานะ & โหมดการทำงาน |
|---|---|---|
| **RGB LED (Red)** | `GPIO 4` | Active-LOW (ใช้แจ้งเตือน Critical Overheat/Alert) |
| **RGB LED (Green)**| `GPIO 16` | Active-LOW (ใช้แสดงสถานะ Live Data Link) |
| **RGB LED (Blue)** | `GPIO 17` | Active-LOW (ใช้แสดงสถานะ Bluetooth Connecting) |
| **LDR (Light Sensor)**| `GPIO 34` | Analog In (ใช้วัดแสงเพื่อปรับความสว่างจอกลางวัน/กลางคืน) |
| **Audio DAC/Buzzer** | `GPIO 26` | DAC Output / PWM ขับลำโพงจิ๋วแจ้งเตือน |
| **SD Card Slot** | SPI3_HOST (VSPI) | ขา 18, 19, 23, CS 5 (สำรองไว้ใช้ Data Logging ในอนาคต) |

---

## 4. ตัวแปลง ELM327 Bluetooth (BLE OBD-II Adapter)
- **อุปกรณ์ที่รองรับ:** อะแดปเตอร์ ELM327 แบบ **Bluetooth Low Energy (BLE 4.0 / 5.0)** ทั่วไป (เช่น `vLinker MC+`, `OBDLink CX`, `OBDBLE` หรือโมดูล BLE Transparent UART มาตรฐาน)
- **ประเภทการเชื่อมต่อ:** **Bluetooth Low Energy (BLE 4.0+)** — *รองรับการสแกนและเชื่อมต่ออัตโนมัติในเมนู Bluetooth OBD*
- **Stack ฝั่ง ESP32:** **NimBLE-Arduino** (ประหยัดแรมเหมาะสำหรับบอร์ด CYD ที่ไร้ PSRAM)
- **GATT Service ที่รองรับ:** Transparent UART Service (`0xFFF0`, `0xFFE0`), Nordic UART (`6E400001-...`) และโหมดสแกนค้นหา Custom Service อัตโนมัติ
- **โพรโทคอลรถที่รองรับ:** มาตรฐาน OBD-II (ISO 15765-4 CAN 11/29-bit, KWP2000, ISO 9141-2, SAE J1850)
