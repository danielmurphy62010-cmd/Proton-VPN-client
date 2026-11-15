#include "ProfilesTab.h"

#include "ApiClient.h"
#include "Common.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVariant>

namespace {
enum Col {
    ColId = 0,
    ColProtocol,
    ColCountry,
    ColCity,
    ColPing,
    ColSucc,
    ColFail,
    ColEnabled,
    ColConnected,
    ColCount
};
} // namespace

// ===========================================================================
// CountriesPanel
// ===========================================================================

CountriesPanel::CountriesPanel(ApiClient* api, QWidget* parent)
    : QGroupBox(tr("Countries"), parent)
    , m_api(api)
{
    auto* lay = new QVBoxLayout(this);

    auto* row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(tr("Edit:"), this));
    m_target = new QComboBox(this);
    m_target->addItem(tr("Normal config"), QStringLiteral("normal"));
    m_target->addItem(tr("Benchmark config"), QStringLiteral("benchmark"));
    row1->addWidget(m_target, 1);
    lay->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    row2->addWidget(new QLabel(tr("Mode:"), this));
    m_mode = new QComboBox(this);
    m_mode->addItems({QStringLiteral("all"), QStringLiteral("allow"), QStringLiteral("deny")});
    m_mode->setToolTip(tr("all: ignore the list; allow: only listed countries; deny: all except listed"));
    row2->addWidget(m_mode, 1);
    lay->addLayout(row2);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter countries"));
    m_filter->setClearButtonEnabled(true);
    lay->addWidget(m_filter);

    m_list = new QListWidget(this);
    lay->addWidget(m_list, 1);

    auto* row3 = new QHBoxLayout;
    auto* allBtn = new QPushButton(tr("Check shown"), this);
    auto* noneBtn = new QPushButton(tr("Uncheck shown"), this);
    row3->addWidget(allBtn);
    row3->addWidget(noneBtn);
    lay->addLayout(row3);

    auto* row4 = new QHBoxLayout;
    auto* reloadBtn = new QPushButton(tr("Reload"), this);
    m_applyBtn = new QPushButton(tr("Apply countries"), this);
    row4->addWidget(reloadBtn);
    row4->addWidget(m_applyBtn);
    lay->addLayout(row4);

    connect(m_target, &QComboBox::currentIndexChanged, this, [this](int) { populate(); });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_filling) {
            setDirty(true);
        }
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        if (!m_filling) {
            setDirty(true);
        }
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    connect(allBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_list->count(); ++i) {
            if (!m_list->item(i)->isHidden()) {
                m_list->item(i)->setCheckState(Qt::Checked);
            }
        }
    });
    connect(noneBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_list->count(); ++i) {
            if (!m_list->item(i)->isHidden()) {
                m_list->item(i)->setCheckState(Qt::Unchecked);
            }
        }
    });
    connect(reloadBtn, &QPushButton::clicked, this, [this]() { reload(); });
    connect(m_applyBtn, &QPushButton::clicked, this, [this]() { apply(); });
}

void CountriesPanel::showEvent(QShowEvent* event)
{
    QGroupBox::showEvent(event);
    if (!m_dirty) {
        reload();
    }
}

QString CountriesPanel::basePath() const
{
    return m_target->currentData().toString() == QLatin1String("benchmark")
        ? QStringLiteral("benchmark.base")
        : QStringLiteral("normal");
}

void CountriesPanel::setDirty(bool dirty)
{
    m_dirty = dirty;
    setTitle(dirty ? tr("Countries (unsaved changes)") : tr("Countries"));
}

void CountriesPanel::reload()
{
    m_api->get(QStringLiteral("/countries"), this, [this](const ApiClient::Response& r) {
        if (r.ok && r.doc.isArray()) {
            m_countries = Common::toStringList(r.doc.array());
        } else if (!r.networkError) {
            emit message(tr("Could not load countries: %1").arg(r.error));
        }
        m_api->get(QStringLiteral("/config"), this, [this](const ApiClient::Response& cr) {
            if (cr.ok && cr.doc.isObject()) {
                m_config = cr.doc.object();
                populate();
            } else if (!cr.networkError) {
                emit message(tr("Could not load config: %1").arg(cr.error));
            }
        });
    });
}

void CountriesPanel::populate()
{
    m_filling = true;
    const QString base = basePath();
    const QStringList chosen = Common::toStringList(Common::getPath(m_config, base + QStringLiteral(".countries.list")));
    const QSet<QString> chosenSet(chosen.cbegin(), chosen.cend());

    QStringList all = m_countries;
    for (const QString& c : chosen) {
        all << c; // keep configured codes that are not in the server list so they can be unchecked
    }
    all.removeDuplicates();
    all.sort();

    m_list->clear();
    for (const QString& c : all) {
        auto* it = new QListWidgetItem(c, m_list);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(chosenSet.contains(c) ? Qt::Checked : Qt::Unchecked);
    }

    const QString mode = Common::getPath(m_config, base + QStringLiteral(".countries.mode"))
                             .toString(QStringLiteral("all"));
    int idx = m_mode->findText(mode);
    if (idx < 0) {
        m_mode->addItem(mode);
        idx = m_mode->findText(mode);
    }
    m_mode->setCurrentIndex(idx);

    m_filling = false;
    setDirty(false);
    applyFilter();
}

void CountriesPanel::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        it->setHidden(!needle.isEmpty() && !it->text().contains(needle, Qt::CaseInsensitive));
    }
}

void CountriesPanel::apply()
{
    QStringList checked;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->checkState() == Qt::Checked) {
            checked << m_list->item(i)->text();
        }
    }
    const QString mode = m_mode->currentText();
    const QString base = basePath();

    m_applyBtn->setEnabled(false);
    m_api->modifyConfig(
        this,
        [base, checked, mode](QJsonObject& cfg) {
            Common::setPath(cfg, base + QStringLiteral(".countries.list"), Common::fromStringList(checked));
            Common::setPath(cfg, base + QStringLiteral(".countries.mode"), mode);
            return true;
        },
        [this](bool ok, const QString& err) {
            m_applyBtn->setEnabled(true);
            if (ok) {
                setDirty(false);
                emit message(tr("Country settings saved."));
                reload();
            } else {
                emit message(tr("Saving country settings failed: %1").arg(err));
            }
        });
}

// ===========================================================================
// ProfilesTab
// ===========================================================================

ProfilesTab::ProfilesTab(ApiClient* api, QWidget* parent)
    : QWidget(parent)
    , m_api(api)
{
    auto* root = new QVBoxLayout(this);

    // --- top row: filter and include/exclude target ---
    auto* top = new QHBoxLayout;
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter by id, protocol, country or city"));
    m_filter->setClearButtonEnabled(true);
    top->addWidget(m_filter, 1);

    top->addWidget(new QLabel(tr("Enabled checkbox edits:"), this));
    m_cfgTarget = new QComboBox(this);
    m_cfgTarget->addItem(tr("normal.profiles"), QStringLiteral("normal"));
    m_cfgTarget->addItem(tr("benchmark.base.profiles"), QStringLiteral("benchmark"));
    top->addWidget(m_cfgTarget);
    m_listKind = new QComboBox(this);
    m_listKind->addItem(tr("include list"), QStringLiteral("include"));
    m_listKind->addItem(tr("exclude list"), QStringLiteral("exclude"));
    m_listKind->setToolTip(
        tr("include: checking adds the id to the include list, unchecking removes it.\n"
           "exclude: checking removes the id from the exclude list, unchecking adds it."));
    top->addWidget(m_listKind);
    root->addLayout(top);

    // --- table + countries panel ---
    auto* split = new QSplitter(Qt::Horizontal, this);

    m_table = new QTableWidget(0, ColCount, split);
    m_table->setHorizontalHeaderLabels({tr("ID"), tr("Protocol"), tr("Country"), tr("City"),
                                        tr("Ping avg (ms)"), tr("Successes"), tr("Failures"),
                                        tr("Enabled"), tr("Connected")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(ColId, 230);
    m_table->setColumnWidth(ColProtocol, 80);
    m_table->setColumnWidth(ColCountry, 70);
    m_table->setColumnWidth(ColCity, 110);
    m_table->setColumnWidth(ColPing, 100);
    m_table->setColumnWidth(ColSucc, 80);
    m_table->setColumnWidth(ColFail, 70);
    m_table->setColumnWidth(ColEnabled, 70);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(ColId, Qt::AscendingOrder);

    m_countries = new CountriesPanel(api, split);

    split->addWidget(m_table);
    split->addWidget(m_countries);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    // --- button row ---
    auto* buttons = new QHBoxLayout;
    auto* connectBtn = new QPushButton(tr("Connect selected"), this);
    auto* disconnectBtn = new QPushButton(tr("Disconnect selected"), this);
    auto* serversBtn = new QPushButton(tr("Refresh server list"), this);
    auto* reloadBtn = new QPushButton(tr("Reload table"), this);
    buttons->addWidget(connectBtn);
    buttons->addWidget(disconnectBtn);
    buttons->addStretch(1);
    buttons->addWidget(serversBtn);
    buttons->addWidget(reloadBtn);
    root->addLayout(buttons);

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    connect(m_cfgTarget, &QComboBox::currentIndexChanged, this, [this](int) { rebuildTable(); });
    connect(m_listKind, &QComboBox::currentIndexChanged, this, [this](int) { rebuildTable(); });
    connect(m_table, &QTableWidget::itemChanged, this, &ProfilesTab::onItemChanged);
    connect(connectBtn, &QPushButton::clicked, this, [this]() { actOnSelected(QStringLiteral("/connect")); });
    connect(disconnectBtn, &QPushButton::clicked, this, [this]() { actOnSelected(QStringLiteral("/disconnect")); });
    connect(reloadBtn, &QPushButton::clicked, this, [this]() { refresh(); });
    connect(serversBtn, &QPushButton::clicked, this, [this]() {
        emit message(tr("Requesting server list refresh..."));
        m_api->post(QStringLiteral("/servers/refresh"), QJsonObject(), this,
                    [this](const ApiClient::Response& r) {
                        if (r.ok) {
                            emit message(tr("Server list refreshed."));
                            refresh();
                            m_countries->reload();
                        } else {
                            emit message(tr("Server list refresh failed: %1").arg(r.error));
                        }
                    },
                    120000);
    });

    // Periodic refresh, only while visible.
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            refresh();
        }
    });
    m_timer.start();
}

void ProfilesTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
}

bool ProfilesTab::targetIsBenchmark() const
{
    return m_cfgTarget->currentData().toString() == QLatin1String("benchmark");
}

QString ProfilesTab::listKind() const
{
    return m_listKind->currentData().toString();
}

QString ProfilesTab::listPath() const
{
    const QString base = targetIsBenchmark() ? QStringLiteral("benchmark.base.profiles")
                                             : QStringLiteral("normal.profiles");
    return base + QLatin1Char('.') + listKind();
}

QStringList ProfilesTab::selectedIds() const
{
    QStringList ids;
    const QModelIndexList rows = m_table->selectionModel()->selectedRows(ColId);
    for (const QModelIndex& ix : rows) {
        if (m_table->isRowHidden(ix.row())) {
            continue;
        }
        const QTableWidgetItem* it = m_table->item(ix.row(), ColId);
        if (it) {
            ids << it->text();
        }
    }
    return ids;
}

void ProfilesTab::refresh()
{
    if (m_refreshing) {
        return;
    }
    m_refreshing = true;
    m_api->get(QStringLiteral("/profiles"), this, [this](const ApiClient::Response& r) {
        if (!r.ok || !r.doc.isArray()) {
            m_refreshing = false;
            if (!r.networkError) {
                emit message(tr("Could not load profiles: %1").arg(r.error));
            }
            return;
        }
        m_profiles = r.doc.array();
        m_api->get(QStringLiteral("/config"), this, [this](const ApiClient::Response& cr) {
            m_refreshing = false;
            if (cr.ok && cr.doc.isObject()) {
                m_config = cr.doc.object();
            }
            rebuildTable();
        });
    });
}

void ProfilesTab::rebuildTable()
{
    const bool bench = targetIsBenchmark();
    const QString kind = listKind();

    QStringList benchList;
    if (bench) {
        benchList = Common::toStringList(
            Common::getPath(m_config, QStringLiteral("benchmark.base.profiles.") + kind));
    }
    const QSet<QString> benchSet(benchList.cbegin(), benchList.cend());

    // Skip the (potentially large) rebuild when nothing relevant changed.
    QStringList sortedBench = benchList;
    sortedBench.sort();
    QByteArray sig = QJsonDocument(m_profiles).toJson(QJsonDocument::Compact);
    sig += '|';
    sig += bench ? "B" : "N";
    sig += kind.toUtf8();
    sig += '|';
    sig += sortedBench.join(QLatin1Char(',')).toUtf8();
    if (sig == m_lastSignature) {
        return;
    }
    m_lastSignature = sig;

    const QStringList prevSelected = selectedIds();
    const QSet<QString> prev(prevSelected.cbegin(), prevSelected.cend());
    const int vscroll = m_table->verticalScrollBar()->value();

    m_filling = true;
    m_table->setUpdatesEnabled(false);
    m_table->setSortingEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(m_profiles.size());

    auto textItem = [](const QString& t) {
        auto* it = new QTableWidgetItem(t);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        return it;
    };
    auto numItem = [](const QVariant& v) {
        auto* it = new QTableWidgetItem;
        it->setData(Qt::DisplayRole, v);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        return it;
    };

    int row = 0;
    for (const QJsonValue& pv : m_profiles) {
        const QJsonObject p = pv.toObject();
        const QJsonObject st = p.value(QStringLiteral("stats")).toObject();
        const QString id = p.value(QStringLiteral("id")).toString();

        QTableWidgetItem* idItem = textItem(id);
        QStringList tip;
        tip << tr("Server: %1").arg(p.value(QStringLiteral("server")).toString());
        tip << tr("Entry: %1:%2")
                   .arg(p.value(QStringLiteral("entry_ip")).toString(),
                        Common::display(p.value(QStringLiteral("port"))));
        tip << tr("Transport: %1").arg(p.value(QStringLiteral("transport")).toString());
        tip << tr("Tier: %1").arg(Common::display(p.value(QStringLiteral("tier"))));
        tip << tr("Features: %1").arg(Common::toStringList(p.value(QStringLiteral("features"))).join(QStringLiteral(", ")));
        idItem->setToolTip(tip.join(QLatin1Char('\n')));

        m_table->setItem(row, ColId, idItem);
        m_table->setItem(row, ColProtocol, textItem(p.value(QStringLiteral("protocol")).toString()));
        m_table->setItem(row, ColCountry, textItem(p.value(QStringLiteral("country")).toString()));
        m_table->setItem(row, ColCity, textItem(p.value(QStringLiteral("city")).toString()));

        const double pingCount = st.value(QStringLiteral("ping_count")).toDouble();
        if (pingCount > 0) {
            const double avg = st.value(QStringLiteral("avg_ping_ms")).toDouble();
            m_table->setItem(row, ColPing, numItem(QVariant(qRound64(avg * 10.0) / 10.0)));
        } else {
            m_table->setItem(row, ColPing, numItem(QVariant()));
        }
        m_table->setItem(row, ColSucc, numItem(QVariant(st.value(QStringLiteral("successes")).toDouble())));
        m_table->setItem(row, ColFail, numItem(QVariant(st.value(QStringLiteral("failures")).toDouble())));

        bool enabled = p.value(QStringLiteral("enabled")).toBool();
        if (bench) {
            // The API reports the effective state for the normal config only, so for the
            // benchmark config the state is derived from the chosen list.
            enabled = (kind == QLatin1String("include")) ? benchSet.contains(id) : !benchSet.contains(id);
        }
        auto* en = new QTableWidgetItem;
        en->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        en->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(row, ColEnabled, en);

        m_table->setItem(row, ColConnected,
                         textItem(p.value(QStringLiteral("connected")).toBool() ? tr("yes") : QString()));
        ++row;
    }

    m_table->setSortingEnabled(true); // re-applies the current sort indicator

    // Restore selection by id.
    if (!prev.isEmpty()) {
        QItemSelection sel;
        for (int r = 0; r < m_table->rowCount(); ++r) {
            const QTableWidgetItem* it = m_table->item(r, ColId);
            if (it && prev.contains(it->text())) {
                sel.select(m_table->model()->index(r, 0), m_table->model()->index(r, ColCount - 1));
            }
        }
        m_table->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    }

    m_table->setUpdatesEnabled(true);
    m_filling = false;
    applyFilter();
    m_table->verticalScrollBar()->setValue(vscroll);
}

void ProfilesTab::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        bool show = needle.isEmpty();
        if (!show) {
            for (int c = ColId; c <= ColCity; ++c) {
                const QTableWidgetItem* it = m_table->item(r, c);
                if (it && it->text().contains(needle, Qt::CaseInsensitive)) {
                    show = true;
                    break;
                }
            }
        }
        m_table->setRowHidden(r, !show);
    }
}

void ProfilesTab::onItemChanged(QTableWidgetItem* item)
{
    if (m_filling || !item || item->column() != ColEnabled) {
        return;
    }
    const QTableWidgetItem* idItem = m_table->item(item->row(), ColId);
    if (!idItem) {
        return;
    }
    const QString id = idItem->text();
    const bool checked = item->checkState() == Qt::Checked;
    const QString path = listPath();
    // include list: enabled == present. exclude list: enabled == absent.
    const bool wantInList = (listKind() == QLatin1String("include")) ? checked : !checked;

    m_api->modifyConfig(
        this,
        [path, id, wantInList](QJsonObject& cfg) {
            Common::setIdInList(cfg, path, id, wantInList);
            return true;
        },
        [this, id](bool ok, const QString& err) {
            if (!ok) {
                emit message(tr("Updating profile list for %1 failed: %2").arg(id, err));
            }
            // Always reload: shows the effective state (and reverts the box on failure).
            m_lastSignature.clear();
            refresh();
        });
}

void ProfilesTab::actOnSelected(const QString& apiPath)
{
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) {
        emit message(tr("No profile selected."));
        return;
    }
    for (const QString& id : ids) {
        QJsonObject body;
        body.insert(QStringLiteral("id"), id);
        m_api->post(apiPath, body, this, [this, id, apiPath](const ApiClient::Response& r) {
            if (!r.ok) {
                emit message(tr("%1 failed for %2: %3").arg(apiPath, id, r.error));
            }
            QTimer::singleShot(500, this, [this]() {
                m_lastSignature.clear();
                refresh();
            });
        });
    }
}
