#include "core/project/project.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace psi {

Project::Project(QObject* parent) : QObject(parent), doc_(this), stack_(doc_, this) {
    connect(&stack_, &CommandStack::changed, this, &Project::stateChanged);
    autosaveTimer_.setInterval(5 * 60 * 1000);
    connect(&autosaveTimer_, &QTimer::timeout, this, [this] { if (isDirty()) { QString e; autosave(&e); } });
    autosaveTimer_.start();
}

QString Project::displayName() const {
    if (!path_.isEmpty()) return QFileInfo(path_).completeBaseName();
    return meta_.name.isEmpty() ? QStringLiteral("Без названия") : meta_.name;
}

void Project::newProject() {
    doc_.reset();
    stack_.clear();
    meta_ = ProjectMeta{};
    foreign_.clear();
    path_.clear();
    dirtyFlag_ = false;
    Q_EMIT stateChanged();
}

bool Project::open(const QString& path, QString* error, QStringList* warnings) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = QStringLiteral("Не удалось открыть «%1»: %2").arg(path, f.errorString()); return false; }
    QByteArray bytes = f.readAll();
    Document tmp;
    ProjectMeta meta;
    QMap<QString, QByteArray> foreign;
    int schema = kSchemaVersion;
    if (!PsiFile::read(bytes, tmp, meta, foreign, error, warnings, &schema)) return false;
    // success: load into the live document (second parse keeps the live document untouched on failure)
    PsiFile::read(bytes, doc_, meta_, foreign_, error, nullptr, nullptr);
    stack_.clear();
    path_ = path;
    dirtyFlag_ = false;
    if (schema < kSchemaVersion) {
        const QString bak = path + ".bak";
        QFile::remove(bak);
        QFile::copy(path, bak);
        if (warnings) *warnings << QStringLiteral("Проект старой схемы %1 перенесён; копия сохранена в «%2»").arg(schema).arg(bak);
        dirtyFlag_ = true;
    }
    addRecent(path);
    Q_EMIT stateChanged();
    return true;
}

bool Project::saveAs(const QString& path, QString* error) {
    if (path.isEmpty()) { if (error) *error = QStringLiteral("Не указан файл для сохранения"); return false; }
    if (meta_.name.isEmpty() || meta_.name == QStringLiteral("Без названия")) meta_.name = QFileInfo(path).completeBaseName();
    QByteArray bytes = PsiFile::write(doc_, meta_, foreign_);
    if (!PsiFile::save(path, bytes, error)) return false;
    QFile::remove(autosavePath(path_.isEmpty() ? path : path_));
    QFile::remove(unsavedAutosavePath());
    path_ = path;
    dirtyFlag_ = false;
    stack_.setClean();
    addRecent(path);
    Q_EMIT stateChanged();
    return true;
}

QString Project::autosavePath(const QString& projectPath) {
    if (projectPath.isEmpty()) return unsavedAutosavePath();
    QFileInfo fi(projectPath);
    return fi.absolutePath() + "/" + fi.completeBaseName() + ".autosave.psi";
}

QString Project::unsavedAutosavePath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/Без названия.autosave.psi";
}

bool Project::autosave(QString* error) {
    QByteArray bytes = PsiFile::write(doc_, meta_, foreign_);
    const QString p = autosavePath(path_);
    if (!PsiFile::save(p, bytes, error)) return false;
    Q_EMIT autosaved(p);
    return true;
}

bool Project::hasRecovery(const QString& projectPath) {
    QFileInfo a(autosavePath(projectPath));
    if (!a.exists()) return false;
    if (projectPath.isEmpty()) return true;
    QFileInfo p(projectPath);
    return !p.exists() || a.lastModified() > p.lastModified();
}

void Project::discardRecovery(const QString& projectPath) { QFile::remove(autosavePath(projectPath)); }

void Project::setAutosaveMinutes(int minutes) {
    if (minutes <= 0) autosaveTimer_.stop();
    else { autosaveTimer_.setInterval(minutes * 60 * 1000); autosaveTimer_.start(); }
}

QStringList Project::recentFiles() {
    QSettings s;
    QStringList list = s.value("recentFiles").toStringList();
    QStringList out;
    for (const QString& p : list) if (QFileInfo::exists(p)) out << p;
    return out;
}

void Project::addRecent(const QString& path) {
    QSettings s;
    QStringList list = s.value("recentFiles").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 10) list.removeLast();
    s.setValue("recentFiles", list);
}

void Project::clearRecent() { QSettings().remove("recentFiles"); }

} // namespace psi
