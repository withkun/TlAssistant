#ifndef __INC_NATSORT_H
#define __INC_NATSORT_H

#include <vector>
#include <QStringList>


class natsort {
public:
    template <typename T>
    static std::vector<T> os_sorted(std::vector<T> &seq, bool reverse = false);

    static QStringList os_sorted(QStringList &images, bool reverse = false);
};
#endif //__INC_NATSORT_H