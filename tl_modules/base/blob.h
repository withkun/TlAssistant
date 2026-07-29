#ifndef __INC_BLOB_H
#define __INC_BLOB_H

#include <string>


class Attachment {
public:
    std::string url_;
    std::string hash_;
};

class Blob {
public:
    Blob(const std::string &url, const std::string &hash, const Attachment &attach={});

    std::string filename() const;
    std::string path() const;

    void pull();

    std::string url_;
    std::string hash_;
    Attachment  attachments_;
};
#endif //__INC_BLOB_H