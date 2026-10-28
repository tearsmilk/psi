#include "io/step/step.h"
#include "modules/model/kernel_occt.h"

#include <Interface_Static.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <QFileInfo>

namespace psi {

// STEPControl печатает статистику переноса в консоль — оставляем только предупреждения и ошибки.
static void quietOcct() {
    const Message_SequenceOfPrinters& printers = Message::DefaultMessenger()->Printers();
    for (Message_SequenceOfPrinters::Iterator it(printers); it.More(); it.Next()) it.Value()->SetTraceLevel(Message_Warning);
}

QString StepReport::summary() const {
    QStringList l;
    l << QStringLiteral("Корней: %1, тел: %2, граней: %3").arg(roots).arg(bodies).arg(faces);
    l << QStringLiteral("Единицы: %1").arg(unit);
    if (!unsupported.isEmpty()) l << QStringLiteral("Пропущено: %1").arg(unsupported.join(QStringLiteral(", ")));
    for (const QString& w : warnings) l << QStringLiteral("⚠ %1").arg(w);
    if (cancelled) l << QStringLiteral("Операция отменена");
    return l.join(QLatin1Char('\n'));
}

bool Step::write(const QString& path, const std::vector<StepBody>& bodies, QString* error, StepReport* report) {
    StepReport rep;
    try {
        quietOcct();
        Interface_Static::SetCVal("write.step.unit", "MM");
        Interface_Static::SetCVal("write.step.schema", "AP214");
        Interface_Static::SetIVal("write.precision.mode", 0);
        STEPControl_Writer writer;
        for (const StepBody& b : bodies) {
            if (b.shape.isNull()) { rep.unsupported << QStringLiteral("«%1»: пустое тело").arg(b.name); continue; }
            if (b.shape.solidCount() == 0 && b.shape.faceCount() == 1 && b.shape.edgeCount() == 0) {
                rep.unsupported << QStringLiteral("«%1»: тело-сетка (STL) — в STEP не экспортируется, сохраните в STL").arg(b.name);
                continue;
            }
            Interface_Static::SetCVal("write.step.product.name", b.name.isEmpty() ? "PSI body" : b.name.toUtf8().constData());
            if (writer.Transfer(md::occShape(b.shape), STEPControl_AsIs) != IFSelect_RetDone) { rep.warnings << QStringLiteral("«%1»: тело не перенесено").arg(b.name); continue; }
            ++rep.bodies;
            rep.faces += b.shape.faceCount();
        }
        if (rep.bodies == 0) { if (error) *error = QStringLiteral("Нет тел для записи в STEP"); if (report) *report = rep; return false; }
        if (writer.Write(path.toUtf8().constData()) != IFSelect_RetDone) { if (error) *error = QStringLiteral("Не удалось записать %1").arg(path); if (report) *report = rep; return false; }
        rep.roots = rep.bodies;
    } catch (const Standard_Failure& f) { if (error) *error = QStringLiteral("STEP: %1").arg(QString::fromUtf8(f.GetMessageString())); if (report) *report = rep; return false; }
    if (report) *report = rep;
    return true;
}

std::vector<StepBody> Step::read(const QString& path, StepReport* report, QString* error, const std::function<bool()>& cancelled) {
    std::vector<StepBody> out;
    StepReport rep;
    if (!QFileInfo::exists(path)) { if (error) *error = QStringLiteral("Файл не найден: %1").arg(path); if (report) *report = rep; return out; }
    try {
        quietOcct();
        Interface_Static::SetCVal("xstep.cascade.unit", "MM");
        STEPControl_Reader reader;
        IFSelect_ReturnStatus st = reader.ReadFile(path.toUtf8().constData());
        if (st != IFSelect_RetDone) { if (error) *error = QStringLiteral("STEP не прочитан (код %1): файл повреждён или не STEP").arg(int(st)); if (report) *report = rep; return out; }
        rep.roots = reader.NbRootsForTransfer();
        rep.unit = QStringLiteral("мм (единицы файла пересчитаны)");
        for (int i = 1; i <= rep.roots; ++i) {
            if (cancelled && cancelled()) { rep.cancelled = true; break; }
            if (!reader.TransferOneRoot(i)) { rep.unsupported << QStringLiteral("корень %1 не перенесён").arg(i); continue; }
        }
        for (int i = 1; i <= reader.NbShapes(); ++i) {
            TopoDS_Shape s = reader.Shape(i);
            if (s.IsNull()) continue;
            StepBody b;
            b.shape = md::fromOcc(s);
            b.name = QStringLiteral("Тело %1").arg(i);
            if (b.shape.faceCount() == 0) { rep.unsupported << QStringLiteral("корень %1 без граней (точки / кривые)").arg(i); continue; }
            rep.faces += b.shape.faceCount();
            if (b.shape.solidCount() == 0) rep.warnings << QStringLiteral("«%1»: поверхности без тела — булевы операции недоступны").arg(b.name);
            out.push_back(b);
        }
        rep.bodies = int(out.size());
        if (rep.bodies) rep.warnings << QStringLiteral("Имена продуктов STEP не переносятся (без XDE), тела названы «Тело N»");
        if (rep.bodies == 0 && !rep.cancelled && error) *error = QStringLiteral("В STEP нет тел");
    } catch (const Standard_Failure& f) { if (error) *error = QStringLiteral("STEP: %1").arg(QString::fromUtf8(f.GetMessageString())); }
    if (report) *report = rep;
    return out;
}

} // namespace psi
