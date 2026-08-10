#pragma once

#include <QString>
#include <QStringList>

// docs/PROTOCOL.md §4.1 / §4.2 / §4.4
namespace afmu {

// 剥掉路径部分、替换非法字符、截断 200 字符，空则 "unnamed"
QString sanitizeFileName(const QString &raw);

// 把用户传入的路径规范化（解析 .. 与符号链接），并检查是否等于某个 root 或位于其下。
// 越界或非法返回空字符串（调用方一律按 404 处理，不泄露真实原因）。
QString resolveUnderRoots(const QString &path, const QStringList &roots);

// overwrite != 1 时的自动改名：a.txt -> "a (1).txt" -> "a (2).txt"，上限 10000
QString uniqueTarget(const QString &dir, const QString &name);

} // namespace afmu
