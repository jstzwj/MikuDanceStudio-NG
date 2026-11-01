#pragma once
#include <QComboBox>
#include <QSignalBlocker>
namespace mikudancestudio::qt {
// Refresh values without resetting unchanged models, popup/scroll state or
// emitting transient current-index changes on each animation frame.
template <class F> void SyncComboItems(QComboBox* combo, F fill) {
    const QSignalBlocker blocked(combo);
    int next = 0;
    fill([&](const QString& text, const QVariant& data = {}) {
        if (next == combo->count())
            combo->addItem(text, data);
        else {
            if (combo->itemText(next) != text)
                combo->setItemText(next, text);
            if (combo->itemData(next) != data)
                combo->setItemData(next, data);
        }
        ++next;
    });
    while (combo->count() > next)
        combo->removeItem(combo->count() - 1);
}
}
