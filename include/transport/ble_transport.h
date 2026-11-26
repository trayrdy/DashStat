#pragma once
#ifndef UNIT_TEST
#include "transport/transport_interface.h"
#include <NimBLEDevice.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>

class BleTransport : public ITransport {
public:
    static BleTransport& instance() {
        static BleTransport inst;
        return inst;
    }

    bool begin() override;
    bool connect(const char* targetAddress) override;
    void disconnect() override;
    bool isConnected() const override;

    size_t write(const uint8_t* data, size_t len) override;
    size_t read(uint8_t* buffer, size_t maxLen) override;
    size_t available() const override;

    TransportState getState() const override { return state_.load(); }
    const char* getConnectedAddress() const override { return address_; }

    // BLE Scan & Core 0 lifecycle support
    void startScan(uint32_t durationSec = 5);
    void stopScan();
    bool isScanning() const { return isScanning_.load(); }
    size_t getDiscoveredCount() const { return discoveredCount_.load(); }
    bool getDiscoveredInfo(size_t index, char* outName, size_t nameLen, char* outAddr, size_t addrLen) const;
    void onScanComplete();
    void processEvents();
    void onPeerDisconnected();
    void setNotifyTask(TaskHandle_t h) { notifyTask_.store(h, std::memory_order_release); }

    // Internal notification callback
    void onDataReceived(const uint8_t* data, size_t len);
    void addDiscoveredDevice(const char* name, const char* address, int rssi, uint8_t addressType = 0);

private:
    BleTransport();
    ~BleTransport() override;
    void cleanupConnection();

    std::atomic<TransportState> state_{TransportState::DISCONNECTED};
    std::atomic<bool> disconnectPending_{false};
    char address_[18] = "";

    NimBLEClient* pClient_ = nullptr;
    NimBLERemoteCharacteristic* pTxChar_ = nullptr;
    NimBLERemoteCharacteristic* pRxChar_ = nullptr;

    // Static zero-alloc ring buffer for incoming stream
    static constexpr size_t RING_BUF_SIZE = 512;
    uint8_t ringBuf_[RING_BUF_SIZE];
    std::atomic<size_t> ringHead_{0};
    std::atomic<size_t> ringTail_{0};

    // Discovered devices table (guarded by scanMux_ across NimBLE & UI cores)
    static constexpr size_t MAX_DISCOVERED = 10;
    struct DiscoveredDev {
        char name[32];
        char address[18];
        uint8_t addressType = 0; // 0=Public, 1=Random
        int rssi = 0;
    };
    DiscoveredDev discovered_[MAX_DISCOVERED];
    std::atomic<size_t> discoveredCount_{0};
    std::atomic<bool> isScanning_{false};
    std::atomic<TaskHandle_t> notifyTask_{nullptr};
    mutable portMUX_TYPE scanMux_ = portMUX_INITIALIZER_UNLOCKED;
};
#endif
