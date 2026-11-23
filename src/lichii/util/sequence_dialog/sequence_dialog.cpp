// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include "lichii/configuration/settings_dialog.h"
#include "lichii/spotlight/settings_page.h"
#include "lichii/util/sequence_dialog/sequence_dialog.h"

namespace {

class KeyWell final : public QWidget {
public:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(0xff, 0xff, 0xff, 0x08));
        painter.setPen(QPen(QColor(0xff, 0x6b, 0x95, 0x80), 2, Qt::DashLine));
        const QRectF bounds = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.drawRoundedRect(bounds, Spotlight::Radius::Panel + 2, Spotlight::Radius::Panel + 2);
    }
};

} // namespace

SequenceDialog::SequenceDialog(QWidget* parent, const QString& action)
    : Spotlight::Dialog(parent, Chrome::Content) {
    using namespace SettingsCard;
    auto* layout = Layout(*this, 440);
    Header(*this, *layout, tr("Enter a hotkey"), QStringLiteral("keyboard"),
           tr("%1 · Press the new key combination").arg(action), true,
           Spotlight::SettingsColor::Gold());

    auto* well = new KeyWell;
    well->setFixedHeight(92);
    auto* stack = new QGridLayout(well);
    stack->setContentsMargins(4, 4, 4, 4);
    key_sequence = new QKeySequenceEdit;
    key_sequence->setMaximumSequenceLength(1);
    key_sequence->setAccessibleName(tr("Press the new key combination"));
    key_sequence->setStyleSheet(
        QStringLiteral("QKeySequenceEdit, QLineEdit { background: transparent; border: none; "
                       "color: transparent; selection-background-color: transparent; "
                       "selection-color: transparent; }"));
    key_sequence->findChild<QLineEdit*>()->setPlaceholderText({});
    stack->addWidget(key_sequence, 0, 0);
    auto* preview = new QWidget;
    preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* caps = new QHBoxLayout(preview);
    caps->setContentsMargins(0, 0, 0, 0);
    caps->setSpacing(4);
    stack->addWidget(preview, 0, 0, Qt::AlignCenter);
    layout->addWidget(well);

    auto* footer = new QHBoxLayout;
    footer->setSpacing(10);
    footer->setContentsMargins(0, 6, 0, 0);
    footer->addStretch();
    auto* cancel = new Button(tr("Cancel"), ButtonKind::Control);
    auto* okay = new Button(tr("OK"), ButtonKind::Accent);
    footer->addWidget(cancel);
    footer->addWidget(okay);
    layout->addLayout(footer);

    const auto render_keys = [caps, okay](const QKeySequence& sequence) {
        while (auto* item = caps->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const QString formatted = sequence.toString(QKeySequence::NativeText);
        if (formatted.isEmpty()) {
            caps->addWidget(
                Text(tr("Press the new key combination"), 14, 400, Color::TextSecondary()));
        } else {
            auto remaining = QStringView(formatted);
            for (qsizetype separator = remaining.indexOf(u'+');
                 separator > 0 && separator < remaining.size() - 1;
                 separator = remaining.indexOf(u'+')) {
                caps->addWidget(new KeyCap(remaining.first(separator).toString()));
                remaining = remaining.sliced(separator + 1);
            }
            caps->addWidget(new KeyCap(remaining.toString()));
        }
        okay->setEnabled(!sequence.isEmpty());
    };
    render_keys({});
    connect(key_sequence, &QKeySequenceEdit::keySequenceChanged, this, render_keys);
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(okay, &QAbstractButton::clicked, this, &QDialog::accept);
    key_sequence->setFocus();
    adjustSize();
}

QKeySequence SequenceDialog::GetSequence() const {
    return key_sequence->keySequence();
}

void SequenceDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (auto* focused = qobject_cast<QAbstractButton*>(focusWidget())) {
            focused->click();
            return;
        }
    }
    Dialog::keyPressEvent(event);
}
