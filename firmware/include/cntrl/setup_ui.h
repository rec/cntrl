#pragma once
#include "documents.h"

namespace cntrl {
// Foot-operated management. A/B browse or adjust, C confirms, F goes back.
class SetupUi {
public:
    SetupUi(Documents &documents, Hardware &hardware) : docs_(documents), hardware_(hardware) {}
    bool open() const { return mode_ != Mode::closed; }
    void enter() {
        if (docs_.busy()) return;
        mode_ = Mode::catalogue; locked_ = true;
        std::snprintf(name_, sizeof(name_), "%s", docs_.selected());
        if (!name_[0]) docs_.next_valid("", name_);
        render();
    }
    void poll(uint8_t held) {
        if (locked_ && !held) locked_ = false;
        if (mode_ == Mode::saving && !docs_.busy()) {
            if (docs_.last_error()[0]) { mode_ = Mode::failed; render(); }
            else { mode_ = Mode::closed; docs_.redraw(); }
        }
    }
    void button(unsigned button, bool down, uint32_t now) {
        if (!down || locked_ || mode_ == Mode::saving) return;
        if (mode_ == Mode::failed) { if (button == 2 || button == 5) close(); return; }
        if (mode_ == Mode::catalogue) {
            if (button < 2) browse(button == 0);
            else if (button == 2 && name_[0]) { mode_ = Mode::action; item_ = 0; }
            else if (button == 5) { close(); return; }
        } else if (mode_ == Mode::action) {
            if (button < 2) item_ = 1 - item_;
            else if (button == 5) mode_ = Mode::catalogue;
            else if (button == 2) {
                if (item_ == 0) {
                    if (!docs_.select(name_)) { hardware_.error("Selection failed"); return; }
                    close(); return;
                }
                if (name_compare(name_, docs_.selected())) {
                    if (!docs_.select(name_)) { hardware_.error("Selection failed"); return; }
                }
                if (!docs_.edit_begin()) return;
                mode_ = Mode::patch; patch_ = field_ = 0;
            }
        } else if (mode_ == Mode::patch) {
            if (button < 2) patch_ = (patch_ + docs_.document().patches() + (button == 0 ? -1 : 1)) % docs_.document().patches();
            else if (button == 2) { mode_ = Mode::field; field_ = 0; }
            else if (button == 5) { mode_ = Mode::decision; item_ = 2; }
        } else if (mode_ == Mode::field) {
            if (button < 2) field_ = (field_ + 7 + (button == 0 ? -1 : 1)) % 7;
            else if (button == 2) mode_ = Mode::value;
            else if (button == 5) mode_ = Mode::patch;
        } else if (mode_ == Mode::value) {
            if (button < 2) docs_.edit_adjust(patch_, field_, button == 0 ? -1 : 1);
            else if (button == 2 || button == 5) mode_ = Mode::field;
        } else if (mode_ == Mode::decision) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5) mode_ = Mode::patch;
            else if (button == 2) {
                if (item_ == 0) { if (docs_.edit_save(now)) mode_ = Mode::saving; else { hardware_.error("Save failed"); return; } }
                else if (item_ == 1) { docs_.edit_cancel(); close(); return; }
                else mode_ = Mode::patch;
            }
        }
        render();
    }
private:
    enum class Mode { closed, catalogue, action, patch, field, value, decision, saving, failed };
    Documents &docs_; Hardware &hardware_;
    Mode mode_ = Mode::closed;
    bool locked_ = false;
    char name_[64] = {};
    unsigned item_ = 0, patch_ = 0, field_ = 0;
    void close() { mode_ = Mode::closed; docs_.redraw(); }
    void browse(bool backward) {
        char cursor[64] = {}, next[64], first[64] = {}, last[64] = {}, previous[64] = {}, following[64] = {};
        while (docs_.next_valid(cursor, next)) {
            if (!first[0]) std::snprintf(first, sizeof(first), "%s", next);
            if (name_compare(next, name_) < 0) std::snprintf(previous, sizeof(previous), "%s", next);
            if (!following[0] && name_compare(next, name_) > 0) std::snprintf(following, sizeof(following), "%s", next);
            std::snprintf(last, sizeof(last), "%s", next);
            std::snprintf(cursor, sizeof(cursor), "%s", next);
        }
        std::snprintf(name_, sizeof(name_), "%s", backward ? (previous[0] ? previous : last) : (following[0] ? following : first));
    }
    void render() {
        const char *title = "Setups", *value = name_[0] ? name_ : "No setups";
        char number[64] = {};
        if (mode_ == Mode::action) { title = name_; value = item_ ? "Edit" : "Select"; }
        else if (mode_ == Mode::patch) {
            title = reinterpret_cast<const char *>(docs_.document().category(u16(docs_.document().patch(patch_) + 2)));
            value = reinterpret_cast<const char *>(docs_.document().patch(patch_) + 12);
        }
        else if (mode_ == Mode::field || mode_ == Mode::value) {
            static const char *fields[] = {"Program", "Bank", "Channel", "Volume", "Pan", "Brightness", "Idle seconds"};
            title = fields[field_];
            int64_t v = docs_.edit_value(patch_, field_);
            if ((field_ == 1 && v == -1) || (field_ == 3 && v == -1) || (field_ == 4 && v == -65)) std::snprintf(number, sizeof(number), "None");
            else if (v >= 0) std::snprintf(number, sizeof(number), "%lu", (unsigned long)v);
            else std::snprintf(number, sizeof(number), "%ld", (long)v);
            value = number;
        } else if (mode_ == Mode::decision) {
            title = "Unsaved edits"; value = item_ == 0 ? "Save" : item_ == 1 ? "Discard" : "Cancel";
        } else if (mode_ == Mode::saving) { title = "Saving"; value = "Please wait"; }
        else if (mode_ == Mode::failed) { title = "Save failed"; value = docs_.last_error(); }
        // Management has its own readable defaults; performance typography may
        // be too large for field labels or absent altogether on an empty card.
        Pane views[3];
        std::snprintf(views[0].category, 64, "A/B browse");
        std::snprintf(views[0].patch, 64, "C confirm / F back");
        std::snprintf(views[1].category, 64, "%.25s", title);
        std::snprintf(views[1].patch, 64, "%.25s", value);
        std::snprintf(views[2].category, 64, "%s", mode_ == Mode::value ? "A/B adjust" : "Setup management");
        std::snprintf(views[2].patch, 64, "%s", docs_.dirty() ? "Not saved" : "No MIDI sent");
        if (std::strlen(value) > 25) {
            std::snprintf(views[2].category, 64, "%.25s", value + 25);
            std::snprintf(views[2].patch, 64, "%.25s", std::strlen(value) > 50 ? value + 50 : "");
        } else if (std::strlen(title) > 25) {
            std::snprintf(views[2].category, 64, "%.25s", title + 25);
            std::snprintf(views[2].patch, 64, "%.25s", std::strlen(title) > 50 ? title + 50 : "");
        }
        for (unsigned i = 0; i < 3; ++i) hardware_.show(i, views[i]);
    }
};
}
