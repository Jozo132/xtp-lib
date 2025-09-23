#pragma once

#include <stdint.h>
#include <stdbool.h>

uint32_t _iec_ms = 0;

void iec_sync(uint32_t ms) {
    _iec_ms = ms;
}

class IECTimer {
public:
    uint32_t T;
    uint32_t ET;
    bool IN;
    bool OUT;
    void reset(uint32_t T = 0) {
        if (T > 0) this->T = T;
        this->ET = 0;
        this->IN = false;
        this->OUT = false;
    }
};

class TON : IECTimer {
public:
    TON(uint32_t T) { reset(T); }
    bool update(bool IN, uint32_t elapsed = 0) {
        this->IN = IN;
        if (IN) {
            if (this->ET < this->T)
                this->ET += elapsed > 0 ? elapsed : _iec_ms;
            else
                this->OUT = true;
        } else {
            this->ET = 0;
            this->OUT = false;
        }
        return this->OUT;
    }
};

class TOF : IECTimer {
public:
    TOF(uint32_t T) { reset(T); }
    bool update(bool IN, uint32_t elapsed = 0) {
        this->IN = IN;
        if (IN) {
            this->ET = 0;
            this->OUT = false;
        } else {
            if (this->ET < this->T)
                this->ET += elapsed > 0 ? elapsed : _iec_ms;
            else
                this->OUT = true;
        }
        return this->OUT;
    }
};



struct DiffUP {
    bool OUT = false;
    bool update(bool IN) {
        if (IN && !OUT) OUT = true;
        else if (!IN) OUT = false;
        return OUT;
    }
};

struct DiffDOWN {
    bool OUT = false;
    bool update(bool IN) {
        if (!IN && OUT) OUT = true;
        else if (IN) OUT = false;
        return OUT;
    }
};