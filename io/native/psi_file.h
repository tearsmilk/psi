#pragma once
#include "core/document/document.h"
#include <QByteArray>
#include <QMap>
#include <QString>

namespace psi {

constexpr int kSchemaVersion = 1;

struct ProjectMeta {
    QString name = QStringLiteral("Без названия");
    QString author, description;
    QString created, modified;   // ISO 8601
    QString appVersion;
    QJsonObject toJson() const;
    static ProjectMeta fromJson(const QJsonObject& o);
};

// The .psi container: manifest.json + project/metadata.json + project/settings.json + drawings/main.json + sheets/sheets.json …
// Parts we do not understand are kept in `foreign` and written back unchanged.
struct PsiFile {
    static QByteArray write(const Document& doc, const ProjectMeta& meta, const QMap<QString, QByteArray>& foreign);
    static bool read(const QByteArray& bytes, Document& doc, ProjectMeta& meta, QMap<QString, QByteArray>& foreign,
                     QString* error, QStringList* warnings = nullptr, int* schemaVersion = nullptr);
    // atomic write (temp file + rename); backs up the previous file to <path>.bak when `backup`
    static bool save(const QString& path, const QByteArray& bytes, QString* error, bool backup = false);
};

} // namespace psi
