#pragma once
#include <stdint.h>
#include <stddef.h>

enum class TransportState : uint8_t {
    DISCONNECTED = 0,
    SCANNING,
    CONNECTING,
    CONNECTED,
    RECONNECTING
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool begin() = 0;
    virtual bool connect(const char* targetAddress) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual size_t read(uint8_t* buffer, size_t maxLen) = 0;
    virtual size_t available() const = 0;

    virtual TransportState getState() const = 0;
    virtual const char* getConnectedAddress() const = 0;
};
