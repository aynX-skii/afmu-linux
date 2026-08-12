#include "Identity.h"

#include "ProtocolConstants.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>

namespace afmu {

namespace {

// OpenSSL 的每个 new 都要配一个 free，而中间任何一步都可能失败。
// 用 unique_ptr 托管，省掉一串 goto cleanup —— 那种写法漏一条就是内存泄漏。
template <typename T, void (*F)(T *)>
struct Deleter
{
    void operator()(T *p) const { if (p) F(p); }
};
using KeyPtr = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY, EVP_PKEY_free>>;
using CertPtr = std::unique_ptr<X509, Deleter<X509, X509_free>>;
using BioPtr = std::unique_ptr<BIO, Deleter<BIO, BIO_free_all>>;

QByteArray bioToByteArray(BIO *bio)
{
    char *data = nullptr;
    const long len = BIO_get_mem_data(bio, &data);
    return len > 0 ? QByteArray(data, int(len)) : QByteArray();
}

/**
 * SHA-256( DER(SubjectPublicKeyInfo) )。
 *
 * `i2d_X509_PUBKEY` 出来的正是 SPKI —— 算法标识 + 公钥位串那一整层封装，
 * 和 `openssl x509 -pubkey | openssl pkey -pubin -outform DER` 的输出一致。
 * **不要**用 `i2d_PUBKEY` 之外的东西，也不要去哈希裸的 EC 点：
 * 那会算出一个自洽但和对端对不上的值。
 */
QByteArray spkiFingerprint(X509 *cert)
{
    X509_PUBKEY *spki = X509_get_X509_PUBKEY(cert);
    if (!spki)
        return {};
    unsigned char *der = nullptr;
    const int len = i2d_X509_PUBKEY(spki, &der);
    if (len <= 0 || !der)
        return {};
    const QByteArray raw(reinterpret_cast<const char *>(der), len);
    OPENSSL_free(der);

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    if (EVP_Digest(raw.constData(), size_t(raw.size()), digest, &digestLen, EVP_sha256(), nullptr)
        != 1) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(digest), int(digestLen));
}

} // namespace

// ---------------------------------------------------------------- base32

QString Identity::toBase32(const QByteArray &raw)
{
    static const QByteArray alphabet(kFingerprintAlphabet);
    QString out;
    out.reserve((raw.size() * 8 + 4) / 5);

    quint32 buffer = 0;
    int bits = 0;
    for (const char c : raw) {
        buffer = (buffer << 8) | quint8(c);
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.append(QLatin1Char(alphabet.at((buffer >> bits) & 0x1F)));
        }
    }
    // 32 字节 = 256 bit，不是 5 的整数倍，最后剩 1 bit，左对齐补零成一组
    if (bits > 0)
        out.append(QLatin1Char(alphabet.at((buffer << (5 - bits)) & 0x1F)));
    return out;
}

namespace {

/**
 * 读回指纹时跳过的字符：我们自己打印的那些，加上复制粘贴容易带进来的几种空格。
 *
 * 显式列出来，而不是问平台「这是不是空白」—— 两个平台的答案不一样：
 * `QChar::isSpace()` 认 U+00A0 / U+2007 / U+202F，Kotlin 的 `Char.isWhitespace()` 不认。
 * 于是带不间断空格的指纹在 Linux 这边解得出、在手机上被拒 —— 同一个字符串两种答案，
 * 而它偏偏是决定「你在跟哪台设备说话」的那个值。
 */
bool isFingerprintSeparator(QChar ch)
{
    switch (ch.unicode()) {
    case u' ': case u'\t': case u'\n': case u'\r': case u'-':
    case 0x00A0: case 0x2007: case 0x202F: case 0x3000:
        return true;
    default:
        return false;
    }
}

} // namespace

QByteArray Identity::fromBase32(const QString &text)
{
    static const QByteArray alphabet(kFingerprintAlphabet);
    QByteArray out;
    quint32 buffer = 0;
    int bits = 0;
    for (const QChar ch : text) {
        if (isFingerprintSeparator(ch))
            continue; // 展示形式里的分组空格，以及复制粘贴容易带进来的几种
        const int idx = alphabet.indexOf(ch.toUpper().toLatin1());
        if (idx < 0)
            return {}; // 不在字母表里 —— 整串作废，别猜
        buffer = (buffer << 5) | quint32(idx);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(char((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

QString Identity::group(const QString &base32)
{
    const int size = kFingerprintGroupSize;
    QString out;
    out.reserve(base32.size() + base32.size() / size);
    for (int i = 0; i < base32.size(); ++i) {
        if (i > 0 && i % size == 0)
            out.append(QLatin1Char(' '));
        out.append(base32.at(i));
    }
    return out;
}

QByteArray Identity::fingerprintOf(const QByteArray &certDer)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(certDer.constData());
    CertPtr cert(d2i_X509(nullptr, &p, certDer.size()));
    if (!cert)
        return {};
    return spkiFingerprint(cert.get());
}

QByteArray Identity::privateKeyPem() const
{
    // 文件里是「私钥 PEM + 证书 PEM」拼在一起。QSslKey 只该看到前半段：
    // 把整个文件喂给它，解析成功与否取决于它的实现细节，不该赌。
    const int begin = m_keyPem.indexOf("-----BEGIN ");
    if (begin < 0)
        return {};
    const int keyMark = m_keyPem.indexOf("PRIVATE KEY-----", begin);
    if (keyMark < 0)
        return {};
    const int end = m_keyPem.indexOf("-----END ", keyMark);
    if (end < 0)
        return {};
    const int endLine = m_keyPem.indexOf('\n', end);
    return m_keyPem.mid(begin, (endLine < 0 ? m_keyPem.size() : endLine + 1) - begin);
}

QString Identity::fingerprintBase32() const { return toBase32(m_fingerprint); }
QString Identity::fingerprintDisplay() const { return group(fingerprintBase32()); }

// ---------------------------------------------------------------- 生成/加载

bool Identity::ensure(const QString &path)
{
    m_path = path;
    m_error.clear();

    if (QFileInfo::exists(path))
        return load(path);
    return generate(path);
}

bool Identity::adopt(const QByteArray &keyPem, const QByteArray &certDer)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(certDer.constData());
    CertPtr cert(d2i_X509(nullptr, &p, certDer.size()));
    if (!cert) {
        m_error = QStringLiteral("证书解析失败");
        return false;
    }
    const QByteArray fp = spkiFingerprint(cert.get());
    if (fp.size() != 32) {
        m_error = QStringLiteral("无法从证书算出 SPKI 指纹");
        return false;
    }
    m_keyPem = keyPem;
    m_certDer = certDer;
    m_fingerprint = fp;
    return true;
}

bool Identity::load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        // 读不出来就报错，**绝不重新生成**：静默换一把新钥匙的表现是
        // 「所有已配对设备突然都连不上了」，而且没人查得出原因。
        m_error = QStringLiteral("身份文件打不开：%1").arg(f.errorString());
        return false;
    }
    const QByteArray pem = f.readAll();
    f.close();

    BioPtr bio(BIO_new_mem_buf(pem.constData(), pem.size()));
    if (!bio) {
        m_error = QStringLiteral("内存不足");
        return false;
    }
    KeyPtr key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
    CertPtr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (!key || !cert) {
        m_error = QStringLiteral("身份文件损坏：%1").arg(path);
        return false;
    }

    unsigned char *der = nullptr;
    const int len = i2d_X509(cert.get(), &der);
    if (len <= 0 || !der) {
        m_error = QStringLiteral("证书编码失败");
        return false;
    }
    const QByteArray certDer(reinterpret_cast<const char *>(der), len);
    OPENSSL_free(der);

    return adopt(pem, certDer);
}

bool Identity::generate(const QString &path)
{
    KeyPtr key(EVP_EC_gen(kIdentityCurve));
    if (!key) {
        m_error = QStringLiteral("生成 %1 密钥失败").arg(QLatin1String(kIdentityCurve));
        return false;
    }

    CertPtr cert(X509_new());
    if (!cert) {
        m_error = QStringLiteral("内存不足");
        return false;
    }
    // v3，才能带扩展
    X509_set_version(cert.get(), 2);

    // 序列号随机。自签证书没人查重，但固定值会让两台机器的证书长得一模一样，
    // 某些 TLS 栈会因此困惑。
    unsigned char serial[16];
    if (RAND_bytes(serial, sizeof(serial)) != 1) {
        m_error = QStringLiteral("随机数不可用");
        return false;
    }
    serial[0] &= 0x7F; // 正数
    BIGNUM *bn = BN_bin2bn(serial, sizeof(serial), nullptr);
    BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(cert.get()));
    BN_free(bn);

    X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), 60L * 60 * 24 * kIdentityValidityDays);

    if (X509_set_pubkey(cert.get(), key.get()) != 1) {
        m_error = QStringLiteral("写入公钥失败");
        return false;
    }

    // subject 只是占位。钉的是公钥，不是名字 —— 所以这里放什么都不影响安全性，
    // 放设备名反而会把它印进握手（TLS 1.3 加密证书，但配对表和日志里仍会出现）。
    X509_NAME *name = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("AFMU device"), -1, -1, 0);
    X509_set_issuer_name(cert.get(), name); // 自签

    if (X509_sign(cert.get(), key.get(), EVP_sha256()) == 0) {
        m_error = QStringLiteral("证书自签失败");
        return false;
    }

    // 序列化：私钥 PEM + 证书 PEM 写进同一个文件
    BioPtr bio(BIO_new(BIO_s_mem()));
    if (!bio || PEM_write_bio_PrivateKey(bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1
        || PEM_write_bio_X509(bio.get(), cert.get()) != 1) {
        m_error = QStringLiteral("身份序列化失败");
        return false;
    }
    const QByteArray pem = bioToByteArray(bio.get());

    unsigned char *der = nullptr;
    const int len = i2d_X509(cert.get(), &der);
    if (len <= 0 || !der) {
        m_error = QStringLiteral("证书编码失败");
        return false;
    }
    const QByteArray certDer(reinterpret_cast<const char *>(der), len);
    OPENSSL_free(der);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    // QSaveFile 走临时文件 + rename，临时文件本身是 0600，所以私钥不会有
    // 一个短暂的 0644 窗口。rename 保留 inode 权限，最终文件同样是 0600。
    if (!out.open(QIODevice::WriteOnly)) {
        m_error = QStringLiteral("写不了身份文件：%1").arg(out.errorString());
        return false;
    }
    out.write(pem);
    if (!out.commit()) {
        m_error = QStringLiteral("身份文件保存失败");
        return false;
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);

    return adopt(pem, certDer);
}

} // namespace afmu
