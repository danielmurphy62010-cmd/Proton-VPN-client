#include "StatusTab.h"

#include "Common.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum ConnCol { CId = 0, CProtocol, CCountry, CServer, CState, CPing, CAction, CCount };

QString pingText(const QJsonValue& v)
{
    if (!v.isDouble()) {
        return QStringLiteral("-");
    }
    return QString::number(v.toDouble(), 'f', 1);
}
} // namespace

StatusTab::StatusTab(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);

    // --- control group ---
    auto* ctl = new QGroupBox(tr("Control"), this);
    auto* grid = new QGridLayout(ctl);

    grid->addWidget(new QLabel(tr("Mode:"), ctl), 0, 0);
    m_mode = new QComboBox(ctl);
    for (const Common::ModeInfo& m : Common::modes()) {
        m_mode->addItem(m.label, m.id);
    }
    grid->addWidget(m_mode, 0, 1);

    m_bench = new QCheckBox(tr("Benchmark mode"), ctl);
    grid->addWidget(m_bench, 0, 2);

    grid->addWidget(new QLabel(tr("Proton login:"), ctl), 1, 0);
    m_login = new QLabel(tr("unknown"), ctl);
    grid->addWidget(m_login, 1, 1);
    auto* loginRow = new QHBoxLayout;
    m_loginBtn = new QPushButton(tr("Log in to Proton"), ctl);
    m_logoutBtn = new QPushButton(tr("Log out"), ctl);
    m_logoutBtn->setEnabled(false);
    loginRow->addWidget(m_loginBtn);
    loginRow->addWidget(m_logoutBtn);
    loginRow->addStretch(1);
    grid->addLayout(loginRow, 1, 2);

    grid->addWidget(new QLabel(tr("Tor:"), ctl), 2, 0);
    m_tor = new QLabel(tr("unknown"), ctl);
    grid->addWidget(m_tor, 2, 1, 1, 2);

    grid->addWidget(new QLabel(tr("Local proxy:"), ctl), 3, 0);
    m_proxy = new QLabel(tr("unknown"), ctl);
    m_proxy->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(m_proxy, 3, 1, 1, 2);

    grid->setColumnStretch(1, 1);
    root->addWidget(ctl);

    // --- connections + log ---
    auto* split = new QSplitter(Qt::Vertical, this);

    auto* connBox = new QGroupBox(tr("Connections"), split);
    auto* connLay = new QVBoxLayout(connBox);
    m_conns = new QTableWidget(0, CCount, connBox);
    m_conns->setHorizontalHeaderLabels({tr("ID"), tr("Protocol"), tr("Country"), tr("Server"),
                                        tr("State"), tr("Ping (ms)"), tr("")});
    m_conns->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_conns->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_conns->setSelectionMode(QAbstractItemView::SingleSelection);
    m_conns->verticalHeader()->setVisible(false);
    m_conns->horizontalHeader()->setStretchLastSection(false);
    m_conns->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_conns->setColumnWidth(CId, 220);
    m_conns->setColumnWidth(CProtocol, 90);
    m_conns->setColumnWidth(CCountry, 70);
    m_conns->setColumnWidth(CServer, 160);
    m_conns->setColumnWidth(CState, 110);
    m_conns->setColumnWidth(CPing, 80);
    m_conns->setColumnWidth(CAction, 110);
    connLay->addWidget(m_conns);

    auto* logBox = new QGroupBox(tr("Log"), split);
    auto* logLay = new QVBoxLayout(logBox);
    m_log = new QPlainTextEdit(logBox);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    logLay->addWidget(m_log);

    split->addWidget(connBox);
    split->addWidget(logBox);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    root->addWidget(split, 1);

    // --- user actions ---
    connect(m_mode, &QComboBox::activated, this, [this](int index) {
        emit modeSelected(m_mode->itemData(index).toString());
    });
    connect(m_bench, &QCheckBox::clicked, this, [this](bool on) { emit benchmarkToggled(on); });
    connect(m_loginBtn, &QPushButton::clicked, this, [this]() { emit loginRequested(); });
    connect(m_logoutBtn, &QPushButton::clicked, this, [this]() { emit logoutRequested(); });
}

void StatusTab::setUnreachable()
{
    m_login->setText(tr("core unreachable"));
    m_tor->setText(tr("core unreachable"));
    m_proxy->setText(tr("core unreachable"));
}

void StatusTab::setState(const QJsonObject& st)
{
    // Mode combo (do not fight the user while the drop-down is open).
    const QString mode = st.value(QStringLiteral("mode")).toString().toLower();
    const int idx = m_mode->findData(mode);
    if (idx >= 0 && idx != m_mode->currentIndex() && !m_mode->view()->isVisible()) {
        const QSignalBlocker blocker(m_mode);
        m_mode->setCurrentIndex(idx);
    }

    if (m_bench->isChecked() != st.value(QStringLiteral("benchmark_enabled")).toBool()) {
        const QSignalBlocker blocker(m_bench);
        m_bench->setChecked(st.value(QStringLiteral("benchmark_enabled")).toBool());
    }

    const bool loggedIn = st.value(QStringLiteral("logged_in")).toBool();
    m_login->setText(loggedIn ? tr("logged in") : tr("not logged in"));
    m_logoutBtn->setEnabled(loggedIn);

    const QJsonObject tor = st.value(QStringLiteral("tor")).toObject();
    m_tor->setText(tr("running: %1, bootstrapped: %2, SOCKS: %3")
                       .arg(Common::display(tor.value(QStringLiteral("running"))),
                            Common::display(tor.value(QStringLiteral("bootstrapped"))),
                            Common::display(tor.value(QStringLiteral("socks")))));

    const QString proxy = Common::display(st.value(QStringLiteral("proxy")));
    m_proxy->setText(proxy.isEmpty() ? tr("-") : proxy);

    // Connections: rebuild only when the set/order of ids changes, otherwise update in place.
    const QJsonArray conns = st.value(QStringLiteral("connections")).toArray();
    QStringList ids;
    for (const QJsonValue& v : conns) {
        ids << v.toObject().value(QStringLiteral("id")).toString();
    }
    if (ids != m_connIds) {
        rebuildConnections(conns);
    } else {
        for (int i = 0; i < conns.size(); ++i) {
            fillConnectionRow(i, conns.at(i).toObject());
        }
    }

    // Log tail.
    const QJsonArray tail = st.value(QStringLiteral("log_tail")).toArray();
    QStringList lines;
    for (const QJsonValue& v : tail) {
        lines << v.toString();
    }
    const QString text = lines.join(QLatin1Char('\n'));
    if (text != m_lastLog) {
        m_lastLog = text;
        QScrollBar* sb = m_log->verticalScrollBar();
        const bool atBottom = sb->value() >= sb->maximum() - 2;
        const int old = sb->value();
        m_log->setPlainText(text);
        sb->setValue(atBottom ? sb->maximum() : old);
    }
}

void StatusTab::rebuildConnections(const QJsonArray& conns)
{
    m_connIds.clear();
    m_conns->setRowCount(0);
    m_conns->setRowCount(conns.size());
    for (int r = 0; r < conns.size(); ++r) {
        const QJsonObject c = conns.at(r).toObject();
        const QString id = c.value(QStringLiteral("id")).toString();
        m_connIds << id;
        for (int col = 0; col < CAction; ++col) {
            auto* item = new QTableWidgetItem;
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_conns->setItem(r, col, item);
        }
        auto* btn = new QPushButton(tr("Disconnect"), m_conns);
        connect(btn, &QPushButton::clicked, this, [this, id]() { emit disconnectRequested(id); });
        m_conns->setCellWidget(r, CAction, btn);
        fillConnectionRow(r, c);
    }
}

void StatusTab::fillConnectionRow(int row, const QJsonObject& c)
{
    auto setText = [this, row](int col, const QString& text) {
        QTableWidgetItem* it = m_conns->item(row, col);
        if (it && it->text() != text) {
            it->setText(text);
        }
    };
    setText(CId, c.value(QStringLiteral("id")).toString());
    setText(CProtocol, c.value(QStringLiteral("protocol")).toString());
    setText(CCountry, c.value(QStringLiteral("country")).toString());
    setText(CServer, c.value(QStringLiteral("server")).toString());
    setText(CState, c.value(QStringLiteral("state")).toString());
    setText(CPing, pingText(c.value(QStringLiteral("ping_ms"))));
}
