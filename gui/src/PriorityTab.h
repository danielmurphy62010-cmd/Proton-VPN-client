#pragma once

#include <QGroupBox>
#include <QJsonArray>
#include <QString>
#include <QWidget>

class ApiClient;
class QComboBox;
class QPushButton;
class QShowEvent;
class QTableWidget;

// Editor for one priority object (<base> = "normal.priority" or "benchmark.base.priority"):
// strategy combo plus an editable, re-orderable table of criteria.
class PriorityEditor : public QGroupBox
{
    Q_OBJECT
public:
    PriorityEditor(ApiClient* api, const QString& basePath, const QString& title, QWidget* parent = nullptr);

public slots:
    void reload();

signals:
    void message(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void fillRows(const QJsonArray& criteria);
    QJsonArray collectRows() const;
    void markDirty();
    void setDirty(bool dirty);
    void addRow();
    void removeRow();
    void moveRow(int delta);
    void apply();

    ApiClient* m_api;
    QString m_base;
    QString m_title;
    QComboBox* m_strategy = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_applyBtn = nullptr;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_applying = false;
};

// "Priority" tab: normal and benchmark priority editors side by side.
class PriorityTab : public QWidget
{
    Q_OBJECT
public:
    explicit PriorityTab(ApiClient* api, QWidget* parent = nullptr);

signals:
    void message(const QString& text);
};
