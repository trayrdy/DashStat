#include "transport/mock_transport.h"
#include <cstring>
#include <algorithm>

MockTransport::MockTransport() {
    state_ = TransportState::DISCONNECTED;
}

bool MockTransport::begin() {
    state_ = TransportState::DISCONNECTED;
    return true;
}

bool MockTransport::connect(const char* targetAddress) {
    if (!targetAddress || strlen(targetAddress) == 0) {
        state_ = TransportState::DISCONNECTED;
        return false;
    }
    address_ = targetAddress;
    state_ = TransportState::CONNECTED;
    return true;
}

void MockTransport::disconnect() {
    state_ = TransportState::DISCONNECTED;
    rxBuffer_.clear();
}

bool MockTransport::isConnected() const {
    return state_ == TransportState::CONNECTED;
}

size_t MockTransport::write(const uint8_t* data, size_t len) {
    if (!data || len == 0 || !isConnected()) return 0;
    lastWritten_.assign(reinterpret_cast<const char*>(data), len);
    return len;
}

size_t MockTransport::read(uint8_t* buffer, size_t maxLen) {
    if (!buffer || maxLen == 0 || rxBuffer_.empty()) return 0;
    size_t toRead = std::min(maxLen, rxBuffer_.size());
    std::memcpy(buffer, rxBuffer_.data(), toRead);
    rxBuffer_.erase(0, toRead);
    return toRead;
}

size_t MockTransport::available() const {
    return rxBuffer_.size();
}

void MockTransport::feedResponse(const char* resp) {
    if (resp) {
        rxBuffer_.append(resp);
    }
}
