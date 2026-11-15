#include "ApiClient.h"
#include "Common.h"
#include "MainWindow.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QString>

int main(int argc, char* argv[])
{
    // The whole GUI process (and therefore Chromium inside QtWebEngine) goes through Tor.
    // This MUST happen before QApplication is constructed. The control API client uses
    // QNetworkAccessManager with QNetworkProxy::NoProxy, which is unaffected by this flag.
    const QByteArray chromiumFlags = QByteArray("--proxy-server=socks5://127.0.0.1:")
        + QByteArray::number(Common::torSocksPort());
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags);

    // Required by QtWebEngine when it is created after the application object.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(QStringLiteral("pvc"));
    QCoreApplication::setApplicationName(QStringLiteral("pvc-gui"));

    bool portOk = false;
    int port = qEnvironmentVariableIntValue("PVC_CONTROL_PORT", &portOk);
    if (!portOk || port <= 0 || port > 65535) {
        port = 8081; // default from docs/control-api.md
    }
    const QString token = qEnvironmentVariable("PVC_TOKEN");

    // Declared in this order so that the window is destroyed before the API client,
    // and the API client before the application object.
    ApiClient api(port, token);
    MainWindow window(&api);
    window.start();

    return app.exec();
}
