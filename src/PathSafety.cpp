#include "PathSafety.h"

#include <QDir>
#include <QFileInfo>

namespace afmu {

QString sanitizeFileName(const QString &raw)
{
    QString s = raw;
    // 剥掉路径部分：取最后一个 / 或 \ 之后的内容
    const int slash = qMax(s.lastIndexOf(QLatin1Char('/')), s.lastIndexOf(QLatin1Char('\\')));
    if (slash >= 0)
        s = s.mid(slash + 1);

    QString out;
    out.reserve(s.size());
    for (QChar c : std::as_const(s)) {
        const ushort u = c.unicode();
        if (u < 0x20 || u == 0x7f || c == u'<' || c == u'>' || c == u':' || c == u'"'
            || c == u'|' || c == u'?' || c == u'*' || c == u'/' || c == u'\\') {
            out.append(u'_');
        } else {
            out.append(c);
        }
    }
    out = out.trimmed();
    if (out.size() > 200)
        out.truncate(200);
    // "." 和 ".." 会指向目录本身/父目录，必须挡掉
    if (out.isEmpty() || out == QLatin1String(".") || out == QLatin1String(".."))
        out = QStringLiteral("unnamed");
    return out;
}

// 返回 path 中最长的、真实存在的前缀的 canonical 形式 + 剩余部分（用于 mkdir/upload 目标尚未存在的情况）
static QString canonicalizeLenient(const QString &absPath)
{
    const QString clean = QDir::cleanPath(absPath);
    QFileInfo fi(clean);
    QString canon = fi.canonicalFilePath();
    if (!canon.isEmpty())
        return canon;

    QStringList tail;
    QString cur = clean;
    while (true) {
        const int slash = cur.lastIndexOf(QLatin1Char('/'));
        if (slash < 0)
            return QString();
        const QString name = cur.mid(slash + 1);
        cur = slash == 0 ? QStringLiteral("/") : cur.left(slash);
        tail.prepend(name);
        const QString c = QFileInfo(cur).canonicalFilePath();
        if (!c.isEmpty()) {
            QString joined = c;
            for (const QString &t : std::as_const(tail)) {
                if (t.isEmpty() || t == QLatin1String("."))
                    continue;
                if (t == QLatin1String(".."))
                    return QString(); // 已 cleanPath 过，还有 .. 说明是想穿越，直接拒绝
                if (!joined.endsWith(QLatin1Char('/')))
                    joined += QLatin1Char('/');
                joined += t;
            }
            return joined;
        }
        if (cur == QLatin1String("/"))
            return QString();
    }
}

static bool isUnder(const QString &path, const QString &root)
{
    if (path == root)
        return true;
    QString r = root;
    if (!r.endsWith(QLatin1Char('/')))
        r += QLatin1Char('/');
    return path.startsWith(r);
}

QString resolveUnderRoots(const QString &path, const QStringList &roots)
{
    if (path.isEmpty() || !path.startsWith(QLatin1Char('/')))
        return QString();

    const QString canon = canonicalizeLenient(path);
    if (canon.isEmpty())
        return QString();

    for (const QString &root : roots) {
        const QString canonRoot = QFileInfo(root).canonicalFilePath();
        if (canonRoot.isEmpty())
            continue;
        if (isUnder(canon, canonRoot))
            return canon;
    }
    return QString();
}

QString uniqueTarget(const QString &dir, const QString &name)
{
    QDir d(dir);
    if (!d.exists(name))
        return d.filePath(name);

    QString base = name;
    QString ext;
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        base = name.left(dot);
        ext = name.mid(dot); // 含点
    }
    for (int i = 1; i < 10000; ++i) {
        const QString cand = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(ext);
        if (!d.exists(cand))
            return d.filePath(cand);
    }
    return d.filePath(name);
}

} // namespace afmu
