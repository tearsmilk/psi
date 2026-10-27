#include "main_window.h"
#include "selftest.h"
#include "ui/theme.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <cstring>

int main(int argc, char** argv) {
    bool selftest = false;
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;
    if (selftest && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("tearsmilk"));
    QApplication::setOrganizationDomain(QStringLiteral("psi.ink"));
    QApplication::setApplicationName(QStringLiteral("Psi"));
    QApplication::setApplicationDisplayName(QStringLiteral("Пси"));
    QApplication::setApplicationVersion(QStringLiteral(PSI_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Пси — универсальная CAD-система"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("selftest"), QStringLiteral("Выполнить самопроверку без окна и выйти")});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Файл .psi или .dxf для открытия"));
    parser.process(app);

    if (parser.isSet(QStringLiteral("selftest"))) return psi::runSelfTest();

    QSettings st;
    QString mode = st.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
    QColor accent(st.value(QStringLiteral("ui/accent"), psi::Theme::defaultAccent().name()).toString());
    if (!accent.isValid()) accent = psi::Theme::defaultAccent();
    psi::Theme::apply(app, mode == "dark" ? psi::ThemeMode::Dark : mode == "light" ? psi::ThemeMode::Light : psi::ThemeMode::System, accent);

    psi::MainWindow w;
    w.show();
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) w.openPath(args.first());
    return app.exec();
}
