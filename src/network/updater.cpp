#include "updater.h"
#include "appsettings.h"
#include "startmenushortcut.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVersionNumber>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QUuid>
#include <QtEndian>
#include <memory>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#endif

std::string Updater::polar_version="v1.5.0";
bool Updater::isCurrentLatest=true;
QString Updater::latestReleaseName;
namespace {
const QUrl releasesApi("https://api.github.com/repos/darkruss48/Polar/releases/latest");
QString resultPath() {return QDir(QCoreApplication::applicationDirPath()).filePath("polar-update-result.json");}
bool writeJson(const QString &path,const QJsonObject &object) {
    QSaveFile file(path);const auto bytes=QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
}
QVersionNumber version(QString text) {
    text=text.trimmed();if(text.startsWith('v',Qt::CaseInsensitive)) text.remove(0,1);
    if(!QRegularExpression("^[0-9]+(?:\\.[0-9]+){1,3}$").match(text).hasMatch()) return {};
    return QVersionNumber::fromString(text).normalized();
}
bool sameDirectory(const QString &path,const QString &directory) {
    return QFileInfo(path).absolutePath().compare(QDir(directory).absolutePath(),Qt::CaseInsensitive)==0;
}
#ifdef Q_OS_WIN
QString normalizedPath(const QString &path) {
    const QFileInfo file(QDir::fromNativeSeparators(path));
    const auto canonical=file.canonicalFilePath();
    if(!canonical.isEmpty()) return canonical;
    const auto parent=QFileInfo(file.absolutePath()).canonicalFilePath();
    return QDir(parent.isEmpty()?file.absolutePath():parent).filePath(file.fileName());
}
#endif
}
Updater::Updater(QObject *parent) : QObject(parent),manager(this) {
    checkTimer.setInterval(60*60*1000);
    connect(&checkTimer,&QTimer::timeout,this,&Updater::checkForUpdate);checkTimer.start();
}
bool Updater::isNewerVersion(const QString &candidate,const QString &current) {
    const auto next=version(candidate),installed=version(current);
    return !next.isNull() && !installed.isNull() && QVersionNumber::compare(next,installed)>0;
}
void Updater::checkForUpdate() {
    if(checking || downloading) return;
    checking=true;
    QNetworkRequest request(releasesApi);request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::UserAgentHeader,"PolarApp");
    auto *reply=manager.get(request);
    auto *deadline=new QTimer(reply);deadline->setSingleShot(true);deadline->start(20000);
    connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort);
    connect(reply,&QNetworkReply::finished,this,[this,reply] {
        checking=false;const auto bytes=reply->readAll();const auto error=reply->error();reply->deleteLater();
        if(error!=QNetworkReply::NoError) return;
        const auto release=QJsonDocument::fromJson(bytes).object();const auto tag=release.value("tag_name").toString();
        if(version(tag).isNull() || release.value("prerelease").toBool() || release.value("draft").toBool()) return;
        latestReleaseName=tag;isCurrentLatest=!isNewerVersion(tag,QString::fromStdString(polar_version));
        assetUrl.clear();assetDigest.clear();assetSize=0;
        for(const auto &value:release.value("assets").toArray()) {
            const auto asset=value.toObject();const auto name=asset.value("name").toString();
            const QUrl url(asset.value("browser_download_url").toString());
            if(!name.endsWith(".exe",Qt::CaseInsensitive) || url.scheme()!="https" || url.host()!="github.com"
                || !url.path().startsWith("/darkruss48/polar/releases/download/",Qt::CaseInsensitive)) continue;
            assetUrl=url.toString();assetSize=asset.value("size").toVariant().toLongLong();assetDigest=asset.value("digest").toString();break;
        }
        if(!isCurrentLatest) emit updateAvailable(tag,release.value("body").toString(),release.value("html_url").toString());
    });
}
bool Updater::validExecutable(const QString &path,qint64 expectedSize,const QString &digest) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()<64 || (expectedSize>0 && file.size()!=expectedSize)) return false;
    const auto header=file.read(64);if(!header.startsWith("MZ")) return false;
    const quint32 offset=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData()+0x3c));
    if(offset<64 || offset>quint64(file.size()-24) || !file.seek(offset) || file.read(4)!=QByteArray("PE\0\0",4)) return false;
    if(!digest.isEmpty()) {
        if(!digest.startsWith("sha256:") || digest.mid(7).size()!=64 || !file.seek(0)) return false;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if(!hash.addData(&file) || QString::fromLatin1(hash.result().toHex()).compare(digest.mid(7),Qt::CaseInsensitive)!=0) return false;
    }
    return true;
}
void Updater::startDownloadLatestAsset() {
#ifndef Q_OS_WIN
    return;
#else
    if(downloading) return;
    if(assetUrl.isEmpty() || assetSize<=0) {emit downloadFinished({},false,tr("No Windows executable in this release."));return;}
    const auto target=QCoreApplication::applicationFilePath();const auto directory=QFileInfo(target).absolutePath();
    const auto prefix=QDir(directory).filePath(".polar-update-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto staged=prefix+".download",helper=prefix+".exe",manifest=prefix+".json",backup=prefix+".backup";
    auto file=std::make_shared<QSaveFile>(staged);
    if(!file->open(QIODevice::WriteOnly)) {emit downloadFinished({},false,tr("Cannot write in the application directory."));return;}
    downloading=true;downloadTimer.start();emit downloadStarted(assetSize);
    const auto expectedSize=assetSize;const auto digest=assetDigest;
    QNetworkRequest request{QUrl(assetUrl)};request.setTransferTimeout(30000);
    request.setHeader(QNetworkRequest::UserAgentHeader,"PolarApp");
    auto *reply=manager.get(request);
    connect(reply,&QNetworkReply::readyRead,this,[reply,file] {
        const auto bytes=reply->readAll();if(file->write(bytes)!=bytes.size()) reply->abort();
    });
    connect(reply,&QNetworkReply::downloadProgress,this,[this](qint64 received,qint64 total) {
        const double speed=received/(qMax<qint64>(1,downloadTimer.elapsed())/1000.0);
        emit downloadProgress(received,total,speed,speed>0 && total>received?qint64((total-received)/speed):0);
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,file,expectedSize,digest,target,directory,staged,helper,manifest,backup] {
        downloading=false;const auto bytes=reply->readAll();const bool wrote=file->write(bytes)==bytes.size();
        const bool downloaded=reply->error()==QNetworkReply::NoError && wrote;reply->deleteLater();
        if(!downloaded || !file->commit() || !validExecutable(staged,expectedSize,digest)) {
            file->cancelWriting();QFile::remove(staged);emit downloadFinished({},false,tr("Download could not be verified."));return;
        }
        QStringList restartArguments=QCoreApplication::arguments();restartArguments.removeFirst();
        const QJsonObject data{{"target",target},{"staged",staged},{"helper",helper},{"backup",backup},
            {"pid",qint64(QCoreApplication::applicationPid())},{"size",expectedSize},{"digest",digest},
            {"arguments",QJsonArray::fromStringList(restartArguments)},{"shortcut",AppSettings::updateStartShortcutOnUpgrade}};
        AppSettings::save();
        // The helper is an exact copy of this client, so downloaded releases do not need our protocol.
        if(!QFile::copy(target,helper) || !writeJson(manifest,data)
            || !QProcess::startDetached(helper,{"--polar-apply-update",manifest},directory)) {
            QFile::remove(staged);QFile::remove(helper);QFile::remove(manifest);
            emit downloadFinished({},false,tr("Could not start the update helper."));return;
        }
        emit downloadFinished(target,true,{});QCoreApplication::quit();
    });
#endif
}
int Updater::applyStagedUpdate(const QString &manifestPath) {
#ifndef Q_OS_WIN
    Q_UNUSED(manifestPath);return 1;
#else
    QFile manifest(manifestPath);if(!manifest.open(QIODevice::ReadOnly)) return 1;
    const auto data=QJsonDocument::fromJson(manifest.readAll()).object();manifest.close();
    const auto helper=normalizedPath(QCoreApplication::applicationFilePath()),directory=QFileInfo(helper).absolutePath();
    const auto prefix=QFileInfo(helper).absolutePath()+"/"+QFileInfo(helper).completeBaseName();
    const auto target=normalizedPath(data.value("target").toString()),staged=normalizedPath(data.value("staged").toString()),backup=normalizedPath(data.value("backup").toString());
    const auto pid=data.value("pid").toVariant().toULongLong();
    const auto report=[&](bool ok,const QString &error) {
        writeJson(QDir(directory).filePath("polar-update-result.json"),{{"ok",ok},{"error",error},{"helper",helper},{"manifest",manifestPath},{"staged",staged},{"backup",backup}});
    };
    if(!QFileInfo(helper).fileName().startsWith(".polar-update-") || !sameDirectory(target,directory)
        || normalizedPath(manifestPath).compare(prefix+".json",Qt::CaseInsensitive)!=0
        || staged.compare(prefix+".download",Qt::CaseInsensitive)!=0 || backup.compare(prefix+".backup",Qt::CaseInsensitive)!=0
        || QFileInfo(target).suffix().compare("exe",Qt::CaseInsensitive)!=0) {report(false,"manifest");return 1;}
    if(!pid || pid>MAXDWORD || pid==GetCurrentProcessId()) {report(false,"pid");return 1;}
    QStringList arguments;for(const auto &arg:data.value("arguments").toArray()) arguments.append(arg.toString());
    if(!validExecutable(staged,data.value("size").toVariant().toLongLong(),data.value("digest").toString())) {report(false,"verification");return 1;}
    HANDLE parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));
    if(parent) {
        wchar_t name[32768]={};DWORD length=32768;
        const bool matches=QueryFullProcessImageNameW(parent,0,name,&length)
            && normalizedPath(QString::fromWCharArray(name)).compare(target,Qt::CaseInsensitive)==0;
        const DWORD waited=matches?WaitForSingleObject(parent,60000):WAIT_FAILED;CloseHandle(parent);
        if(waited!=WAIT_OBJECT_0) {report(false,"process");return 1;}
    } else if(GetLastError()!=ERROR_INVALID_PARAMETER) {report(false,"process");return 1;}
    const auto nativeTarget=QDir::toNativeSeparators(target),nativeStaged=QDir::toNativeSeparators(staged),nativeBackup=QDir::toNativeSeparators(backup);
    bool replaced=false;
    for(int attempt=0;attempt<20 && !replaced;++attempt) {
        replaced=ReplaceFileW(reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),reinterpret_cast<LPCWSTR>(nativeStaged.utf16()),
            reinterpret_cast<LPCWSTR>(nativeBackup.utf16()),0,nullptr,nullptr);
        if(!replaced) Sleep(250);
    }
    if(!replaced) {
        if(!QFileInfo::exists(target) && QFileInfo::exists(backup))
            MoveFileExW(reinterpret_cast<LPCWSTR>(nativeBackup.utf16()),reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),MOVEFILE_WRITE_THROUGH);
        report(false,"replace");QProcess::startDetached(target,arguments,directory);return 1;
    }
    if(data.value("shortcut").toBool() && StartMenuShortcut::inspect(target)!=StartMenuShortcut::State::Missing) StartMenuShortcut::create(target);
    report(true,{});
    if(!QProcess::startDetached(target,arguments,directory)) {
        const bool restored=ReplaceFileW(reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),reinterpret_cast<LPCWSTR>(nativeBackup.utf16()),nullptr,0,nullptr,nullptr);
        report(false,restored?"restart":"rollback");
        if(restored) QProcess::startDetached(target,arguments,directory);return 1;
    }
    return 0;
#endif
}
QJsonObject Updater::takeUpdateResult() {
    QFile file(resultPath());if(!file.open(QIODevice::ReadOnly)) return {};
    const auto result=QJsonDocument::fromJson(file.readAll()).object();file.close();file.remove();
    const auto directory=QCoreApplication::applicationDirPath();
    const auto cleanup=[result,directory] {
        for(const auto &key:{"helper","manifest","staged","backup"}) {
            const auto path=result.value(key).toString();
            if(!sameDirectory(path,directory) || !QFileInfo(path).fileName().startsWith(".polar-update-")) continue;
            if(QString(key)=="backup" && !result.value("ok").toBool()) continue;
            QFile::remove(path);
        }
    };
    QTimer::singleShot(1500,QCoreApplication::instance(),cleanup);
    return result;
}
