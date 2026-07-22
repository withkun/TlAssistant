#include "blob.h"

#include <Shlobj.h>
#include <filesystem>
#include <QCoreApplication>
#include "spdlog/spdlog.h"


Blob::Blob(const std::string &url, const std::string &hash, const Attachment &attach) {
    url_ = url;
    hash_ = hash;
    attachments_ = attach;
}

std::string Blob::filename() const {
    // 查找路径开始位置（跳过协议和域名部分）
    const auto pos = url_.find_last_of('/');
    if (pos != std::string::npos) {
        return url_.substr(pos+1);
    }
    return url_;
}

std::string Blob::path() const {
    // 先判断应用进程目录下是否存在models目录, 如果存在则从进程目录获取模型.
    const QString appDirPath = QCoreApplication::applicationDirPath();
    std::filesystem::path path{appDirPath.toLocal8Bit().constData()};
    path /= "models";
    if (!std::filesystem::exists(path)) {
        TCHAR szPath[1024]{};
        SHGetSpecialFolderPath(nullptr, szPath, CSIDL_PROFILE, 0);
        path = std::filesystem::path{szPath};
        path /= ".cache/osam/models/blobs";
    }

    if (!attachments_.url_.empty()) {
        std::string safe_hash = hash_;
        if (const auto pos = safe_hash.find("sha256:"); pos != std::string::npos) {
            safe_hash.replace(pos, strlen("sha256:"), "sha256-");
        }
        path /= safe_hash;
    }
    path /= filename();

    SPDLOG_INFO("===> path: {}", path.generic_string());
    return std::filesystem::absolute(path).generic_string();
}

void Blob::pull() {
    // 下载远程文件, 暂不实现.
}