#pragma once

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLocale>
#include <QString>
#include <QWheelEvent>

// Spin box that edits both integers and doubles, shows no trailing zeros and
// ignores the mouse wheel unless it has keyboard focus (so scrolling a
// tree or table does not change values by accident).
// No Q_OBJECT needed: only virtuals are overridden.
class NumberSpinBox : public QDoubleSpinBox
{
public:
    explicit NumberSpinBox(QWidget* parent = nullptr)
        : QDoubleSpinBox(parent)
    {
        setLocale(QLocale::c());
        setDecimals(9);
        setRange(-1e12, 1e12);
        setFocusPolicy(Qt::StrongFocus);
    }

    QString textFromValue(double value) const override
    {
        QString s = QString::number(value, 'f', decimals());
        if (s.contains(QLatin1Char('.'))) {
            while (s.endsWith(QLatin1Char('0'))) {
                s.chop(1);
            }
            if (s.endsWith(QLatin1Char('.'))) {
                s.chop(1);
            }
        }
        if (s == QLatin1String("-0")) {
            s = QStringLiteral("0");
        }
        return s;
    }

protected:
    void wheelEvent(QWheelEvent* e) override
    {
        if (hasFocus()) {
            QDoubleSpinBox::wheelEvent(e);
        } else {
            e->ignore();
        }
    }
};

// Combo box that ignores the mouse wheel unless focused.
class WheelGuardComboBox : public QComboBox
{
public:
    explicit WheelGuardComboBox(QWidget* parent = nullptr)
        : QComboBox(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void wheelEvent(QWheelEvent* e) override
    {
        if (hasFocus()) {
            QComboBox::wheelEvent(e);
        } else {
            e->ignore();
        }
    }
};
