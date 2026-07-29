#ifndef __INC_FILE_WATCHER_H
#define __INC_FILE_WATCHER_H

#include <QFileSystemWatcher>


class TlFileWatcher : public QFileSystemWatcher {
    Q_OBJECT
public:
    TlFileWatcher(const QString &path, QObject *parent = nullptr);
    ~TlFileWatcher() override;

private slots:
    void slotFileChanged(const QString &path);
};
#endif //__INC_FILE_WATCHER_H