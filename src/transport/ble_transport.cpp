#ifndef UNIT_TEST
#include "transport/ble_transport.h"
#include <Arduino.h>
#include <cstring>
#include <algorithm>

// Notification callback trampoline
static void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    (void)pChar;
    (void)isNotify;
    BleTransport::instance().onDataReceived(pData, length);
}

// Client callbacks to detect abrupt disconnection (NimBLE task context: flag Core 0 only)
class DashStatClientCallbacks : public NimBLEClientCallbacks {
public:
    void onDisconnect(NimBLEClient* pClient, int reason) override {
        (void)pClient;
        (void)reason;
        Serial.println("[BleTransport] Disconnected from OBD peer!");
        BleTransport::instance().onPeerDisconnected();
    }
};
static DashStatClientCallbacks s_clientCallbacks;

// Scan callbacks to discover nearby OBD BLE adapters
class DashStatScanCallbacks : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        std::string name = advertisedDevice->getName();
        std::string addr = advertisedDevice->getAddress().toString();
        int rssi = advertisedDevice->getRSSI();
        uint8_t addrType = advertisedDevice->getAddressType();

        // Open up all names per user request! Fallback to "BLE DEVICE" if empty
        const char* devName = (name.length() > 0) ? name.c_str() : "BLE DEVICE";
        BleTransport::instance().addDiscoveredDevice(devName, addr.c_str(), rssi, addrType);
    }

    void onScanEnd(const NimBLEScanResults& scanResults, int reason) override {
        (void)scanResults;
        (void)reason;
        BleTransport::instance().onScanComplete();
    }
};
static DashStatScanCallbacks s_scanCallbacks;

BleTransport::BleTransport() {
    state_.store(TransportState::DISCONNECTED);
    ringHead_.store(0, std::memory_order_relaxed);
    ringTail_.store(0, std::memory_order_relaxed);
    discoveredCount_.store(0);
    isScanning_.store(false);
}

BleTransport::~BleTransport() {
    disconnect();
}

bool BleTransport::begin() {
    NimBLEDevice::init("DashStat");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max TX power for robust in-car reception
    state_.store(TransportState::DISCONNECTED);
    Serial.printf("[BleTransport] Initialized NimBLE. FreeHeap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    return true;
}

void BleTransport::startScan(uint32_t durationSec) {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&s_scanCallbacks, false);
    pScan->setActiveScan(true); // Active scan requests Scan Response (vital to fetch names)
    pScan->setInterval(100);
    pScan->setWindow(80);
    discoveredCount_.store(0);
    isScanning_.store(true);
    state_.store(TransportState::SCANNING);

    Serial.printf("[BleTransport] Starting async BLE Scan for %lu seconds...\n", (unsigned long)durationSec);
    // NimBLE 2.x duration is in MILLISECONDS!
    bool started = pScan->start(durationSec * 1000, false);
    if (!started) {
        isScanning_.store(false);
        state_.store(TransportState::DISCONNECTED);
        Serial.println("[BleTransport] Failed to start BLE scan!");
    }
}

void BleTransport::stopScan() {
    if (isScanning_.load()) {
        NimBLEDevice::getScan()->stop();
        isScanning_.store(false);
        state_.store(TransportState::DISCONNECTED);
        Serial.println("[BleTransport] BLE Scan stopped.");
    }
}

void BleTransport::onScanComplete() {
    isScanning_.store(false);
    state_.store(TransportState::DISCONNECTED);
    Serial.printf("[BleTransport] Scan finished. Found %lu devices.\n", (unsigned long)discoveredCount_.load());
}

void BleTransport::addDiscoveredDevice(const char* name, const char* address, int rssi, uint8_t addressType) {
    if (!address || address[0] == '\0') return;

    const char* safeName = (name && name[0] != '\0') ? name : "BLE DEVICE";
    bool updatedName = false;
    bool addedNew = false;

    portENTER_CRITICAL(&scanMux_);
    size_t count = discoveredCount_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < count; i++) {
        if (std::strcmp(discovered_[i].address, address) == 0) {
            if (std::strcmp(discovered_[i].name, "BLE DEVICE") == 0 && std::strcmp(safeName, "BLE DEVICE") != 0) {
                std::strncpy(discovered_[i].name, safeName, sizeof(discovered_[0].name) - 1);
                discovered_[i].name[sizeof(discovered_[0].name) - 1] = '\0';
                updatedName = true;
            }
            portEXIT_CRITICAL(&scanMux_);
            if (updatedName) {
                Serial.printf("[BleTransport] Updated name: %s (%s)\n", safeName, address);
            }
            return;
        }
    }

    if (count < MAX_DISCOVERED) {
        std::strncpy(discovered_[count].name, safeName, sizeof(discovered_[0].name) - 1);
        discovered_[count].name[sizeof(discovered_[0].name) - 1] = '\0';
        std::strncpy(discovered_[count].address, address, sizeof(discovered_[0].address) - 1);
        discovered_[count].address[sizeof(discovered_[0].address) - 1] = '\0';
        discovered_[count].rssi = rssi;
        discovered_[count].addressType = addressType;
        discoveredCount_.store(count + 1, std::memory_order_release);
        addedNew = true;
    }
    portEXIT_CRITICAL(&scanMux_);

    if (addedNew) {
        Serial.printf("[BleTransport] Discovered: %s (%s, Type: %u, RSSI: %d)\n",
                      safeName, address, addressType, rssi);
    }
}

bool BleTransport::getDiscoveredInfo(size_t index, char* outName, size_t nameLen, char* outAddr, size_t addrLen) const {
    portENTER_CRITICAL(&scanMux_);
    size_t count = discoveredCount_.load(std::memory_order_acquire);
    if (index >= count) {
        portEXIT_CRITICAL(&scanMux_);
        if (outName && nameLen > 0) outName[0] = '\0';
        if (outAddr && addrLen > 0) outAddr[0] = '\0';
        return false;
    }
    if (outName && nameLen > 0) {
        std::strncpy(outName, discovered_[index].name, nameLen - 1);
        outName[nameLen - 1] = '\0';
    }
    if (outAddr && addrLen > 0) {
        std::strncpy(outAddr, discovered_[index].address, addrLen - 1);
        outAddr[addrLen - 1] = '\0';
    }
    portEXIT_CRITICAL(&scanMux_);
    return true;
}

bool BleTransport::connect(const char* targetAddress) {
    if (!targetAddress || std::strlen(targetAddress) == 0) {
        return false;
    }

    processEvents();
    std::strncpy(address_, targetAddress, sizeof(address_) - 1);
    address_[sizeof(address_) - 1] = '\0';

    disconnectPending_.store(false, std::memory_order_release);
    state_.store(TransportState::CONNECTING, std::memory_order_release);
    Serial.printf("[BleTransport] Connecting to OBD BLE: %s ...\n", address_);

    if (!pClient_) {
        pClient_ = NimBLEDevice::createClient();
        pClient_->setClientCallbacks(&s_clientCallbacks, false);
        pClient_->setConnectTimeout(2500); // Bound connect stall to 2.5s
        pClient_->setConnectRetries(0);    // Single attempt per call; Core 0 loop handles backoff
        pClient_->setConnectionParams(6, 12, 0, 51); // 7.5ms-15ms interval for lowest latency OBD polling
    }

    // Determine address type (default to PUBLIC, but check discovered list)
    uint8_t addrType = BLE_ADDR_PUBLIC;
    portENTER_CRITICAL(&scanMux_);
    size_t count = discoveredCount_.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; i++) {
        if (std::strcmp(discovered_[i].address, address_) == 0) {
            addrType = discovered_[i].addressType;
            break;
        }
    }
    portEXIT_CRITICAL(&scanMux_);

    NimBLEAddress peerAddr(std::string(address_), addrType);
    bool connected = pClient_->connect(peerAddr);

    if (!connected) {
        Serial.println("[BleTransport] Connection failed!");
        state_.store(TransportState::DISCONNECTED);
        if (pClient_) {
            pClient_->disconnect();
        }
        return false;
    }

    Serial.println("[BleTransport] Connected to peer. Discovering GATT Services...");

    // Discover OBD services: Prioritize known OBD profiles, skip SIG metadata services (0x1800/0x1801/0x180A)
    pTxChar_ = nullptr;
    pRxChar_ = nullptr;

    static const NimBLEUUID uuidFff0(uint16_t(0xFFF0));
    static const NimBLEUUID uuidFfe0(uint16_t(0xFFE0));
    static const NimBLEUUID uuidNordic("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    static const NimBLEUUID uuidSigGenericAccess(uint16_t(0x1800));
    static const NimBLEUUID uuidSigGenericAttr(uint16_t(0x1801));
    static const NimBLEUUID uuidSigDeviceInfo(uint16_t(0x180A));

    NimBLERemoteService* pTargetService = pClient_->getService(uuidFff0);
    if (!pTargetService) pTargetService = pClient_->getService(uuidFfe0);
    if (!pTargetService) pTargetService = pClient_->getService(uuidNordic);

    if (pTargetService) {
        Serial.printf("[BleTransport] Found known OBD Service: %s\n", pTargetService->getUUID().toString().c_str());
        for (auto* pChar : pTargetService->getCharacteristics(true)) {
            if ((pChar->canWrite() || pChar->canWriteNoResponse()) && !pTxChar_) {
                pTxChar_ = pChar;
            }
            if ((pChar->canNotify() || pChar->canIndicate()) && !pRxChar_) {
                pRxChar_ = pChar;
            }
        }
    } else {
        Serial.println("[BleTransport] Known OBD service not found, scanning custom services...");
        for (auto* pService : pClient_->getServices(true)) {
            NimBLEUUID svcUuid = pService->getUUID();
            if (svcUuid == uuidSigGenericAccess || svcUuid == uuidSigGenericAttr || svcUuid == uuidSigDeviceInfo) {
                continue;
            }
            for (auto* pChar : pService->getCharacteristics(true)) {
                if ((pChar->canWrite() || pChar->canWriteNoResponse()) && !pTxChar_) {
                    pTxChar_ = pChar;
                }
                if ((pChar->canNotify() || pChar->canIndicate()) && !pRxChar_) {
                    pRxChar_ = pChar;
                }
            }
            if (pTxChar_ && pRxChar_) break;
        }
    }

    if (!pTxChar_ || !pRxChar_) {
        Serial.println("[BleTransport] Failed to find valid TX/RX characteristics!");
        pClient_->disconnect();
        state_.store(TransportState::DISCONNECTED);
        return false;
    }

    // Subscribe to RX notifications
    if (pRxChar_->canNotify()) {
        pRxChar_->subscribe(true, notifyCallback);
    } else if (pRxChar_->canIndicate()) {
        pRxChar_->subscribe(false, notifyCallback);
    }

    ringHead_.store(0, std::memory_order_relaxed);
    ringTail_.store(0, std::memory_order_relaxed);
    state_.store(TransportState::CONNECTED);

    Serial.printf("[BleTransport] BLE Handshake Complete! FreeHeap: %lu | MaxAlloc: %lu bytes\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap());

    return true;
}

void BleTransport::onPeerDisconnected() {
    state_.store(TransportState::DISCONNECTED, std::memory_order_release);
    disconnectPending_.store(true, std::memory_order_release);
}

void BleTransport::cleanupConnection() {
    if (pClient_ && pClient_->isConnected()) {
        pClient_->disconnect();
    }
    pTxChar_ = nullptr;
    pRxChar_ = nullptr;
    ringHead_.store(0, std::memory_order_release);
    ringTail_.store(0, std::memory_order_release);
    state_.store(TransportState::DISCONNECTED, std::memory_order_release);
}

void BleTransport::processEvents() {
    if (disconnectPending_.exchange(false, std::memory_order_acq_rel)) {
        cleanupConnection();
    }
}

void BleTransport::disconnect() {
    state_.store(TransportState::DISCONNECTED, std::memory_order_release);
    disconnectPending_.store(true, std::memory_order_release);
}

bool BleTransport::isConnected() const {
    return !disconnectPending_.load(std::memory_order_acquire) &&
           (state_.load(std::memory_order_acquire) == TransportState::CONNECTED) &&
           pClient_ && pClient_->isConnected();
}

size_t BleTransport::write(const uint8_t* data, size_t len) {
    if (!isConnected() || !pTxChar_ || !data || len == 0) {
        return 0;
    }
    bool noResp = pTxChar_->canWriteNoResponse();
    bool success = pTxChar_->writeValue(data, len, !noResp);
    return success ? len : 0;
}

size_t BleTransport::read(uint8_t* buffer, size_t maxLen) {
    if (!buffer || maxLen == 0) return 0;

    size_t count = 0;
    size_t tail = ringTail_.load(std::memory_order_relaxed);
    size_t head = ringHead_.load(std::memory_order_acquire);

    while (count < maxLen && tail != head) {
        buffer[count++] = ringBuf_[tail];
        tail = (tail + 1) % RING_BUF_SIZE;
    }
    ringTail_.store(tail, std::memory_order_release);
    return count;
}

size_t BleTransport::available() const {
    size_t head = ringHead_.load(std::memory_order_acquire);
    size_t tail = ringTail_.load(std::memory_order_relaxed);
    if (head >= tail) {
        return head - tail;
    } else {
        return RING_BUF_SIZE - (tail - head);
    }
}

void BleTransport::onDataReceived(const uint8_t* data, size_t len) {
    if (!data || len == 0 || disconnectPending_.load(std::memory_order_acquire) ||
        state_.load(std::memory_order_acquire) != TransportState::CONNECTED) {
        return;
    }

    bool hasPrompt = false;
    size_t head = ringHead_.load(std::memory_order_relaxed);
    size_t tail = ringTail_.load(std::memory_order_acquire);

    for (size_t i = 0; i < len; i++) {
        if (data[i] == '>') hasPrompt = true;
        size_t nextHead = (head + 1) % RING_BUF_SIZE;
        if (nextHead != tail) { // Don't overflow ring buffer
            ringBuf_[head] = data[i];
            head = nextHead;
        } else {
            // Ring buffer full; drop remaining bytes in this notification packet
            break;
        }
    }
    ringHead_.store(head, std::memory_order_release);
    if (hasPrompt) {
        TaskHandle_t h = notifyTask_.load(std::memory_order_acquire);
        if (h) xTaskNotifyGive(h);
    }
}

#endif
