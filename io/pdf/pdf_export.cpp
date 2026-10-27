#include "io/pdf/pdf_export.h"
#include "rendering/renderer.h"
#include <QFile>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTemporaryFile>

namespace psi {

QStringList PdfExport::checkClipping(const Document& doc, const Sheet& s) {
    QStringList out;
    Rect page(0, 0, s.pageWidth(), s.pageHeight());
    int i = 0;
    for (const SheetViewport& vp : s.viewports) {
        ++i;
        if (!page.contains(vp.frame)) out << QStringLiteral("Лист «%1»: видовой экран %2 выходит за край страницы").arg(s.name).arg(i);
        Rect content;
        if (!Renderer::viewportFits(doc, vp, &content))
            out << QStringLiteral("Лист «%1»: чертёж не помещается в видовой экран %2 (масштаб %3) — часть будет обрезана").arg(s.name).arg(i).arg(scaleLabel(vp.scale));
    }
    return out;
}

// Rewrites every "/MediaBox [0 0 W H]" with the exact size in points, keeping the byte length (so the xref table
// stays valid): the number of decimals is adjusted to match the token Qt wrote.
bool PdfExport::fixMediaBoxes(const QString& path, const std::vector<QSizeF>& pagesMm, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) { if (error) *error = QStringLiteral("Не удалось открыть PDF для правки размера страницы"); return false; }
    QByteArray bytes = f.readAll();
    auto sameLength = [](double value, int length) {
        QByteArray s = QByteArray::number(value, 'f', 6);
        int dot = s.indexOf('.');
        if (s.size() > length) { if (dot >= 0 && dot < length) s = s.left(length); else return QByteArray(); }
        while (s.size() < length) s += '0';
        return s;
    };
    int pos = 0; size_t page = 0;
    while ((pos = bytes.indexOf("/MediaBox", pos)) >= 0) {
        int open = bytes.indexOf('[', pos), close = bytes.indexOf(']', pos);
        if (open < 0 || close < 0 || page >= pagesMm.size()) break;
        QList<QByteArray> parts = bytes.mid(open + 1, close - open - 1).simplified().split(' ');
        if (parts.size() == 4) {
            QByteArray w = sameLength(pagesMm[page].width() * 72.0 / 25.4, parts[2].size());
            QByteArray h = sameLength(pagesMm[page].height() * 72.0 / 25.4, parts[3].size());
            if (!w.isEmpty() && !h.isEmpty()) {
                QByteArray repl = parts[0] + ' ' + parts[1] + ' ' + w + ' ' + h;
                QByteArray orig = bytes.mid(open + 1, close - open - 1);
                if (repl.size() == orig.simplified().size()) { QByteArray padded = repl; while (padded.size() < orig.size()) padded += ' '; if (padded.size() == orig.size()) bytes.replace(open + 1, orig.size(), padded); }
            }
        }
        ++page;
        pos = close;
    }
    f.seek(0); f.write(bytes); f.resize(bytes.size()); f.close();
    return true;
}

bool PdfExport::exportSheets(const Document& doc, const std::vector<quint64>& sheetIds, const QString& path, QString* error,
                             QStringList* warnings, const std::function<bool(int, int)>& progress) {
    std::vector<const Sheet*> sheets;
    for (quint64 id : sheetIds) if (const Sheet* s = doc.sheet(id)) sheets.push_back(s);
    if (sheets.empty()) { if (error) *error = QStringLiteral("Не выбран ни один лист"); return false; }
    QTemporaryFile tmp(QFileInfo(path).absolutePath() + "/psi-export-XXXXXX.pdf");
    tmp.setAutoRemove(false);
    if (!tmp.open()) { if (error) *error = QStringLiteral("Не удалось создать файл рядом с «%1»").arg(path); return false; }
    const QString tmpPath = tmp.fileName();
    tmp.close();
    {
        QPdfWriter writer(tmpPath);
        writer.setResolution(1200);
        writer.setTitle(QStringLiteral("PSI"));
        writer.setCreator(QStringLiteral("PSI ") + PSI_VERSION);
        writer.setPdfVersion(QPdfWriter::PdfVersion_1_6);
        const double k = 1200.0 / 25.4;   // device px per mm
        QPainter painter;
        int n = 0;
        for (const Sheet* s : sheets) {
            if (warnings) *warnings += checkClipping(doc, *s);
            QPageSize size(QSizeF(s->pageWidth(), s->pageHeight()), QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
            QPageLayout layout(size, QPageLayout::Portrait, QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
            layout.setMode(QPageLayout::FullPageMode);
            if (n == 0) {
                writer.setPageLayout(layout);
                if (!painter.begin(&writer)) { if (error) *error = QStringLiteral("Не удалось начать запись PDF"); return false; }
            } else {
                writer.setPageLayout(layout);
                writer.newPage();
            }
            painter.setRenderHint(QPainter::Antialiasing, true);
            // Qt rounds the page box to whole points; the content is anchored to the top edge, so shift it down by the
            // rounding remainder — after fixMediaBoxes() below the geometry sits exactly on the true page.
            const double hPt = s->pageHeight() * 72.0 / 25.4;
            const double shiftPx = (qRound(hPt) - hPt) * 1200.0 / 72.0;
            // sheet mm (y up) → device px (y down)
            QTransform t;
            t.translate(0, shiftPx + s->pageHeight() * k);
            t.scale(k, -k);
            Renderer::drawSheet(painter, doc, *s, t, k, true);
            ++n;
            if (progress && !progress(n, static_cast<int>(sheets.size()))) { painter.end(); QFile::remove(tmpPath); if (error) *error = QStringLiteral("Экспорт отменён"); return false; }
        }
        painter.end();
    }
    {
        std::vector<QSizeF> pages;
        for (const Sheet* s : sheets) pages.emplace_back(s->pageWidth(), s->pageHeight());
        QString fixErr;
        if (!fixMediaBoxes(tmpPath, pages, &fixErr)) { QFile::remove(tmpPath); if (error) *error = fixErr; return false; }
    }
    QFile::remove(path);
    if (!QFile::rename(tmpPath, path)) { QFile::remove(tmpPath); if (error) *error = QStringLiteral("Не удалось записать «%1»").arg(path); return false; }
    return true;
}

} // namespace psi
