#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class ApiClient;
class QLabel;
class QPushButton;
class QShowEvent;
class QTreeWidget;
class QTreeWidgetItem;

// Generic editor for the ENTIRE configuration (GET /config, PUT /config).
//
// The config is shown as a tree: objects are child nodes, bool -> check box, numbers -> spin box
// (range from GET /schema when the dotted path matches), strings -> line edit or combo box (enum
// from GET /schema), arrays -> child nodes with Add / Remove / Up / Down buttons. Schema paths
// are dotted names with "[]" for array elements, for example "normal.priority.criteria[].metric".
class ConfigEditor : public QWidget
{
    Q_OBJECT
public:
    explicit ConfigEditor(ApiClient* api, QWidget* parent = nullptr);

public slots:
    void reload();

signals:
    void message(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;

private:
    enum Kind { KObject = 0, KArray, KBool, KNumber, KString, KNull };
    enum Role {
        KindRole = Qt::UserRole + 1,
        KeyRole,
        SchemaPathRole,
        ConcretePathRole,
        DeadRole
    };

    struct Schema {
        QHash<QString, QStringList> enums;
        QHash<QString, QPair<double, double>> ranges;
        QHash<QString, QString> docs;
    };

    void parseSchema(const QJsonObject& obj);
    void build(const QJsonObject& cfg);

    QTreeWidgetItem* addNode(QTreeWidgetItem* parent, const QString& label, const QString& key,
                             const QString& schemaPath, const QString& concretePath,
                             const QJsonValue& value, bool isElement);
    void fillArrayChildren(QTreeWidgetItem* arr, const QJsonArray& items);
    void rebuildArray(QTreeWidgetItem* arr, const QJsonArray& items);
    void installButtons(QTreeWidgetItem* item, bool isArray, bool isElement);
    void setValueWidget(QTreeWidgetItem* item, QWidget* w, const QString& schemaPath);
    void updateSummary(QTreeWidgetItem* item);

    QJsonValue collect(QTreeWidgetItem* item) const;
    QJsonObject collectObject(QTreeWidgetItem* parent) const;

    void addElement(QTreeWidgetItem* arr);
    void elementAction(QTreeWidgetItem* elem, int action); // 0 = remove, -1 = up, +1 = down
    void removeElement(QTreeWidgetItem* elem);
    void moveElement(QTreeWidgetItem* elem, int delta);

    void defer(const std::function<void()>& fn);
    void collectExpanded(QTreeWidgetItem* item, QSet<QString>& out) const;
    void restoreExpanded(QTreeWidgetItem* item, const QSet<QString>& set);

    void markDirty();
    void setDirty(bool dirty);
    void setStatus(const QString& text, bool error);
    void apply();

    ApiClient* m_api;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_applyBtn = nullptr;
    QPushButton* m_revertBtn = nullptr;

    Schema m_schema;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_applying = false;
    bool m_reloading = false;
    bool m_firstLoad = true;
    int m_generation = 0; // bumped whenever the tree is rebuilt; invalidates deferred actions
};
