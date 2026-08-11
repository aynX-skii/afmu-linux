#include "JsonFile.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QSaveFile>

namespace afmu {

JsonLoadResult loadJson(const QString &path, JsonShape shape)
{
    JsonLoadResult r;
    r.existed = QFileInfo::exists(path);
    if (!r.existed)
        return r;

    const QString who = QFileInfo(path).fileName();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.unreadable = true;
        r.error = QStringLiteral("%1 打不开：%2").arg(who, f.errorString());
    } else {
        const QByteArray raw = f.readAll();
        f.close();
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        const bool ok = shape == JsonShape::Object ? doc.isObject() : doc.isArray();
        if (ok) {
            r.doc = doc;
            return r;
        }
        r.unreadable = true;
        // 语法没问题但顶层类型不对时 err 是 NoError，报「第 0 字节 无错误」只会误导，
        // 所以这两种情况分开说。
        r.error = err.error == QJsonParseError::NoError
                      ? QStringLiteral("%1 的内容不是预期的结构").arg(who)
                      : QStringLiteral("%1 解析失败（第 %2 字节）：%3")
                            .arg(who)
                            .arg(err.offset)
                            .arg(err.errorString());
    }

    // 留底再继续。用默认值跑起来是对的 —— 总比打不开好 —— 但用户得有机会把内容抠回来。
    const QString backup = path + QStringLiteral(".broken-")
        + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    if (QFile::rename(path, backup)) {
        r.error += QStringLiteral("；原文件已保留为 %1").arg(backup);
    } else {
        r.error += QStringLiteral("；而且备份也失败了，已放弃写入以免覆盖它");
        r.readOnlyFallback = true;
    }
    return r;
}

bool saveJson(const QString &path, const QJsonDocument &doc)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(doc.toJson(QJsonDocument::Indented));
    if (!f.commit())
        return false;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

} // namespace afmu
