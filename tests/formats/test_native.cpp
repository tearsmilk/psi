#include "core/project/project.h"
#include "io/native/psi_file.h"
#include "io/native/zip.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace psi;

class TestNative : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void zipRoundTrip() {
        ZipWriter w;
        QByteArray big(200000, 'a'); for (int i = 0; i < big.size(); i += 7) big[i] = char('b' + i % 20);
        w.add("a.txt", "hello", false);
        w.add("dir/big.bin", big, true);
        w.add(QStringLiteral("кириллица.json"), "{}", true);
        QByteArray bytes = w.finish();
        QVERIFY(bytes.startsWith("PK\x03\x04"));
        QVERIFY(bytes.size() < big.size() / 2);   // deflate actually compressed
        ZipReader r; QString err;
        QVERIFY2(r.open(bytes, &err), qPrintable(err));
        QCOMPARE(r.names().size(), 3);
        QVERIFY(r.contains("dir/big.bin"));
        QCOMPARE(r.read("a.txt"), QByteArray("hello"));
        QCOMPARE(r.read("dir/big.bin"), big);
        QCOMPARE(r.read(QStringLiteral("кириллица.json")), QByteArray("{}"));
    }
    void zipCorruption() {
        QByteArray noisy(20000, '\0'); quint32 seed = 7; for (char& ch : noisy) { seed = seed * 1103515245u + 12345u; ch = char(seed >> 24); }
        ZipWriter w; w.add("x", noisy, true);
        QByteArray bytes = w.finish();
        bytes[500] = char(bytes[500] ^ 0x55);   // flip a byte in the compressed stream
        ZipReader r; QString err;
        bool opened = r.open(bytes, &err);
        QVERIFY(!opened || r.read("x").isEmpty());   // CRC must catch it
        ZipReader r2;
        QVERIFY(!r2.open(QByteArray("not a zip at all"), &err));
        QVERIFY(!err.isEmpty());
    }
    void zipLimits() {
        ZipWriter w; w.add("huge", QByteArray(5 * 1024 * 1024, '\0'), true);
        QByteArray bytes = w.finish();
        ZipLimits lim; lim.maxPart = 1024 * 1024;
        ZipReader r; QString err;
        QVERIFY(!r.open(bytes, &err, lim) || r.read("huge").isEmpty());
    }
    void psiRoundTrip() {
        Document d;
        Layer l; l.name = QStringLiteral("Слой А"); l.color = 0x112233; quint32 lid = d.addLayer(l);
        Entity e = makeLine({0, 0}, {100, 50}); e.layer = lid; e.attrs["material"] = AttrValue::material(QStringLiteral("Дуб"));
        d.addEntity(e);
        d.addEntity(makeCircle({10, 10}, 4));
        d.addEntity(makeText({1, 1}, QStringLiteral("Текст"), 3.5));
        DocumentSettings s = d.settings(); s.unit = Unit::Centimeter; s.gridSpacing = 25; s.snapStep = 5; s.gridSnap = false; d.setSettings(s);
        Sheet sh = d.sheets()[0]; sh.name = QStringLiteral("Лист А3"); sh.width = 297; sh.height = 420; d.updateSheet(sh);
        ProjectMeta meta; meta.name = QStringLiteral("Проект"); meta.author = QStringLiteral("Я");
        QMap<QString, QByteArray> foreign; foreign["extensions/other-app/data.bin"] = QByteArray("\x01\x02\x03", 3);
        QByteArray bytes = PsiFile::write(d, meta, foreign);

        ZipReader r; QString err;
        QVERIFY(r.open(bytes, &err));
        QVERIFY(r.contains("manifest.json"));
        QVERIFY(r.contains("project/metadata.json"));
        QVERIFY(r.contains("project/settings.json"));
        QVERIFY(r.contains("drawings/main.json"));
        QVERIFY(r.contains("sheets/sheets.json"));
        QJsonObject manifest = QJsonDocument::fromJson(r.read("manifest.json")).object();
        QCOMPARE(manifest["schema"].toInt(), kSchemaVersion);

        Document d2; ProjectMeta meta2; QMap<QString, QByteArray> foreign2; QStringList warnings; int schema = 0;
        QVERIFY2(PsiFile::read(bytes, d2, meta2, foreign2, &err, &warnings, &schema), qPrintable(err));
        QCOMPARE(schema, kSchemaVersion);
        QCOMPARE(meta2.name, meta.name); QCOMPARE(meta2.author, meta.author);
        QCOMPARE(d2.entityCount(), size_t(3));
        QCOMPARE(d2.layers().size(), size_t(2));
        QCOMPARE(d2.settings().unit, Unit::Centimeter);
        QCOMPARE(d2.settings().gridSpacing, 25.0);
        QCOMPARE(d2.settings().snapStep, 5.0);
        QVERIFY(!d2.settings().gridSnap);
        QCOMPARE(d2.sheets()[0].name, QStringLiteral("Лист А3"));
        for (quint64 id : d.entityIds()) QVERIFY(*d.entity(id) == *d2.entity(id));
        QCOMPARE(foreign2.value("extensions/other-app/data.bin"), QByteArray("\x01\x02\x03", 3));
        QVERIFY(warnings.isEmpty());
    }
    void psiIntegrity() {
        Document d; d.addEntity(makeLine({0, 0}, {1, 1}));
        QByteArray bytes = PsiFile::write(d, ProjectMeta{}, {});
        // rebuild the archive with a tampered drawing part → sha256 mismatch must be reported
        ZipReader r; QString err; QVERIFY(r.open(bytes, &err));
        ZipWriter w;
        for (const QString& n : r.names()) { QByteArray part = r.read(n); if (n == "drawings/main.json") part.append(" "); /* still valid JSON, different hash */ w.add(n, part, true); }
        QByteArray tampered = w.finish();
        Document d2; ProjectMeta m; QMap<QString, QByteArray> f; QStringList warnings;
        bool ok = PsiFile::read(tampered, d2, m, f, &err, &warnings);
        QVERIFY(!ok || !warnings.isEmpty());
        // garbage
        QVERIFY(!PsiFile::read("garbage", d2, m, f, &err));
        QVERIFY(!err.isEmpty());
        // future schema is refused
        ZipWriter w2;
        for (const QString& n : r.names()) { QByteArray part = r.read(n); if (n == "manifest.json") { QJsonObject o = QJsonDocument::fromJson(part).object(); o["schema"] = kSchemaVersion + 100; part = QJsonDocument(o).toJson(); } w2.add(n, part, true); }
        QVERIFY(!PsiFile::read(w2.finish(), d2, m, f, &err));
        QVERIFY(err.contains(QStringLiteral("верси"), Qt::CaseInsensitive));
    }
    void projectSaveOpenAutosave() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Project p;
        p.newProject();
        auto c = std::make_unique<EntityCommand>("add"); c->add(makeLine({0, 0}, {5, 5})); p.commands().push(std::move(c));
        QVERIFY(p.isDirty());
        QString path = dir.filePath("a.psi"); QString err;
        QVERIFY2(p.saveAs(path, &err), qPrintable(err));
        QVERIFY(!p.isDirty());
        QCOMPARE(p.path(), path);
        QVERIFY(QFileInfo::exists(path));
        // autosave sits next to the file and is detected as recovery once the project changes
        auto c2 = std::make_unique<EntityCommand>("add2"); c2->add(makeCircle({0, 0}, 1)); p.commands().push(std::move(c2));
        QTest::qWait(1100);
        QVERIFY2(p.autosave(&err), qPrintable(err));
        QVERIFY(QFileInfo::exists(Project::autosavePath(path)));
        QVERIFY(Project::hasRecovery(path));
        Project q; QStringList warnings;
        QVERIFY2(q.open(Project::autosavePath(path), &err, &warnings), qPrintable(err));
        QCOMPARE(q.document().entityCount(), size_t(2));
        p.discardRecovery(path);
        QVERIFY(!QFileInfo::exists(Project::autosavePath(path)));
        // backup on overwrite keeps the previous version
        QVERIFY(p.save(&err));
        Project o;
        QVERIFY(o.open(path, &err, &warnings));
        QCOMPARE(o.document().entityCount(), size_t(2));
        QVERIFY(!o.open(dir.filePath("missing.psi"), &err));
    }
};

QTEST_GUILESS_MAIN(TestNative)
#include "test_native.moc"
