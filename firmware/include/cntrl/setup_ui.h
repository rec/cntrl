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
        if (mode_ == Mode::history_work && !docs_.busy()) {
            if (docs_.last_error()[0]) mode_ = Mode::failed;
            else {
                mode_ = history_destination_; revision_ = docs_.history_revision();
                const auto &comparison = docs_.history_comparison();
                changed_patches_ = comparison.patches; changed_routes_ = comparison.routes;
                changed_settings_ = comparison.settings; changed_formatting_ = comparison.formatting;
            }
            render();
        }
        if (mode_ == Mode::saving && !docs_.busy()) {
            if (docs_.last_error()[0]) { mode_ = Mode::failed; render(); }
            else { mode_ = Mode::closed; docs_.redraw(); }
        }
    }
    void button(unsigned button, bool down, uint32_t now) {
        if (!down || locked_ || (mode_ == Mode::saving || mode_ == Mode::history_work)) return;
        if (mode_ == Mode::failed) { if (button == 2 || button == 5) close(); return; }
        if (mode_ == Mode::catalogue) {
            if (button < 2) browse(button == 0);
            else if (button == 2 && name_[0]) { mode_ = Mode::action; item_ = 0; }
            else if (button == 5) { close(); return; }
        } else if (mode_ == Mode::action) {
            if (button < 2) item_ = (item_ + 3 + (button == 0 ? -1 : 1)) % 3;
            else if (button == 5) mode_ = Mode::catalogue;
            else if (button == 2) {
                if (item_ == 2) {
                    revision_ = Revision(); mode_ = Mode::history; render(); return;
                }
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
        } else if (mode_ == Mode::history) {
            if (button == 2 && !revision_.number) {
                if (!docs_.history_save(name_, "")) { hardware_.error("Snapshot failed"); return; }
                history_destination_ = Mode::history; mode_ = Mode::history_work; render(); return;
            }
            if (button == 5) mode_ = Mode::action;
            else if (button < 2) {
                if (button == 0) revision_ = Revision();
                else if (docs_.history_next(name_, revision_.number ? revision_.number : UINT64_MAX)) {
                    history_destination_ = Mode::history; mode_ = Mode::history_work;
                }
            } else if (button == 2 && revision_.number) {
                if (!docs_.history_compare(name_, revision_.number)) {
                    hardware_.error("Cannot compare revision"); return;
                }
                history_destination_ = Mode::comparison; mode_ = Mode::history_work; item_ = 0;
            }
        } else if (mode_ == Mode::comparison) {
            if (button == 5) mode_ = Mode::history;
            else if (button < 2) item_ = (item_ + 5 + (button == 0 ? -1 : 1)) % 5;
            else if (button == 2 && item_ == 4) {
                if (docs_.history_restore(name_, revision_.number, now)) mode_ = Mode::saving;
                else { hardware_.error("Restore failed"); return; }
            }
        } else if (mode_ == Mode::patch) {
            if (button < 2) patch_ = (patch_ + docs_.document().patches() + (button == 0 ? -1 : 1)) % docs_.document().patches();
            else if (button == 2) { mode_ = Mode::field; field_ = 0; }
            else if (button == 5) { mode_ = Mode::decision; item_ = 2; }
        } else if (mode_ == Mode::field) {
            if (button < 2) field_ = (field_ + 8 + (button == 0 ? -1 : 1)) % 8;
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
    enum class Mode { closed, catalogue, action, patch, field, value, decision, saving, failed, history, comparison, history_work };
    Documents &docs_; Hardware &hardware_;
    Mode mode_ = Mode::closed, history_destination_ = Mode::history;
    Revision revision_;
    unsigned changed_patches_ = 0, changed_routes_ = 0;
    bool changed_settings_ = false, changed_formatting_ = false;
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
        const char *value_label = "Action";
        char number[64] = {};
        if (mode_ == Mode::action) { title = name_; value = item_ == 2 ? "History" : item_ ? "Edit" : "Select"; }
        else if (mode_ == Mode::patch) {
            title = reinterpret_cast<const char *>(docs_.document().category(u16(docs_.document().patch(patch_) + 2)));
            value = reinterpret_cast<const char *>(docs_.document().patch(patch_) + 12);
            value_label = "Patch";
        }
        else if (mode_ == Mode::field || mode_ == Mode::value) {
            static const char *fields[] = {"Program", "Bank", "Channel", "Volume", "Pan", "Brightness", "Idle seconds", "USB MIDI"};
            title = fields[field_];
            int64_t v = docs_.edit_value(patch_, field_);
            if ((field_ == 1 && v == -1) || (field_ == 3 && v == -1) || (field_ == 4 && v == -65)) std::snprintf(number, sizeof(number), "None");
            else if (field_ == 7) std::snprintf(number, sizeof(number), "%s", v ? "On" : "Off");
            else if (v >= 0) std::snprintf(number, sizeof(number), "%lu", (unsigned long)v);
            else std::snprintf(number, sizeof(number), "%ld", (long)v);
            value = number;
            value_label = "Value";
        } else if (mode_ == Mode::decision) {
            title = "Unsaved edits"; value = item_ == 0 ? "Save" : item_ == 1 ? "Discard" : "Cancel";
        } else if (mode_ == Mode::history_work) { title = "History"; value = "Working..."; }
        else if (mode_ == Mode::saving) { title = "Saving"; value = "Please wait"; }
        else if (mode_ == Mode::failed) { title = "Save failed"; value = docs_.last_error(); }
        if (mode_ == Mode::history) {
            title = "History"; value_label = "Revision";
            if (revision_.number) std::snprintf(number, sizeof(number), "%llu @%lu %.24s", (unsigned long long)revision_.number, (unsigned long)revision_.modified, revision_.label);
            else std::snprintf(number, sizeof(number), "Current: C snapshot");
            value = number;
        } else if (mode_ == Mode::comparison) {
            title = "Compare to current"; value_label = "Change";
            if (item_ == 0) std::snprintf(number, sizeof(number), "%u changed patches", changed_patches_);
            else if (item_ == 1) std::snprintf(number, sizeof(number), "%u changed routes", changed_routes_);
            else if (item_ == 2) std::snprintf(number, sizeof(number), "Settings: %s", changed_settings_ ? "changed" : "same");
            else if (item_ == 3) std::snprintf(number, sizeof(number), "Authored data: %s", changed_formatting_ ? "changed" : "same");
            else std::snprintf(number, sizeof(number), "C Restore, F Back");
            value = number;
        }
        // Management has its own readable defaults; performance typography may
        // be too large for field labels or absent altogether on an empty card.
        Pane views[3];
        for (auto &view : views) {
            for (unsigned i = 0; i < 2; ++i)
                view.lines[i].graphics.size = view.lines[i].graphics.line_height = 16;
            view.lines[1].wrap = true;
        }
        std::snprintf(views[0].lines[0].text, 64, "A/B browse");
        std::snprintf(views[0].lines[1].text, 64, "C OK, F back");
        if (mode_ == Mode::value) std::snprintf(views[0].lines[0].text, 64, "A/B adjust");
        if (mode_ == Mode::catalogue) {
            std::snprintf(views[1].lines[0].text, 64, "Setups");
            std::snprintf(views[1].lines[1].text, 64, "%s", value);
            std::snprintf(views[2].lines[0].text, 64, "Setup menu");
            std::snprintf(views[2].lines[1].text, 64, "No MIDI sent");
        } else {
            std::snprintf(views[1].lines[0].text, 64, "%s", mode_ == Mode::action ? "Setup" : mode_ == Mode::patch ? "Category" : "Field");
            std::snprintf(views[1].lines[1].text, 64, "%s", title);
            std::snprintf(views[2].lines[0].text, 64, "%s", value_label);
            std::snprintf(views[2].lines[1].text, 64, "%s", value);
        }
        for (unsigned i = 0; i < 3; ++i) hardware_.show(i, views[i]);
    }
};
}
