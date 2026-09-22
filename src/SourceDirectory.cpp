#include "SourceDirectory.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTextStream>

namespace {

struct RequiredFile {
    const char *name;
    qint64 bytes;
    const char *knownSha256;
};

constexpr RequiredFile kRequiredFiles[] = {
    {"NEWGAME.DAT", 70677, "da0e78e42fff86935a137d0b9b25296a262b3e4fd8fef6c965ee181cea77510f"},
    {"MAP.PUT", 9072, "7cc1be5a31a22941449a0c8cfa4ab02864452ed4cb923e5023f1336470ad638a"},
    {"SHINARIO.CIM", 7595, "9bef680aa42239275f3a073072a5f3e8f3b712c253496946a646dd8c6291bf20"},
    {"COLOR.CIM", 2568, "ac8728961453e4f91a5c7f78aabed30205752d3cc7c1dbbe9453de228260c7ba"},
    {"MAIN.EXE", 338277, "0b6795578e8c968c05f04cac1b91d71249833400d843594a1faaa121819ff9a2"},
};

QString sha256ForFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray data = file.read(1024 * 1024);
        if (data.isEmpty() && file.error() != QFile::NoError) {
            if (error) *error = file.errorString();
            return {};
        }
        hash.addData(data);
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

QString SourceDirectoryReport::toText() const
{
    QString text;
    QTextStream out(&text);
    out << "Original data directory\n"
        << "  " << rootPath << "\n\n";

    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const SourceFileStatus &file = it.value();
        out << file.canonicalName << ": ";
        if (!file.present) {
            out << "MISSING\n";
            continue;
        }
        out << file.actualBytes << " bytes";
        if (file.expectedBytes >= 0) {
            out << " (expected " << file.expectedBytes << ")";
        }
        out << "\n  " << file.resolvedPath << "\n";
        if (!file.sha256.isEmpty()) out << "  SHA-256: " << file.sha256 << "\n";
    }

    if (!warnings.isEmpty()) {
        out << "\nWarnings:\n";
        for (const QString &warning : warnings) out << "- " << warning << "\n";
    }
    if (!errors.isEmpty()) {
        out << "\nErrors:\n";
        for (const QString &error : errors) out << "- " << error << "\n";
    }
    out << "\nProfile: " << (profileId.isEmpty() ? "unsupported" : profileId) << "\n"
        << "Result: " << (valid ? "VALID" : "INVALID") << "\n";
    return text;
}

SourceDirectoryReport SourceDirectory::validate(const QString &directoryPath)
{
    SourceDirectoryReport report;
    const QFileInfo rootInfo(directoryPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        report.errors << "The selected path is not a readable directory.";
        return report;
    }

    report.rootPath = rootInfo.canonicalFilePath();
    if (report.rootPath.isEmpty()) report.rootPath = rootInfo.absoluteFilePath();

    QDir root(report.rootPath);
    const QFileInfoList entries = root.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    QMap<QString, QList<QFileInfo>> byFoldedName;
    for (const QFileInfo &entry : entries) {
        byFoldedName[entry.fileName().toUpper()].append(entry);
    }

    bool allKnownHashesMatch = true;
    for (const RequiredFile &required : kRequiredFiles) {
        SourceFileStatus status;
        status.canonicalName = QString::fromLatin1(required.name);
        status.expectedBytes = required.bytes;

        const QList<QFileInfo> matches = byFoldedName.value(status.canonicalName.toUpper());
        if (matches.isEmpty()) {
            report.errors << QStringLiteral("Required file missing: %1").arg(status.canonicalName);
            report.files.insert(status.canonicalName, status);
            allKnownHashesMatch = false;
            continue;
        }
        if (matches.size() != 1) {
            report.errors << QStringLiteral("Ambiguous case-insensitive file name for %1.")
                                 .arg(status.canonicalName);
            report.files.insert(status.canonicalName, status);
            allKnownHashesMatch = false;
            continue;
        }

        const QFileInfo &match = matches.constFirst();
        status.present = true;
        status.resolvedPath = match.canonicalFilePath();
        if (status.resolvedPath.isEmpty()) status.resolvedPath = match.absoluteFilePath();
        status.actualBytes = match.size();
        status.sizeMatches = status.actualBytes == status.expectedBytes;
        if (!status.sizeMatches) {
            report.errors << QStringLiteral("Unexpected size for %1: got %2, expected %3 bytes.")
                                 .arg(status.canonicalName)
                                 .arg(status.actualBytes)
                                 .arg(status.expectedBytes);
            allKnownHashesMatch = false;
        }

        QString hashError;
        status.sha256 = sha256ForFile(status.resolvedPath, &hashError);
        if (status.sha256.isEmpty()) {
            report.errors << QStringLiteral("Cannot hash %1: %2")
                                 .arg(status.canonicalName, hashError);
            allKnownHashesMatch = false;
        } else if (required.knownSha256[0] != '\0'
                   && status.sha256 != QString::fromLatin1(required.knownSha256)) {
            report.warnings << QStringLiteral("%1 has an unknown hash for the baseline English profile.")
                                   .arg(status.canonicalName);
            allKnownHashesMatch = false;
        }

        report.files.insert(status.canonicalName, status);
    }

    const SourceFileStatus newgame = report.files.value(QStringLiteral("NEWGAME.DAT"));
    if (newgame.present && newgame.sizeMatches) {
        QFile file(newgame.resolvedPath);
        if (!file.open(QIODevice::ReadOnly)) {
            report.errors << QStringLiteral("Cannot inspect NEWGAME.DAT header: %1").arg(file.errorString());
        } else {
            const QByteArray header = file.read(4);
            if (header.size() != 4
                || static_cast<unsigned char>(header[0]) != 0xA4
                || static_cast<unsigned char>(header[1]) != 0x05
                || static_cast<unsigned char>(header[2]) != 0x52
                || static_cast<unsigned char>(header[3]) != 0x15) {
                report.errors << "NEWGAME.DAT header must contain descriptor base 0x05A4 and chunk base 0x1552.";
            }
        }
    }

    if (report.errors.isEmpty() && allKnownHashesMatch) {
        report.profileId = QStringLiteral("uw1-eng-dos-baseline");
        report.valid = true;
    } else if (report.errors.isEmpty()) {
        report.profileId = QStringLiteral("structurally-valid-unprofiled");
        report.warnings << "The directory is structurally valid but not a fully recognized source profile; generation remains disabled.";
    }
    return report;
}
