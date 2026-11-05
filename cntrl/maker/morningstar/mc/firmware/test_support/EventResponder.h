#pragma once
class EventResponder {
public:
    bool clearEvent() { bool old = triggered_; triggered_ = false; return old; }
    void triggerEvent() { triggered_ = true; }
    operator bool() const { return triggered_; }
private:
    bool triggered_ = false;
};
using EventResponderRef = EventResponder &;
