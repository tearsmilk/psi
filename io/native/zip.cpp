#include "io/native/zip.h"
#include <QDataStream>
#include <zlib.h>

namespace psi {

static QByteArray rawDeflate(const QByteArray& in) {
    z_stream s{};
    if (deflateInit2(&s, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
    QByteArray out;
    out.resize(static_cast<int>(deflateBound(&s, static_cast<uLong>(in.size()))));
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
    s.avail_in = static_cast<uInt>(in.size());
    s.next_out = reinterpret_cast<Bytef*>(out.data());
    s.avail_out = static_cast<uInt>(out.size());
    int rc = deflate(&s, Z_FINISH);
    out.resize(static_cast<int>(s.total_out));
    deflateEnd(&s);
    return rc == Z_STREAM_END ? out : QByteArray();
}

static bool rawInflate(const char* data, quint32 csize, quint32 usize, QByteArray* out) {
    z_stream s{};
    if (inflateInit2(&s, -15) != Z_OK) return false;
    out->resize(static_cast<int>(usize));
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    s.avail_in = csize;
    s.next_out = reinterpret_cast<Bytef*>(out->data());
    s.avail_out = usize;
    int rc = inflate(&s, Z_FINISH);
    bool ok = (rc == Z_STREAM_END) && s.total_out == usize;
    inflateEnd(&s);
    return ok;
}

static void put16(QByteArray& b, quint16 v) { b.append(static_cast<char>(v & 0xff)); b.append(static_cast<char>(v >> 8)); }
static void put32(QByteArray& b, quint32 v) { put16(b, static_cast<quint16>(v & 0xffff)); put16(b, static_cast<quint16>(v >> 16)); }
static quint16 get16(const char* p) { return static_cast<quint16>(static_cast<quint8>(p[0]) | (static_cast<quint8>(p[1]) << 8)); }
static quint32 get32(const char* p) { return static_cast<quint32>(get16(p)) | (static_cast<quint32>(get16(p + 2)) << 16); }

void ZipWriter::add(const QString& name, const QByteArray& data, bool compress) {
    Entry e;
    e.name = name;
    e.usize = static_cast<quint32>(data.size());
    e.crc = static_cast<quint32>(crc32(0L, reinterpret_cast<const Bytef*>(data.constData()), static_cast<uInt>(data.size())));
    e.method = 0;
    e.data = data;
    if (compress && data.size() > 64) {
        QByteArray c = rawDeflate(data);
        if (!c.isEmpty() && c.size() < data.size()) { e.data = c; e.method = 8; }
    }
    e.offset = static_cast<quint32>(body_.size());
    QByteArray n = name.toUtf8();
    put32(body_, 0x04034b50); put16(body_, 20); put16(body_, 0x0800); put16(body_, e.method); put16(body_, 0); put16(body_, 0x21);
    put32(body_, e.crc); put32(body_, static_cast<quint32>(e.data.size())); put32(body_, e.usize);
    put16(body_, static_cast<quint16>(n.size())); put16(body_, 0);
    body_.append(n); body_.append(e.data);
    entries_.push_back(e);
}

QByteArray ZipWriter::finish() const {
    QByteArray out = body_;
    const quint32 cdStart = static_cast<quint32>(out.size());
    for (const Entry& e : entries_) {
        QByteArray n = e.name.toUtf8();
        put32(out, 0x02014b50); put16(out, 20); put16(out, 20); put16(out, 0x0800); put16(out, e.method); put16(out, 0); put16(out, 0x21);
        put32(out, e.crc); put32(out, static_cast<quint32>(e.data.size())); put32(out, e.usize);
        put16(out, static_cast<quint16>(n.size())); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0); put32(out, 0); put32(out, e.offset);
        out.append(n);
    }
    const quint32 cdSize = static_cast<quint32>(out.size()) - cdStart;
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0);
    put16(out, static_cast<quint16>(entries_.size())); put16(out, static_cast<quint16>(entries_.size()));
    put32(out, cdSize); put32(out, cdStart); put16(out, 0);
    return out;
}

bool ZipReader::open(const QByteArray& bytes, QString* error, ZipLimits limits) {
    bytes_ = bytes; limits_ = limits; entries_.clear();
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    const int n = bytes_.size();
    if (n < 22) return fail(QStringLiteral("Файл слишком короткий — это не архив PSI"));
    int eocd = -1;
    for (int i = n - 22; i >= std::max(0, n - 22 - 65535); --i)
        if (get32(bytes_.constData() + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) return fail(QStringLiteral("Не найден конец архива — файл повреждён"));
    const char* p = bytes_.constData() + eocd;
    quint16 count = get16(p + 10);
    quint32 cdSize = get32(p + 12), cdStart = get32(p + 16);
    if (count > limits_.maxEntries) return fail(QStringLiteral("Слишком много частей в архиве"));
    if (static_cast<qint64>(cdStart) + cdSize > n) return fail(QStringLiteral("Повреждён каталог архива"));
    qint64 total = 0;
    quint32 pos = cdStart;
    for (quint16 i = 0; i < count; ++i) {
        if (pos + 46 > static_cast<quint32>(n)) return fail(QStringLiteral("Повреждён каталог архива"));
        const char* h = bytes_.constData() + pos;
        if (get32(h) != 0x02014b50) return fail(QStringLiteral("Повреждена запись каталога"));
        Entry e;
        e.method = get16(h + 10); e.crc = get32(h + 16); e.csize = get32(h + 20); e.usize = get32(h + 24);
        quint16 nlen = get16(h + 28), xlen = get16(h + 30), clen = get16(h + 32);
        e.offset = get32(h + 42);
        if (pos + 46 + nlen > static_cast<quint32>(n)) return fail(QStringLiteral("Повреждена запись каталога"));
        QString name = QString::fromUtf8(h + 46, nlen);
        if (e.usize > limits_.maxPart) return fail(QStringLiteral("Часть «%1» слишком велика").arg(name));
        total += e.usize;
        if (total > limits_.maxTotal) return fail(QStringLiteral("Архив слишком велик после распаковки"));
        if (e.method != 0 && e.method != 8) return fail(QStringLiteral("Неподдерживаемый метод сжатия части «%1»").arg(name));
        entries_[name] = e;
        pos += 46 + nlen + xlen + clen;
    }
    return true;
}

QByteArray ZipReader::read(const QString& name, QString* error) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) { if (error) *error = QStringLiteral("В архиве нет части «%1»").arg(name); return {}; }
    const Entry& e = it.value();
    const int n = bytes_.size();
    if (e.offset + 30 > static_cast<quint32>(n)) { if (error) *error = QStringLiteral("Повреждена часть «%1»").arg(name); return {}; }
    const char* h = bytes_.constData() + e.offset;
    if (get32(h) != 0x04034b50) { if (error) *error = QStringLiteral("Повреждён заголовок части «%1»").arg(name); return {}; }
    quint16 nlen = get16(h + 26), xlen = get16(h + 28);
    quint64 dataStart = static_cast<quint64>(e.offset) + 30 + nlen + xlen;
    if (dataStart + e.csize > static_cast<quint64>(n)) { if (error) *error = QStringLiteral("Повреждены данные части «%1»").arg(name); return {}; }
    const char* data = bytes_.constData() + dataStart;
    QByteArray out;
    if (e.method == 0) out = QByteArray(data, static_cast<int>(e.csize));
    else if (!rawInflate(data, e.csize, e.usize, &out)) { if (error) *error = QStringLiteral("Не удалось распаковать часть «%1»").arg(name); return {}; }
    quint32 crc = static_cast<quint32>(crc32(0L, reinterpret_cast<const Bytef*>(out.constData()), static_cast<uInt>(out.size())));
    if (crc != e.crc) { if (error) *error = QStringLiteral("Контрольная сумма части «%1» не совпала").arg(name); return {}; }
    return out;
}

} // namespace psi
