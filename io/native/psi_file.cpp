#include "io/native/psi_file.h"
#include "io/native/zip.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace psi {

QJsonObject ProjectMeta::toJson() const {
    QJsonObject o;
    o["name"] = name; o["author"] = author; o["description"] = description; o["created"] = created; o["modified"] = modified; o["app"] = appVersion;
    return o;
}
ProjectMeta ProjectMeta::fromJson(const QJsonObject& o) {
    ProjectMeta m;
    m.name = o.value("name").toString(m.name); m.author = o.value("author").toString(); m.description = o.value("description").toString();
    m.created = o.value("created").toString(); m.modified = o.value("modified").toString(); m.appVersion = o.value("app").toString();
    return m;
}

static QByteArray json(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
static QString sha(const QByteArray& b) { return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex()); }

static const QStringList kOwnParts = {"manifest.json", "project/metadata.json", "project/settings.json", "drawings/main.json", "sheets/sheets.json"};

QByteArray PsiFile::write(const Document& doc, const ProjectMeta& metaIn, const QMap<QString, QByteArray>& foreign) {
    ProjectMeta meta = metaIn;
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (meta.created.isEmpty()) meta.created = now;
    meta.modified = now;
    meta.appVersion = QStringLiteral("PSI ") + PSI_VERSION;

    QMap<QString, QByteArray> parts;
    parts["project/metadata.json"] = json(meta.toJson());
    parts["project/settings.json"] = json(doc.settings().toJson());
    parts["drawings/main.json"] = json(doc.drawingToJson());
    parts["sheets/sheets.json"] = json(doc.sheetsToJson());
    for (auto it = foreign.cbegin(); it != foreign.cend(); ++it)
        if (!kOwnParts.contains(it.key())) parts[it.key()] = it.value();

    QJsonObject manifest;
    manifest["format"] = "psi";
    manifest["schema"] = kSchemaVersion;
    manifest["app"] = meta.appVersion;
    manifest["created"] = meta.created;
    manifest["modified"] = meta.modified;
    QJsonArray list;
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
        QJsonObject p; p["path"] = it.key(); p["sha256"] = sha(it.value()); p["size"] = it.value().size();
        list.push_back(p);
    }
    manifest["parts"] = list;

    ZipWriter z;
    z.add("manifest.json", json(manifest));
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) z.add(it.key(), it.value());
    return z.finish();
}

// schema migrations: each step upgrades `parts` from version N to N+1
static bool migrate(QMap<QString, QByteArray>& parts, int from, QStringList* warnings) {
    for (int v = from; v < kSchemaVersion; ++v) {
        switch (v) {
        default:
            if (warnings) *warnings << QStringLiteral("Нет миграции со схемы %1 — файл открыт как есть").arg(v);
            return true;
        }
    }
    (void)parts;
    return true;
}

bool PsiFile::read(const QByteArray& bytes, Document& doc, ProjectMeta& meta, QMap<QString, QByteArray>& foreign,
                   QString* error, QStringList* warnings, int* schemaVersion) {
    ZipReader z;
    if (!z.open(bytes, error)) return false;
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (!z.contains("manifest.json")) return fail(QStringLiteral("Это не проект PSI: нет manifest.json"));
    QString e;
    QJsonParseError pe{};
    QJsonDocument md = QJsonDocument::fromJson(z.read("manifest.json", &e), &pe);
    if (!e.isEmpty()) return fail(e);
    if (pe.error != QJsonParseError::NoError || !md.isObject()) return fail(QStringLiteral("manifest.json повреждён"));
    QJsonObject manifest = md.object();
    if (manifest.value("format").toString() != "psi") return fail(QStringLiteral("Это не проект PSI (формат «%1»)").arg(manifest.value("format").toString()));
    int schema = manifest.value("schema").toInt(0);
    if (schemaVersion) *schemaVersion = schema;
    if (schema > kSchemaVersion)
        return fail(QStringLiteral("Файл создан более новой версией PSI (схема %1, поддерживается %2). Обновите программу.").arg(schema).arg(kSchemaVersion));

    QMap<QString, QByteArray> parts;
    for (const QString& name : z.names()) {
        if (name == "manifest.json" || name.endsWith('/')) continue;
        QByteArray data = z.read(name, &e);
        if (!e.isEmpty()) return fail(e);
        parts[name] = data;
    }
    // integrity check against the manifest
    for (const auto& pv : manifest.value("parts").toArray()) {
        QJsonObject p = pv.toObject();
        QString path = p.value("path").toString();
        if (!parts.contains(path)) {
            if (kOwnParts.contains(path)) return fail(QStringLiteral("В проекте отсутствует часть «%1»").arg(path));
            if (warnings) *warnings << QStringLiteral("Отсутствует ресурс «%1»").arg(path);
            continue;
        }
        if (!p.value("sha256").toString().isEmpty() && p.value("sha256").toString() != sha(parts[path]))
            return fail(QStringLiteral("Часть «%1» повреждена (контрольная сумма не совпала)").arg(path));
    }
    if (!migrate(parts, schema, warnings)) return fail(QStringLiteral("Не удалось перенести проект на текущую схему"));

    auto obj = [&](const QString& name, QJsonObject* out) {
        if (!parts.contains(name)) return false;
        QJsonParseError pe2{};
        QJsonDocument d = QJsonDocument::fromJson(parts[name], &pe2);
        if (pe2.error != QJsonParseError::NoError || !d.isObject()) return false;
        *out = d.object();
        return true;
    };
    QJsonObject metaJ, settingsJ, drawingJ, sheetsJ;
    if (!obj("drawings/main.json", &drawingJ)) return fail(QStringLiteral("Повреждён чертёж (drawings/main.json)"));
    if (!obj("project/metadata.json", &metaJ)) { if (warnings) *warnings << QStringLiteral("Нет метаданных проекта — использованы значения по умолчанию"); }
    if (!obj("project/settings.json", &settingsJ)) { if (warnings) *warnings << QStringLiteral("Нет настроек документа — использованы значения по умолчанию"); }
    if (!obj("sheets/sheets.json", &sheetsJ)) { if (warnings) *warnings << QStringLiteral("Нет листов — создан лист A4"); }

    meta = ProjectMeta::fromJson(metaJ);
    doc.reset();
    doc.setSettings(DocumentSettings::fromJson(settingsJ));
    doc.drawingFromJson(drawingJ);
    doc.sheetsFromJson(sheetsJ);
    foreign.clear();
    for (auto it = parts.cbegin(); it != parts.cend(); ++it)
        if (!kOwnParts.contains(it.key())) foreign[it.key()] = it.value();
    return true;
}

bool PsiFile::save(const QString& path, const QByteArray& bytes, QString* error, bool backup) {
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());
    if (backup && fi.exists()) {
        const QString bak = path + ".bak";
        QFile::remove(bak);
        QFile::copy(path, bak);
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = QStringLiteral("Не удалось открыть «%1» для записи: %2").arg(path, f.errorString()); return false; }
    if (f.write(bytes) != bytes.size() || !f.commit()) { if (error) *error = QStringLiteral("Не удалось записать «%1»: %2").arg(path, f.errorString()); return false; }
    return true;
}

} // namespace psi
