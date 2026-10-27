#pragma once
#include "core/command-system/command.h"
#include "core/document/document.h"
#include "io/native/psi_file.h"
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace psi {

// Project lifecycle: new / open / save / autosave / recovery / recent files.
class Project : public QObject {
    Q_OBJECT
public:
    explicit Project(QObject* parent = nullptr);

    Document& document() { return doc_; }
    const Document& document() const { return doc_; }
    CommandStack& commands() { return stack_; }
    ProjectMeta& meta() { return meta_; }
    QMap<QString, QByteArray>& foreignParts() { return foreign_; }

    QString path() const { return path_; }
    QString displayName() const;
    bool isDirty() const { return !stack_.isClean() || dirtyFlag_; }
    void markDirty() { dirtyFlag_ = true; Q_EMIT stateChanged(); }

    void newProject();
    bool open(const QString& path, QString* error, QStringList* warnings = nullptr);
    bool save(QString* error) { return saveAs(path_, error); }
    bool saveAs(const QString& path, QString* error);
    bool autosave(QString* error);                          // <path>.autosave.psi (or in the data dir when unsaved)
    static QString autosavePath(const QString& projectPath);
    static QString unsavedAutosavePath();
    static bool hasRecovery(const QString& projectPath);    // autosave newer than the project
    void discardRecovery(const QString& projectPath);
    void setAutosaveMinutes(int minutes);

    static QStringList recentFiles();
    static void addRecent(const QString& path);
    static void clearRecent();

Q_SIGNALS:
    void stateChanged();        // path / dirty / name
    void autosaved(const QString& path);

private:
    Document doc_;
    CommandStack stack_;
    ProjectMeta meta_;
    QMap<QString, QByteArray> foreign_;
    QString path_;
    bool dirtyFlag_ = false;
    QTimer autosaveTimer_;
};

} // namespace psi
