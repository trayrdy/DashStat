#pragma once
#include "transport/transport_interface.h"
#include <string>
#include <vector>

class MockTransport : public ITransport {
public:
    MockTransport();

    bool begin() override;
    bool connect(const char* targetAddress) override;
    void disconnect() override;
    bool isConnected() const override;

    size_t write(const uint8_t* data, size_t len) override;
    size_t read(uint8_t* buffer, size_t maxLen) override;
    size_t available() const override;

    TransportState getState() const override { return state_; }
    const char* getConnectedAddress() const override { return address_.c_str(); }

    // Test helpers
    void setState(TransportState state) { state_ = state; }
    void feedResponse(const char* resp);
    const std::string& getLastWritten() const { return lastWritten_; }
    void clearLastWritten() { lastWritten_.clear(); }

private:
    TransportState state_ = TransportState::DISCONNECTED;
    std::string address_;
    std::string rxBuffer_;
    std::string lastWritten_;
};
