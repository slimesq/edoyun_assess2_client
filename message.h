#ifndef MESSAGE_H
#define MESSAGE_H

#include <QByteArray>
#include <QString>
#include <cstring>

// ---- 消息类型枚举 ----
enum MsgType : qint32 {
    GroupChat     = 1001,    // 群聊消息
    UploadBegin   = 1002,    // 开始上传
    UploadChunk   = 1003,    // 上传数据块
    DownloadBegin = 1004,    // 开始下载
    DownloadEnd   = 1005,    // 下载结束（服务端→客户端）
    DownloadChunk = 1007,    // 下载数据块（服务端→客户端）
    FileStatus    = 1008     // 状态回复（服务端→客户端）
};

// ---- 状态类型枚举 ----
enum StatusType : qint32 {
    Uncompleted = 2000,
    Completed   = 2001,
    StatusError = 2002
};

// ---- 小端序读写辅助函数 ----

// 写入一个字段：[size: 8字节 LE] [data: size字节]
inline void writeField(QByteArray& buf, const QByteArray& data) {
    quint64 len = data.size();
    buf.append(reinterpret_cast<const char*>(&len), 8);
    buf.append(data);
}

// 写入一个 quint64 值（8字节 LE）
inline void writeU64(QByteArray& buf, quint64 val) {
    buf.append(reinterpret_cast<const char*>(&val), 8);
}

// 写入一个 qint32 值（4字节 LE）
inline void writeI32(QByteArray& buf, qint32 val) {
    buf.append(reinterpret_cast<const char*>(&val), 4);
}

// 从 buf 的 offset 位置读取一个字段，返回数据并移动 offset
inline QByteArray readField(const QByteArray& buf, int& offset) {
    if (offset + 8 > buf.size()) return {};
    quint64 len;
    memcpy(&len, buf.constData() + offset, 8);
    offset += 8;
    if (offset + static_cast<qint64>(len) > buf.size()) return {};
    QByteArray data = buf.mid(offset, static_cast<int>(len));
    offset += static_cast<int>(len);
    return data;
}

// 从 buf 的 offset 位置读取 quint64
inline quint64 readU64(const QByteArray& buf, int& offset) {
    quint64 val = 0;
    if (offset + 8 <= buf.size()) {
        memcpy(&val, buf.constData() + offset, 8);
        offset += 8;
    }
    return val;
}

// 从 buf 的 offset 位置读取 qint32
inline qint32 readI32(const QByteArray& buf, int& offset) {
    qint32 val = 0;
    if (offset + 4 <= buf.size()) {
        memcpy(&val, buf.constData() + offset, 4);
        offset += 4;
    }
    return val;
}

// ---- Train: 统一线格式 [length:8][msgType:4][payload:length] ----
class Train {
public:
    MsgType msgType;
    QByteArray payload;

    // 序列化为线格式
    QByteArray toWire() const {
        QByteArray wire;
        quint64 payloadLen = payload.size();
        wire.append(reinterpret_cast<const char*>(&payloadLen), 8);
        qint32 type = msgType;
        wire.append(reinterpret_cast<const char*>(&type), 4);
        wire.append(payload);
        return wire;
    }

    // 从缓冲区解析一帧，返回消耗的字节数，0 表示数据不完整
    static int fromBuffer(const QByteArray& buf, Train& train) {
        if (buf.size() < 12) return 0; // 至少需要 8(length) + 4(msgType)

        quint64 payloadLen;
        memcpy(&payloadLen, buf.constData(), 8);

        qint32 type;
        memcpy(&type, buf.constData() + 8, 4);

        quint64 totalLen = 12 + payloadLen;
        if (static_cast<quint64>(buf.size()) < totalLen) return 0;

        train.msgType = static_cast<MsgType>(type);
        train.payload = buf.mid(12, static_cast<int>(payloadLen));
        return static_cast<int>(totalLen);
    }

    // ---- Payload 构造方法 ----

    // 群聊消息（payload 就是原始聊天内容）
    static Train groupChat(const QByteArray& rawContent) {
        Train t;
        t.msgType = GroupChat;
        t.payload = rawContent;
        return t;
    }

    // 上传开始
    static Train uploadBegin(const QString& username, const QString& fileName,
                             const QString& sha1sum, quint64 fileSize) {
        Train t;
        t.msgType = UploadBegin;
        writeField(t.payload, username.toUtf8());
        writeField(t.payload, fileName.toUtf8());
        writeField(t.payload, sha1sum.toUtf8());
        writeU64(t.payload, fileSize);
        return t;
    }

    // 上传数据块（含 offset）
    static Train uploadChunk(const QString& username, quint64 offset, const QByteArray& data) {
        Train t;
        t.msgType = UploadChunk;
        writeField(t.payload, username.toUtf8());
        writeU64(t.payload, offset);
        writeField(t.payload, data);
        return t;
    }

    // 下载请求
    static Train downloadBegin(const QString& sha1sum) {
        Train t;
        t.msgType = DownloadBegin;
        writeField(t.payload, sha1sum.toUtf8());
        return t;
    }

    // ---- Payload 解析结构 ----

    struct FileStatusMsg {
        MsgType originMsgType;
        StatusType statusType;
    };

    FileStatusMsg parseFileStatus() const {
        FileStatusMsg m;
        int offset = 0;
        m.originMsgType = static_cast<MsgType>(readI32(payload, offset));
        m.statusType = static_cast<StatusType>(readI32(payload, offset));
        return m;
    }

    struct DownloadChunkMsg {
        QString sha1sum;
        quint64 offset;
        QByteArray data;
    };

    DownloadChunkMsg parseDownloadChunk() const {
        DownloadChunkMsg m;
        int off = 0;
        m.sha1sum = QString::fromUtf8(readField(payload, off));
        m.offset = readU64(payload, off);
        m.data = readField(payload, off);
        return m;
    }

    struct DownloadEndMsg {
        QString sha1sum;
    };

    DownloadEndMsg parseDownloadEnd() const {
        DownloadEndMsg m;
        int offset = 0;
        m.sha1sum = QString::fromUtf8(readField(payload, offset));
        return m;
    }
};

#endif // MESSAGE_H
