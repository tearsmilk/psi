#pragma once
#include <QByteArray>
#include <QMap>
#include <QString>

namespace psi {

// Minimal ZIP container (stored / deflate via zlib), enough for the .psi format. No zip64.
class ZipWriter {
public:
    void add(const QString& name, const QByteArray& data, bool compress = true);
    QByteArray finish() const;
private:
    struct Entry { QString name; QByteArray data; quint32 crc; quint32 usize; quint16 method; quint32 offset; };
    QByteArray body_;
    QList<Entry> entries_;
};

struct ZipLimits { qint64 maxPart = 64ll * 1024 * 1024; qint64 maxTotal = 512ll * 1024 * 1024; int maxEntries = 10000; };

class ZipReader {
public:
    // Parses the central directory; returns false with a Russian message on malformed input or exceeded limits.
    bool open(const QByteArray& bytes, QString* error, ZipLimits limits = {});
    QStringList names() const { return entries_.keys(); }
    bool contains(const QString& name) const { return entries_.contains(name); }
    QByteArray read(const QString& name, QString* error = nullptr) const;
private:
    struct Entry { quint16 method; quint32 crc; quint32 csize; quint32 usize; quint32 offset; };
    QByteArray bytes_;
    QMap<QString, Entry> entries_;
    ZipLimits limits_;
};

} // namespace psi
