#pragma once

#include <QJsonDocument>
#include <QString>

/**
 * 磁盘上的 JSON 文件，按「读不出来就绝不覆盖」的规矩读写。
 *
 * 这条规矩是 config.json 上的教训（原委见 Config::load 里的长注释）：以前
 * 「文件不存在」和「文件在但读不出来」落在同一条路径上 —— 拿默认值填满再存回去 ——
 * 于是一次写了一半的文件、一次磁盘满、或者手改配置时打错一个逗号，
 * 都能让 token 和共享目录当场消失，而且没有任何提示。
 *
 * peers.json 要存的是配对关系，丢了的后果一模一样：所有设备要重新配对，
 * 用户完全不知道为什么。所以把这套判断提出来共用 ——
 * 复制一份过去的话，迟早会漏掉其中一个分支，而漏掉的那次没人会当场发现。
 */
namespace afmu {

/** 顶层该是什么。形状不对和语法错误同等处理：都是「读不出来」，都要留底。 */
enum class JsonShape {
    Object,
    Array,
};

struct JsonLoadResult
{
    QJsonDocument doc;

    /** 文件本来就在。区别于「不存在」—— 那是全新安装，不是出事了。 */
    bool existed = false;

    /** 在，但读不出来。此时 doc 为空，调用方该用默认值跑起来，但必须告诉用户。 */
    bool unreadable = false;

    /** 给用户看的原因，含原文件被留到哪儿了。unreadable 为假时为空。 */
    QString error;

    /**
     * 连备份都失败了：原文件还在原地，里面可能有能救回来的东西。
     *
     * 调用方必须据此**停掉后续所有写入**，否则第一次 save() 就把它盖掉，
     * 而这正是留底想避免的事。
     */
    bool readOnlyFallback = false;
};

/** 读。不存在时返回空 doc 且 existed 为假；读不出来时把原文件改名留底。 */
JsonLoadResult loadJson(const QString &path, JsonShape shape);

/** 原子写入（QSaveFile）并设成 0600；目录不存在会创建。 */
bool saveJson(const QString &path, const QJsonDocument &doc);

} // namespace afmu
