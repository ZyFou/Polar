#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QJsonObject>
#include <QString>
#include <string>

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject *parent=nullptr);
    void checkForUpdate();
    void startDownloadLatestAsset();
    static std::string polar_version;
    static bool isCurrentLatest;
    static QString latestReleaseName;
    static bool isNewerVersion(const QString &candidate,const QString &current);
    static bool validExecutable(const QString &path,qint64 expectedSize,const QString &digest);
    static int applyStagedUpdate(const QString &manifestPath);
    static QJsonObject takeUpdateResult();
signals:
    void updateAvailable(const QString &latestVersion,const QString &changelog,const QString &downloadUrl);
    void downloadStarted(qint64 totalBytes);
    void downloadProgress(qint64 receivedBytes,qint64 totalBytes,double speedBytesPerSec,qint64 etaSecs);
    void downloadFinished(const QString &filePath,bool ok,const QString &errorString);
private:
    QNetworkAccessManager manager;
    QTimer checkTimer;
    QElapsedTimer downloadTimer;
    QString assetUrl,assetDigest;
    qint64 assetSize=0;
    bool checking=false,downloading=false;
};
