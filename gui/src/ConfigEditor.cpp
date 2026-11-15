#include "ConfigEditor.h"

#include "ApiClient.h"
#include "Common.h"
#include "Widgets.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QSize>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kColKey = 0;
constexpr int kColValue = 1;
constexpr int kColActions = 2;
} // namespace

ConfigEditor::ConfigEditor(ApiClient* api, QWidget* parent)
    : QWidget(parent)
    , m_api(api)
{
    auto* root = new QVBoxLayout(this);

    auto* hint = new QLabel(
        tr("All settings of the core. Changes are only sent when you press Apply. "
           "Hover over a key to see its description (when the core provides one)."),
        this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Key"), tr("Value"), tr("")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setUniformRowHeights(false);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QHeaderView* hh = m_tree->header();
    hh->setStretchLastSection(false);
    hh->setSectionResizeMode(kColKey, QHeaderView::Interactive);
    hh->setSectionResizeMode(kColValue, QHeaderView::Stretch);
    hh->setSectionResizeMode(kColActions, QHeaderView::Fixed);
    hh->resizeSection(kColKey, 300);
    hh->resizeSection(kColActions, 250);
    root->addWidget(m_tree, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto* buttons = new QHBoxLayout;
    auto* expandBtn = new QPushButton(tr("Expand all"), this);
    auto* collapseBtn = new QPushButton(tr("Collapse all"), this);
    m_revertBtn = new QPushButton(tr("Revert"), this);
    m_applyBtn = new QPushButton(tr("Apply"), this);
    buttons->addWidget(expandBtn);
    buttons->addWidget(collapseBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_revertBtn);
    buttons->addWidget(m_applyBtn);
    root->addLayout(buttons);

    connect(expandBtn, &QPushButton::clicked, this, [this]() { m_tree->expandAll(); });
    connect(collapseBtn, &QPushButton::clicked, this, [this]() { m_tree->collapseAll(); });
    connect(m_revertBtn, &QPushButton::clicked, this, [this]() {
        setStatus(QString(), false);
        reload();
    });
    connect(m_applyBtn, &QPushButton::clicked, this, [this]() { apply(); });

    // Config changed elsewhere (profiles tab, countries panel, ...).
    connect(m_api, &ApiClient::configChanged, this, [this]() {
        if (m_applying) {
            return;
        }
        if (!m_dirty) {
            if (isVisible()) {
                reload();
            }
        } else {
            setStatus(tr("The config was changed elsewhere. Apply would overwrite those changes; "
                         "press Revert to reload instead."),
                      true);
        }
    });
}

void ConfigEditor::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_dirty) {
        reload();
    }
}

// ---------------------------------------------------------------------------
// Status / dirty handling
// ---------------------------------------------------------------------------

void ConfigEditor::setStatus(const QString& text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color: #cf222e;") : QString());
}

void ConfigEditor::setDirty(bool dirty)
{
    m_dirty = dirty;
    if (dirty) {
        setStatus(tr("Unsaved changes."), false);
    } else if (m_status->text() == tr("Unsaved changes.")) {
        setStatus(QString(), false);
    }
}

void ConfigEditor::markDirty()
{
    if (!m_loading) {
        setDirty(true);
    }
}

void ConfigEditor::defer(const std::function<void()>& fn)
{
    // Structural changes are done from the event loop, never from inside the click handler of a
    // widget that is about to be destroyed. The generation check drops actions whose items are gone.
    const int gen = m_generation;
    QTimer::singleShot(0, this, [this, gen, fn]() {
        if (gen == m_generation) {
            fn();
        }
    });
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void ConfigEditor::parseSchema(const QJsonObject& obj)
{
    m_schema = Schema();

    const QJsonObject enums = obj.value(QStringLiteral("enums")).toObject();
    for (auto it = enums.constBegin(); it != enums.constEnd(); ++it) {
        m_schema.enums.insert(it.key(), Common::toStringList(it.value()));
    }
    const QJsonObject ranges = obj.value(QStringLiteral("ranges")).toObject();
    for (auto it = ranges.constBegin(); it != ranges.constEnd(); ++it) {
        const QJsonArray a = it.value().toArray();
        if (a.size() >= 2 && a.at(0).isDouble() && a.at(1).isDouble()) {
            m_schema.ranges.insert(it.key(), qMakePair(a.at(0).toDouble(), a.at(1).toDouble()));
        }
    }
    const QJsonObject docs = obj.value(QStringLiteral("docs")).toObject();
    for (auto it = docs.constBegin(); it != docs.constEnd(); ++it) {
        if (it.value().isString()) {
            m_schema.docs.insert(it.key(), it.value().toString());
        }
    }
}

void ConfigEditor::reload()
{
    if (m_reloading) {
        return;
    }
    m_reloading = true;
    m_api->get(QStringLiteral("/schema"), this, [this](const ApiClient::Response& sr) {
        if (sr.networkError) {
            m_reloading = false;
            setStatus(tr("Could not reach the core: %1").arg(sr.error), true);
            return;
        }
        if (sr.ok && sr.doc.isObject()) {
            parseSchema(sr.doc.object());
        } else {
            m_schema = Schema(); // the schema is optional; fall back to plain editors
        }
        m_api->get(QStringLiteral("/config"), this, [this](const ApiClient::Response& cr) {
            m_reloading = false;
            if (!cr.ok || !cr.doc.isObject()) {
                setStatus(tr("Could not load the config: %1").arg(cr.error), true);
                emit message(tr("Could not load the config: %1").arg(cr.error));
                return;
            }
            build(cr.doc.object());
        });
    });
}

void ConfigEditor::collectExpanded(QTreeWidgetItem* item, QSet<QString>& out) const
{
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* c = item->child(i);
        if (c->isExpanded()) {
            out.insert(c->data(0, ConcretePathRole).toString());
        }
        collectExpanded(c, out);
    }
}

void ConfigEditor::restoreExpanded(QTreeWidgetItem* item, const QSet<QString>& set)
{
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* c = item->child(i);
        if (set.contains(c->data(0, ConcretePathRole).toString())) {
            c->setExpanded(true);
        }
        restoreExpanded(c, set);
    }
}

void ConfigEditor::build(const QJsonObject& cfg)
{
    QSet<QString> expanded;
    if (!m_firstLoad) {
        collectExpanded(m_tree->invisibleRootItem(), expanded);
    }
    const int vscroll = m_tree->verticalScrollBar()->value();

    m_loading = true;
    ++m_generation;
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();

    QTreeWidgetItem* root = m_tree->invisibleRootItem();
    for (auto it = cfg.constBegin(); it != cfg.constEnd(); ++it) {
        addNode(root, it.key(), it.key(), it.key(), QStringLiteral("/") + it.key(), it.value(), false);
    }

    if (m_firstLoad) {
        for (int i = 0; i < root->childCount(); ++i) {
            root->child(i)->setExpanded(true);
        }
        m_firstLoad = false;
    } else {
        restoreExpanded(root, expanded);
    }

    m_tree->setUpdatesEnabled(true);
    m_tree->verticalScrollBar()->setValue(vscroll);
    m_loading = false;
    setDirty(false);
}

// ---------------------------------------------------------------------------
// Building the tree
// ---------------------------------------------------------------------------

void ConfigEditor::setValueWidget(QTreeWidgetItem* item, QWidget* w, const QString& schemaPath)
{
    const QString doc = m_schema.docs.value(schemaPath);
    if (!doc.isEmpty()) {
        w->setToolTip(doc);
    }
    m_tree->setItemWidget(item, kColValue, w);
    item->setSizeHint(kColValue, QSize(0, w->sizeHint().height() + 2));
}

QTreeWidgetItem* ConfigEditor::addNode(QTreeWidgetItem* parent, const QString& label, const QString& key,
                                       const QString& schemaPath, const QString& concretePath,
                                       const QJsonValue& value, bool isElement)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(kColKey, label);
    item->setData(0, KeyRole, key);
    item->setData(0, SchemaPathRole, schemaPath);
    item->setData(0, ConcretePathRole, concretePath);
    item->setData(0, DeadRole, false);
    const QString doc = m_schema.docs.value(schemaPath);
    if (!doc.isEmpty()) {
        item->setToolTip(kColKey, doc);
    }

    switch (value.type()) {
    case QJsonValue::Object: {
        item->setData(0, KindRole, static_cast<int>(KObject));
        const QJsonObject o = value.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            const QString childSchema = schemaPath.isEmpty() ? it.key() : schemaPath + QLatin1Char('.') + it.key();
            addNode(item, it.key(), it.key(), childSchema, concretePath + QLatin1Char('/') + it.key(),
                    it.value(), false);
        }
        updateSummary(item);
        break;
    }
    case QJsonValue::Array: {
        item->setData(0, KindRole, static_cast<int>(KArray));
        fillArrayChildren(item, value.toArray());
        updateSummary(item);
        break;
    }
    case QJsonValue::Bool: {
        item->setData(0, KindRole, static_cast<int>(KBool));
        auto* cb = new QCheckBox(m_tree);
        cb->setChecked(value.toBool());
        connect(cb, &QCheckBox::toggled, this, [this](bool) { markDirty(); });
        setValueWidget(item, cb, schemaPath);
        break;
    }
    case QJsonValue::Double: {
        item->setData(0, KindRole, static_cast<int>(KNumber));
        const double v = value.toDouble();
        auto* sp = new NumberSpinBox(m_tree);
        double lo = -1e12;
        double hi = 1e12;
        if (m_schema.ranges.contains(schemaPath)) {
            const QPair<double, double> r = m_schema.ranges.value(schemaPath);
            // Never clamp the stored value silently: if it is outside the schema range, widen the
            // spin box so the core can reject it with a proper error message instead.
            lo = std::min(r.first, v);
            hi = std::max(r.second, v);
        }
        sp->setRange(lo, hi);
        sp->setKeyboardTracking(false);
        sp->setValue(v);
        sp->setProperty("orig", v);
        sp->setProperty("edited", false);
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, sp](double) {
            sp->setProperty("edited", true);
            markDirty();
        });
        setValueWidget(item, sp, schemaPath);
        break;
    }
    case QJsonValue::String: {
        item->setData(0, KindRole, static_cast<int>(KString));
        const QString s = value.toString();
        const QStringList choices = m_schema.enums.value(schemaPath);
        if (!choices.isEmpty()) {
            auto* cb = new WheelGuardComboBox(m_tree);
            cb->addItems(choices);
            int idx = cb->findText(s);
            if (idx < 0) {
                cb->addItem(s); // keep a value that is not in the enum visible
                idx = cb->findText(s);
            }
            cb->setCurrentIndex(idx);
            connect(cb, &QComboBox::currentIndexChanged, this, [this](int) { markDirty(); });
            setValueWidget(item, cb, schemaPath);
        } else {
            auto* le = new QLineEdit(s, m_tree);
            connect(le, &QLineEdit::textEdited, this, [this](const QString&) { markDirty(); });
            setValueWidget(item, le, schemaPath);
        }
        break;
    }
    case QJsonValue::Null:
    case QJsonValue::Undefined:
    default: {
        // null: empty text keeps it null, any text turns it into a string.
        item->setData(0, KindRole, static_cast<int>(KNull));
        auto* le = new QLineEdit(m_tree);
        le->setPlaceholderText(tr("(null)"));
        connect(le, &QLineEdit::textEdited, this, [this](const QString&) { markDirty(); });
        setValueWidget(item, le, schemaPath);
        break;
    }
    }

    const bool isArray = (value.type() == QJsonValue::Array);
    if (isArray || isElement) {
        installButtons(item, isArray, isElement);
    }
    return item;
}

void ConfigEditor::fillArrayChildren(QTreeWidgetItem* arr, const QJsonArray& items)
{
    const QString schemaPath = arr->data(0, SchemaPathRole).toString() + QStringLiteral("[]");
    const QString concrete = arr->data(0, ConcretePathRole).toString();
    for (int i = 0; i < items.size(); ++i) {
        addNode(arr, QStringLiteral("[%1]").arg(i), QString(), schemaPath,
                concrete + QStringLiteral("/[%1]").arg(i), items.at(i), true);
    }
}

void ConfigEditor::rebuildArray(QTreeWidgetItem* arr, const QJsonArray& items)
{
    const bool wasLoading = m_loading;
    m_loading = true;
    qDeleteAll(arr->takeChildren());
    fillArrayChildren(arr, items);
    updateSummary(arr);
    arr->setExpanded(true);
    m_loading = wasLoading;
    setDirty(true);
}

void ConfigEditor::installButtons(QTreeWidgetItem* item, bool isArray, bool isElement)
{
    auto* box = new QWidget(m_tree);
    auto* lay = new QHBoxLayout(box);
    lay->setContentsMargins(2, 0, 2, 0);
    lay->setSpacing(2);

    auto makeButton = [box, lay](const QString& text, const QString& tip) {
        auto* b = new QToolButton(box);
        b->setText(text);
        b->setToolTip(tip);
        lay->addWidget(b);
        return b;
    };

    if (isArray) {
        QToolButton* add = makeButton(tr("Add"), tr("Add an item to this list"));
        connect(add, &QToolButton::clicked, this, [this, item]() {
            defer([this, item]() { addElement(item); });
        });
    }
    if (isElement) {
        QToolButton* up = makeButton(tr("Up"), tr("Move this item up"));
        QToolButton* down = makeButton(tr("Down"), tr("Move this item down"));
        QToolButton* del = makeButton(tr("Remove"), tr("Remove this item"));
        connect(up, &QToolButton::clicked, this, [this, item]() { elementAction(item, -1); });
        connect(down, &QToolButton::clicked, this, [this, item]() { elementAction(item, 1); });
        connect(del, &QToolButton::clicked, this, [this, item]() { elementAction(item, 0); });
    }
    lay->addStretch(1);

    m_tree->setItemWidget(item, kColActions, box);
    item->setSizeHint(kColActions, QSize(0, box->sizeHint().height() + 2));
}

void ConfigEditor::updateSummary(QTreeWidgetItem* item)
{
    const int kind = item->data(0, KindRole).toInt();
    QString text;
    if (kind == KArray) {
        text = tr("[%n item(s)]", nullptr, item->childCount());
    } else if (kind == KObject) {
        QStringList parts;
        for (int i = 0; i < item->childCount() && parts.size() < 3; ++i) {
            QTreeWidgetItem* c = item->child(i);
            const int ck = c->data(0, KindRole).toInt();
            if (ck == KBool || ck == KNumber || ck == KString) {
                QString v = Common::display(collect(c));
                if (v.size() > 24) {
                    v = v.left(24) + QStringLiteral("...");
                }
                parts << c->text(kColKey) + QLatin1Char('=') + v;
            }
        }
        text = parts.isEmpty() ? tr("{%n field(s)}", nullptr, item->childCount())
                               : parts.join(QStringLiteral(", "));
    }
    item->setText(kColValue, text);
    item->setForeground(kColValue, palette().brush(QPalette::Disabled, QPalette::Text));
}

// ---------------------------------------------------------------------------
// Reading the tree back into JSON
// ---------------------------------------------------------------------------

QJsonObject ConfigEditor::collectObject(QTreeWidgetItem* parent) const
{
    QJsonObject o;
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem* c = parent->child(i);
        o.insert(c->data(0, KeyRole).toString(), collect(c));
    }
    return o;
}

QJsonValue ConfigEditor::collect(QTreeWidgetItem* item) const
{
    QWidget* w = m_tree->itemWidget(item, kColValue);
    switch (item->data(0, KindRole).toInt()) {
    case KObject:
        return QJsonValue(collectObject(item));
    case KArray: {
        QJsonArray a;
        for (int i = 0; i < item->childCount(); ++i) {
            a.append(collect(item->child(i)));
        }
        return QJsonValue(a);
    }
    case KBool: {
        const auto* cb = qobject_cast<QCheckBox*>(w);
        return QJsonValue(cb ? cb->isChecked() : false);
    }
    case KNumber: {
        const auto* sp = qobject_cast<QDoubleSpinBox*>(w);
        if (!sp) {
            return QJsonValue(0.0);
        }
        if (!sp->property("edited").toBool()) {
            return QJsonValue(sp->property("orig").toDouble()); // untouched: keep the exact original
        }
        return QJsonValue(sp->value());
    }
    case KString: {
        if (const auto* cb = qobject_cast<QComboBox*>(w)) {
            return QJsonValue(cb->currentText());
        }
        if (const auto* le = qobject_cast<QLineEdit*>(w)) {
            return QJsonValue(le->text());
        }
        return QJsonValue(QString());
    }
    case KNull:
    default: {
        if (const auto* le = qobject_cast<QLineEdit*>(w)) {
            if (!le->text().isEmpty()) {
                return QJsonValue(le->text());
            }
        }
        return QJsonValue(QJsonValue::Null);
    }
    }
}

// ---------------------------------------------------------------------------
// Array editing
// ---------------------------------------------------------------------------

void ConfigEditor::addElement(QTreeWidgetItem* arr)
{
    QJsonArray items = collect(arr).toArray();
    QJsonValue nv;

    if (!items.isEmpty()) {
        nv = items.last(); // new item starts as a copy of the last one, so the structure is right
    } else {
        const QString schemaPath = arr->data(0, SchemaPathRole).toString() + QStringLiteral("[]");
        const QStringList enumChoices = m_schema.enums.value(schemaPath);
        if (!enumChoices.isEmpty()) {
            nv = enumChoices.first();
        } else {
            const QStringList types = {tr("string"), tr("integer"), tr("number"), tr("boolean"), tr("object")};
            bool ok = false;
            const QString t = QInputDialog::getItem(this, tr("Add item"),
                                                    tr("Type of the new item:"), types, 0, false, &ok);
            if (!ok) {
                return;
            }
            const int idx = static_cast<int>(types.indexOf(t));
            switch (idx) {
            case 1:
            case 2:
                nv = 0.0;
                break;
            case 3:
                nv = false;
                break;
            case 4:
                nv = QJsonObject();
                break;
            case 0:
            default:
                nv = QString();
                break;
            }
        }
    }

    items.append(nv);
    rebuildArray(arr, items);
    if (arr->childCount() > 0) {
        QTreeWidgetItem* last = arr->child(arr->childCount() - 1);
        m_tree->scrollToItem(last);
        m_tree->setCurrentItem(last);
    }
}

void ConfigEditor::elementAction(QTreeWidgetItem* elem, int action)
{
    QTreeWidgetItem* arr = elem->parent();
    if (!arr || elem->data(0, DeadRole).toBool()) {
        return;
    }
    const int idx = arr->indexOfChild(elem);
    if (idx < 0) {
        return;
    }
    if (action < 0 && idx == 0) {
        return;
    }
    if (action > 0 && idx >= arr->childCount() - 1) {
        return;
    }
    // The element (and its buttons) is destroyed by the rebuild; make a second click a no-op.
    elem->setData(0, DeadRole, true);
    defer([this, elem, action]() {
        if (action == 0) {
            removeElement(elem);
        } else {
            moveElement(elem, action);
        }
    });
}

void ConfigEditor::removeElement(QTreeWidgetItem* elem)
{
    QTreeWidgetItem* arr = elem->parent();
    if (!arr) {
        return;
    }
    const int idx = arr->indexOfChild(elem);
    QJsonArray items = collect(arr).toArray();
    if (idx < 0 || idx >= items.size()) {
        return;
    }
    items.removeAt(idx);
    rebuildArray(arr, items);
}

void ConfigEditor::moveElement(QTreeWidgetItem* elem, int delta)
{
    QTreeWidgetItem* arr = elem->parent();
    if (!arr) {
        return;
    }
    const int idx = arr->indexOfChild(elem);
    const int target = idx + delta;
    QJsonArray items = collect(arr).toArray();
    if (idx < 0 || idx >= items.size() || target < 0 || target >= items.size()) {
        return;
    }
    const QJsonValue a = items.at(idx);
    const QJsonValue b = items.at(target);
    items.replace(idx, b);
    items.replace(target, a);
    rebuildArray(arr, items);
    if (target < arr->childCount()) {
        m_tree->setCurrentItem(arr->child(target));
    }
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

void ConfigEditor::apply()
{
    if (m_applying) {
        return;
    }
    const QJsonObject cfg = collectObject(m_tree->invisibleRootItem());

    m_applying = true;
    m_applyBtn->setEnabled(false);
    setStatus(tr("Applying..."), false);

    m_api->put(QStringLiteral("/config"), cfg, this, [this](const ApiClient::Response& r) {
        m_applying = false;
        m_applyBtn->setEnabled(true);
        if (r.ok) {
            setStatus(tr("Config applied."), false);
            emit message(tr("Config applied."));
            m_dirty = false;
            reload(); // show the normalised values; keeps the "applied" text until the tree is rebuilt
        } else {
            // 400: the core explains what is wrong; keep the edits so they can be fixed.
            setStatus(tr("The core rejected the config: %1").arg(r.error), true);
            emit message(tr("Applying the config failed: %1").arg(r.error));
        }
    });
}
