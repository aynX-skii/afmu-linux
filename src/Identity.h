#pragma once

#include <QByteArray>
#include <QString>

/**
 * 本机的长期身份：一对 EC P-256 密钥和一张自签证书（PROTOCOL-v2-DRAFT.md §3）。
 *
 * v2 的核心变化是「设备身份 = 一对长期密钥」，token 退居次要。这个类只负责
 * **产出并持久化那对密钥**，以及算出用来钉扎的指纹；TLS 怎么用它是后面的事。
 *
 * 之所以现在就能单独做完并验证：指纹的定义是两端唯一必须逐字节一致的东西，
 * 而它出错的症状是「证书明明对却一直不匹配」—— 等握手层调通再发现，极难查。
 * 所以先把它做对、交叉验证过，再往上盖 TLS。
 *
 * 私钥落在 `$XDG_CONFIG_HOME/afmu/identity.pem`，权限 0600。
 * **绝不放进 config.json** —— 那个文件会被看、被贴、被同步。
 */
namespace afmu {

class Identity
{
public:
    /**
     * 加载已有身份；没有就生成一对并写盘。返回是否可用。
     *
     * 生成是一次性的：换了密钥就等于换了设备，所有已配对关系全部作废。
     * 所以只在文件确实不存在时生成，读取失败时**报错而不是重新生成** ——
     * 静默换一把新钥匙，表现是「所有设备突然都连不上了」，没人查得出原因。
     */
    bool ensure(const QString &path);

    bool isValid() const { return !m_certDer.isEmpty(); }

    /** 证书文件路径。 */
    QString path() const { return m_path; }

    /** 出错原因，给日志和界面用。 */
    QString lastError() const { return m_error; }

    /**
     * `SHA-256( DER(SubjectPublicKeyInfo) )`，32 字节。
     *
     * **钉的是 SPKI 而不是整张证书**：这样将来证书到期要重签、或者要改 subject，
     * 只要公钥不变，所有已配对关系就继续有效。钉整张证书的话每次重签都要重新配对。
     *
     * 注意是 SPKI 那一层封装 —— 不是裸公钥点，也不是整张证书。
     * 这一点两端必须完全一致，做完请按 §12 的第 1 步交叉验证。
     */
    QByteArray fingerprint() const { return m_fingerprint; }

    /** 指纹的 base32 形式，无分隔。二维码和配对表里存这个。 */
    QString fingerprintBase32() const;

    /** 给用户看的形式：base32，每 5 个一组用空格分开。 */
    QString fingerprintDisplay() const;

    /** 证书的 DER，握手时要用。 */
    QByteArray certificateDer() const { return m_certDer; }

    /** 私钥的 PEM，交给 QSslKey 用。**不要写进日志。** */
    QByteArray privateKeyPem() const { return m_keyPem; }

    /** 把任意 32 字节指纹格式化成展示形式，配对表里的对端指纹也用它。 */
    static QString toBase32(const QByteArray &raw);
    static QString group(const QString &base32);
    /** 展示形式转回原始字节；不合法返回空。 */
    static QByteArray fromBase32(const QString &text);

private:
    bool load(const QString &path);
    bool generate(const QString &path);
    bool adopt(const QByteArray &keyPem, const QByteArray &certDer);

    QString m_path;
    QString m_error;
    QByteArray m_keyPem;
    QByteArray m_certDer;
    QByteArray m_fingerprint;
};

} // namespace afmu
