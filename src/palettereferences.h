#pragma once
#include "engine.h"

namespace Flame {
struct PaletteReferences {
    QString text, help;
};

// Summarize stored instructions, not resolved levels: equal RGB values can still
// reference different palettes. The reserved white reference stays invisible.
inline PaletteReferences paletteReferences(const Engine &engine, const QMap<int, Value> &values,
                                           const Cue *step = nullptr) {
    QMap<QString, QStringList> controls;
    QSet<int> local;
    if (step) {
        const auto instructions = engine.cueValues(*step);
        for (auto it = step->values.cbegin(); it != step->values.cend(); ++it) local.insert(it.key());
        for (auto it = step->blocked.cbegin(); it != step->blocked.cend(); ++it)
            if (!instructions.contains(it.key())) local.insert(it.key());
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const auto &value = it.value();
        QString label;
        if (value.colorPalette > 0) {
            label = QString("CP %1").arg(value.colorPalette);
            const auto name = engine.colorPalette(value.colorPalette).name;
            if (!name.isEmpty()) label += " · " + name;
        } else if (value.palette > 0) label = QString("IP %1").arg(value.palette);
        if (label.isEmpty()) continue;
        if (step) label = (local.contains(it.key()) ? "Step: " : "Cue: ") + label;
        controls[label].append(engine.controlLabel(it.key()));
    }
    QStringList labels, details;
    for (auto it = controls.cbegin(); it != controls.cend(); ++it) {
        labels << it.key();
        details << it.key() + " — " + it.value().join(", ");
    }
    return {labels.isEmpty() ? QString("—") : labels.join(" / "),
            details.isEmpty() ? QString("No visible palette references stored here.") : details.join("\n")};
}
}
