#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QPointer>
#include <QTimer>
#include <QTranslator>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include "wtdata.h"
#include "wtapi.h"
#include "performanceanalysis.h"
#include "editionpicker.h"
#include "leaderboardoverview.h"
#include "listtransition.h"
#include "notificationcenter.h"
#include "appstyle.h"
#include "switchbutton.h"
#include "startmenushortcut.h"
#include "updater.h"
#include <QTemporaryDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QScrollBar>
#include <QListWidget>
#include <QCryptographicHash>
#include <QProcess>
#include <QThread>
#include <QSaveFile>
#include <QUuid>
#include <QtEndian>
#include "render.h"
#include "appsettings.h"
#include "ui_mainwindow.h"

class PolarTests : public QObject {
    Q_OBJECT
    QTcpServer fixture;
    QStringList requests;
    qint64 tournamentStart=0;
    QByteArray responseFor(const QUrl &url) {
        if(url.path()=="/older_editions") return "<a href='/edition/GLB/62.db'><a href='/edition/GLB/61.db'><a href='/edition/GLB/60.db'>";
        if(url.path().endsWith("/metadata")) {
            const int edition=url.path().split('/')[2].toInt();
            const auto start=edition==0 || edition==62?tournamentStart:tournamentStart-(62-edition)*86400LL*30;
            return QJsonDocument(QJsonObject{{"start_at",start},{"end_at",start+86400}}).toJson();
        }
        if(url.path().endsWith("/get-user")) {
            const int edition=url.path().split('/')[2].toInt();
            return QJsonDocument(QJsonObject{{"points",QJsonArray{0,1000000+(edition-60)*100000}},{"points_wins",QJsonArray{900000,950000}}}).toJson();
        }
        if(url.path().endsWith("/get-top100")) return QJsonDocument(QJsonArray{makePlayer(1,"One",1),makePlayer(2,"Two",2)}).toJson();
        return "{}";
    }
    QJsonObject makePlayer(int id,const QString &name,int rank,double endHour=2) {
        return {{"id",QString::number(id)},{"name",name},{"ranks",QJsonArray{rank,rank,rank,rank,rank}},
            {"hour",QJsonArray{0,0.5,1,1.5,endHour}}, {"points",QJsonArray{200000000-id*1000000,205000000-id*1000000,210000000-id*1000000,215000000-id*1000000,220000000-id*1000000}},
            {"wins",QJsonArray{0,5,10,15,20}},{"wins_pace",QJsonArray{0,10,10,10,10}}};
    }
private slots:
    void initTestCase() {
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        QVERIFY(fixture.listen(QHostAddress::LocalHost));
        tournamentStart=QDateTime::currentSecsSinceEpoch()-7200;
        WtApi::setBaseUrlForTests(QUrl(QString("http://127.0.0.1:%1").arg(fixture.serverPort())));
        connect(&fixture,&QTcpServer::newConnection,this,[this] {
            while(fixture.hasPendingConnections()) {
                auto *socket=fixture.nextPendingConnection();
                connect(socket,&QTcpSocket::readyRead,socket,[this,socket] {
                    const auto bytes=socket->readAll();if(socket->property("answered").toBool() || !bytes.contains("HTTP/")) return;
                    socket->setProperty("answered",true);
                    const auto path=QString::fromUtf8(bytes.split(' ').value(1));requests.append(path);
                    const auto body=responseFor(QUrl(path));
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                    socket->disconnectFromHost();
                });
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
    }
    void currentMetadataWithoutId() {
        const auto m=WtData::metadata(QJsonDocument::fromJson(R"({"start_at":1786689000,"end_at":1786946399})"));
        QVERIFY(!m.isEmpty());QVERIFY(!m.contains("id"));
        QVERIFY(WtData::metadata(QJsonDocument::fromJson(R"({"error":"offline"})")).isEmpty());
        QVERIFY(WtData::metadata(QJsonDocument::fromJson(R"({"start_at":10,"end_at":9})")).isEmpty());
    }
    void legacyMetadataAndRegionCatalog() {
        QCOMPARE(WtData::metadata(QJsonDocument::fromJson(R"({"metadata":{"id":"61","start_at":100,"end_at":200}})")).value("id").toInt(),61);
        QByteArray html=R"(<a href="/edition/GLB/62.db"><a href='/edition/JP/61.db'><a href="/edition/GLB/55.db"><a href="/edition/GLB/62.db">)";
        QCOMPARE(WtData::editions(html,"Glo"),QVector<int>({62,55}));
        QCOMPARE(WtData::editions(html,"JP"),QVector<int>({61}));
        QVERIFY(WtData::editions("<h1>Offline</h1>","Glo").isEmpty());
    }
    void nativeAndStringSamples() {
        const auto doc=QJsonDocument::fromJson(R"([{"hour":[-0.25,0,0.5],"points":[0,3000000000,3010000000],"name":"[a,b,c]"}])");
        auto o=WtData::normalize(doc,"top");WtData::filterNegativeHours(o);
        const auto p=o.value("top").toArray().first().toObject();
        QCOMPARE(WtData::numbers(p.value("points")),QVector<double>({3000000000.,3010000000.}));
        QCOMPARE(p.value("name").toString(),QString("[a,b,c]"));
        QVERIFY(WtData::numbers("[0,null,2]").isEmpty());
    }
    void irregularIntervals() {
        auto s=Performance::summarize({{0,100},{0.2,120},{0.7,120},{1,180}},1);
        QVERIFY(s.valid);QCOMPARE(s.activeHours,0.5);QCOMPARE(s.idleHours,0.5);
        QCOMPARE(s.recentRate,80.0);QCOMPARE(s.activeRate,160.0);QCOMPARE(s.observedRate,80.0);
        QCOMPARE(Performance::project(s,2,0.5),300.0);
    }
    void missingIntervalsAreNotAfk() {
        auto s=Performance::summarize({{0,100},{0.25,100},{2,100},{2.5,150}},2);
        QCOMPARE(s.idleHours,0.25);QCOMPARE(s.unknownHours,1.75);
        QVERIFY(std::isnan(s.recentRate));QVERIFY(std::isnan(s.observedRate));
    }
    void invalidAndResetSamples() {
        QVERIFY(!Performance::summarize({{0,0},{0,10}}).valid);
        QVERIFY(!Performance::summarize({{1,20},{0,10}}).valid);
        QVERIFY(!Performance::summarize({{0,0}}).valid);
        auto s=Performance::summarize({{0,100},{0.5,200},{1,50}});
        QVERIFY(std::isnan(s.recentRate));QVERIFY(std::isnan(s.observedRate));
        QVERIFY(Performance::samples({{"hour","[0,1]"},{"points","[1]"}}).isEmpty());
    }
    void pauseAndCatchDeadline() {
        const auto s=Performance::summarize({{0,3e9},{0.5,3.01e9},{1,3.02e9}});
        QCOMPARE(Performance::project(s,1,2),3.02e9);
        QCOMPARE(Performance::catchHours(100,80,30,3),2.0);
        QVERIFY(std::isnan(Performance::catchHours(100,80,30,1)));
        QVERIFY(std::isnan(Performance::catchHours(100,30,80,3)));
        QVERIFY(std::isnan(Performance::project(s,-1)));
    }
    void historyUsesRealEditionsAndNoCurrentLeakage() {
        QCOMPARE(Performance::historicalProjection({55,57,60},{100,300,600},62),800.0);
        QCOMPARE(Performance::historicalProjection({55,57,60,62},{100,300,600,1},62),800.0);
        QVERIFY(std::isnan(Performance::historicalProjection({62},{1},62)));
    }
    void endpointEscapesIdentifier() {
        QUrlQuery q;q.addQueryItem("identifier","a&rank=1");
        const auto url=WtApi::endpoint(0,"get-user","JP",q);
        QCOMPARE(QUrlQuery(url).queryItemValue("identifier"),QString("a&rank=1"));
        QVERIFY(!QUrlQuery(url).hasQueryItem("rank"));
        QCOMPARE(QUrlQuery(url).queryItemValue("region"),QString("JP"));
        QCOMPARE(url.path(),QString("/api/0/get-user"));
    }
    void asyncCacheCoalescingAndContextLifetime() {
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));int requests=0;
        connect(&server,&QTcpServer::newConnection,&server,[&] {
            while(server.hasPendingConnections()) {
                auto *socket=server.nextPendingConnection();
                connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                    if(socket->property("answered").toBool()) return;
                    socket->readAll();socket->setProperty("answered",true);++requests;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");socket->disconnectFromHost();
                });
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
        WtApi api;QUrl url(QString("http://127.0.0.1:%1/test").arg(server.serverPort()));
        int completed=0;auto callback=[&](const QByteArray &b,const QString &e){QVERIFY(e.isEmpty());QCOMPARE(b,QByteArray("{}"));++completed;};
        auto *context=new QObject;
        api.get(url,this,callback);api.get(url,this,callback);
        api.get(url,context,[&](const auto &,const auto &){QFAIL("Destroyed context called");});delete context;
        QTRY_COMPARE(completed,2);QCOMPARE(requests,1);
        api.get(url,this,callback);QTRY_COMPARE(completed,3);QCOMPARE(requests,1);
        api.get(url,this,callback,0);QTRY_COMPARE(completed,4);QCOMPARE(requests,2);
    }
    void batchConcurrencyAndHttpErrors() {
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        int active=0,peak=0,requests=0;
        connect(&server,&QTcpServer::newConnection,&server,[&] {
            while(server.hasPendingConnections()) {
                auto *socket=server.nextPendingConnection();
                connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                    if(socket->property("answered").toBool()) return;
                    socket->setProperty("answered",true);const auto req=socket->readAll();
                    ++requests;++active;peak=qMax(peak,active);
                    QTimer::singleShot(15,socket,[&,socket,req] {
                        --active;
                        socket->write(req.contains("/fail")?"HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n":"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
                        socket->disconnectFromHost();
                    });
                });
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
        WtApi api;QList<QUrl> urls;
        for(int i=0;i<9;++i) urls.append(QUrl(QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(i)));
        urls.append(QUrl(QString("http://127.0.0.1:%1/fail").arg(server.serverPort())));
        bool done=false;
        api.getMany(urls,this,[&](const QHash<QUrl,WtApi::Result> &results){
            QCOMPARE(results.size(),10);QVERIFY(!results.value(urls.last()).error.isEmpty());done=true;
        });
        QTRY_VERIFY(done);QCOMPARE(requests,10);QVERIFY(peak<=4);QVERIFY(peak>1);
    }
    void pickerKeepsCurrentAndSupportsKeyboardAndDrag() {
        EditionPickerWidget picker;picker.resize(300,60);picker.show();
        int value=-1,changes=0;picker.setOnChanged([&](int v){value=v;++changes;});
        picker.setEditions({0,62,60},0);
        QTest::keyClick(&picker,Qt::Key_Right);QCOMPARE(value,62);
        QTest::keyClick(&picker,Qt::Key_Home);QCOMPARE(value,0);
        QTest::mousePress(&picker,Qt::LeftButton,Qt::NoModifier,{220,30});
        QTest::mouseMove(&picker,{80,30});QTest::mouseRelease(&picker,Qt::LeftButton,Qt::NoModifier,{80,30});QCOMPARE(value,62);
        picker.setEditions({},0);QTest::keyClick(&picker,Qt::Key_Right);QCOMPARE(changes,3);
        if(!qEnvironmentVariable("POLAR_SCREENSHOT_DIR").isEmpty()) picker.grab().save(qEnvironmentVariable("POLAR_SCREENSHOT_DIR")+"/edition-picker.png");
    }
    void chartReplacementThenResize() {
        QMainWindow window;Ui::MainWindow ui;ui.setupUi(&window);window.show();
        for(int i=0;i<20;++i) {
            Render::createLineChartInGraphicsView(&ui,"[0,0.25,0.5]","[0,12,14]","wins_pace");
            window.resize(900+i,700+i);QCoreApplication::processEvents();
        }
        QVERIFY(Render::chartFromView(ui.graphiqueTest));
        // Old event filters must die with each replaced proxy; no dangling resize callback.
        QCOMPARE(ui.graphiqueTest->findChildren<QGraphicsProxyWidget*>().size(),0); // scene owns proxy, not view QObject tree
    }
    void dateForecastExcludesCurrentAndFuture() {
        QCOMPARE(Performance::historicalProjectionByDate({100,200,300},{100,200,300},400),400.0);
        QCOMPARE(Performance::historicalProjectionByDate({100,200,300,400,500},{100,200,300,999999,999999},400),400.0);
        QVERIFY(std::isnan(Performance::historicalProjectionByDate({400},{100},400)));
    }
    void currentGoalOneWithoutEditionIdInFrench() {
        AppSettings::savedLanguage="fr_FR";AppSettings::selectedEdition=0;AppSettings::region="Glo";
        MainWindow window;window.show();
        auto *goal=window.findChild<QLineEdit*>("lineEdit_goal_2");QVERIFY(goal);goal->setText("1");
        auto *result=window.findChild<QLabel*>("label_estimation_rank");QVERIFY(result);
        QTRY_VERIFY_WITH_TIMEOUT(result->text().contains("M"),6000);
        QVERIFY(!result->text().contains("unavailable"));
        QVERIFY(window.findChild<QLabel*>("label_8")->text().contains("actuel"));
        QCOMPARE(window.findChild<EditionPickerWidget*>()->accessibleName(),QString("Édition du TB"));
        QCOMPARE(window.findChild<EditionPickerWidget*>()->tr("Current"),QString("Actuelle"));
        QVERIFY(!requests.contains("/api/62/get-user?rank=1"));
        if(!qEnvironmentVariable("POLAR_SCREENSHOT_DIR").isEmpty()) window.grab().save(qEnvironmentVariable("POLAR_SCREENSHOT_DIR")+"/current-rank-fr.png");
        AppSettings::savedLanguage="en_US";
    }
    void fullTop100OverviewAndStaleProjections() {
        QJsonArray data;for(int i=1;i<=100;++i)data.append(WtData::normalize(QJsonDocument(makePlayer(i,QString("Rival %1").arg(i),i))));
        const auto now=QDateTime::currentSecsSinceEpoch();
        LeaderboardOverview view;view.resize(1280,700);view.show();view.setMetadata(now-7200,now+48*3600);view.setSnapshot(data);
        auto *list=view.findChild<QListWidget*>();QVERIFY(list);QCOMPARE(list->count(),100);
        auto *status=view.findChild<QLabel*>("leaderboardFreshness");QVERIFY(status);
        QVERIFY(status->text().contains("Snapshot"));
        auto *first=list->item(0);auto *row=list->itemWidget(first);QVERIFY(row);
        QStringList values;for(auto *label:row->findChildren<QLabel*>()) values<<label->text();
        QVERIFY(values.join(' ').contains("699")); // 219M + 10M/h over 48 hours, with observed pace scenario.
        view.findChild<QDoubleSpinBox*>()->setValue(1);
        QCOMPARE(list->itemWidget(first),row);
        values.clear();for(auto *label:list->itemWidget(first)->findChildren<QLabel*>())values<<label->text();
        QVERIFY(values.join(' ').contains("689"));
        if(!qEnvironmentVariable("POLAR_SCREENSHOT_DIR").isEmpty()) view.grab().save(qEnvironmentVariable("POLAR_SCREENSHOT_DIR")+"/leaderboard-overview.png");
        view.setMetadata(now-36000,now+3600);QVERIFY(status->text().contains("Stale"));
        values.clear();for(auto *label:list->itemWidget(first)->findChildren<QLabel*>())values<<label->text();
        QVERIFY(values.join(' ').contains("— / —"));
    }
    void rankAnimationPreservesItemsSelectionAndScroll() {
        QListWidget list;list.resize(400,300);list.show();
        const QVector<QJsonObject> first={makePlayer(1,"Same",1),makePlayer(2,"Same",2),makePlayer(3,"Three",3)};
        auto makeRow=[](int i)->QWidget *{return new QLabel(QString::number(i));};
        ListTransition::reconcile(&list,first,makeRow,80);
        auto *one=list.item(0),*two=list.item(1);list.setCurrentItem(two);
        ListTransition::reconcile(&list,{makePlayer(2,"Same",1),makePlayer(1,"Same",2),makePlayer(4,"New",3)},makeRow,80);
        QCOMPARE(list.count(),3);QCOMPARE(list.item(0),two);QCOMPARE(list.item(1),one);QCOMPARE(list.currentItem(),two);
        QVERIFY(list.viewport()->findChild<QWidget*>("rankTransition"));
        QTRY_VERIFY_WITH_TIMEOUT(!list.viewport()->findChild<QWidget*>("rankTransition"),1000);
        QCOMPARE(list.item(2)->data(Qt::UserRole).toJsonObject().value("id").toString(),QString("4"));
    }
    void notificationsActionsRedactionAndExpiry() {
        AppSettings::notificationsEnabled=true;AppSettings::notificationOptions={{"error",QJsonObject{{"enabled",true},{"seconds",2}}}};
        QWidget host;host.resize(700,500);host.show();NotificationCenter center(&host);
        bool accepted=false;center.post("error","Failure https://private.invalid/path",[&]{accepted=true;});
        QTest::qWait(260);QVERIFY(center.isVisible());
        QVERIFY(!center.findChild<QLabel*>("notificationMessage")->text().contains("https://"));
        QTest::mouseClick(center.findChild<QPushButton*>("notificationAccept"),Qt::LeftButton);QVERIFY(accepted);
        QTRY_VERIFY(!center.isVisible());
        center.post("error","Timed notification");QTRY_VERIFY(center.isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!center.isVisible(),3000);
        AppSettings::notificationsEnabled=false;center.post("error","Hidden");QVERIFY(!center.isVisible());
        AppSettings::notificationsEnabled=true;AppSettings::notificationOptions={};
    }
    void liveOptionsThemeAndDisabledNotificationControls() {
        AppSettings::useNewUI=true;AppSettings::transparentControls=false;
        MainWindow window;window.show();bool visited=false;
        QTimer::singleShot(1500,&window,[]{if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();});
        QTimer::singleShot(60,&window,[&] {
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);
            auto *theme=dialog->findChild<QCheckBox*>("checkNewUI");QVERIFY(theme);
            QCOMPARE(dialog->findChild<QDialogButtonBox*>("buttonBox")->standardButtons(),
                QDialogButtonBox::StandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel));
            QVERIFY(dialog->width()<800);
            theme->setChecked(false);QVERIFY(window.styleSheet().isEmpty());QVERIFY(dialog->styleSheet().isEmpty());
            theme->setChecked(true);QVERIFY(!window.styleSheet().isEmpty());QVERIFY(!dialog->styleSheet().isEmpty());
            auto *enabled=dialog->findChild<QCheckBox*>("notificationsEnabled");QVERIFY(enabled);enabled->setChecked(false);
            QVERIFY(!dialog->findChild<QWidget*>("notificationCases")->isEnabled());enabled->setChecked(true);
            auto *errors=dialog->findChild<QCheckBox*>("notify_error");errors->setChecked(false);
            QVERIFY(!dialog->findChild<QSpinBox*>("notify_seconds_error")->isEnabled());
            if(!qEnvironmentVariable("POLAR_SCREENSHOT_DIR").isEmpty()) dialog->grab().save(qEnvironmentVariable("POLAR_SCREENSHOT_DIR")+"/options.png");
            theme->setChecked(false);visited=true;
            dialog->reject();
        });
        QVERIFY(QMetaObject::invokeMethod(&window,"showOptionsDialog",Qt::DirectConnection));QVERIFY(visited);QVERIFY(AppSettings::useNewUI);
    }
    void mouseFocusKeepsKeyboardAccess() {
        AppStyle::installFocusStyle();QWidget host;auto *layout=new QVBoxLayout(&host);
        auto *first=new QCheckBox("First",&host);auto *second=new SwitchButton("Second",&host);
        layout->addWidget(first);layout->addWidget(second);host.show();QTest::qWait(20);
        QTest::mouseClick(first,Qt::LeftButton);QVERIFY(!first->property("keyboardFocus").toBool());
        QTest::keyClick(first,Qt::Key_Tab);QVERIFY(second->hasFocus());QVERIFY(second->property("keyboardFocus").toBool());
        QTest::keyClick(second,Qt::Key_Space);QVERIFY(second->isChecked());
    }
    void releaseVersionsAndDownloadIntegrity() {
        QVERIFY(Updater::isNewerVersion("v1.5.1","v1.5.0"));QVERIFY(Updater::isNewerVersion("v1.10.0","v1.9.9"));
        QVERIFY(!Updater::isNewerVersion("v1.4.4","v1.5.0"));QVERIFY(!Updater::isNewerVersion("v1.5.0","v1.5.0"));
        QVERIFY(!Updater::isNewerVersion("A new release","v1.5.0"));
        QTemporaryDir dir;QFile file(dir.filePath("candidate.exe"));QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray data(128,'\0');data[0]='M';data[1]='Z';qToLittleEndian<quint32>(64,reinterpret_cast<uchar*>(data.data()+0x3c));data.replace(64,4,QByteArray("PE\0\0",4));
        file.write(data);file.close();const auto digest="sha256:"+QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());
        QVERIFY(Updater::validExecutable(file.fileName(),128,digest));QVERIFY(!Updater::validExecutable(file.fileName(),127,digest));
        QVERIFY(!Updater::validExecutable(file.fileName(),128,"sha256:"+QString(64,'0')));
        QVERIFY(file.open(QIODevice::WriteOnly));file.write("<html>Download failed</html>");file.close();QVERIFY(!Updater::validExecutable(file.fileName(),0,{}));
    }

#ifdef Q_OS_WIN
    void startShortcutChecksDirectoryAndRenamedExecutable() {
        QTemporaryDir dir;StartMenuShortcut::setDirectoryForTests(dir.path());
        const auto target=dir.filePath("Polar renamed.exe"),wrong=dir.filePath("Wrong.exe");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),target));
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),wrong));
        QCOMPARE(StartMenuShortcut::inspect(target),StartMenuShortcut::State::Missing);
        QVERIFY(StartMenuShortcut::create(wrong));QCOMPARE(StartMenuShortcut::inspect(target),StartMenuShortcut::State::Invalid);
        QVERIFY(StartMenuShortcut::create(target));QCOMPARE(StartMenuShortcut::inspect(target),StartMenuShortcut::State::Valid);
        QFile::remove(target);QCOMPARE(StartMenuShortcut::inspect(target),StartMenuShortcut::State::Invalid);
        StartMenuShortcut::setDirectoryForTests({});
    }
    void windowsExecutableReplacementAndRollback_data() {
        QTest::addColumn<bool>("invalidImage");QTest::addColumn<bool>("parentExited");
        QTest::newRow("same-name replacement")<<false<<false;
        QTest::newRow("parent already exited")<<false<<true;
        QTest::newRow("restart failure rolls back")<<true<<true;
    }
    void windowsExecutableReplacementAndRollback() {
        QFETCH(bool,invalidImage);QFETCH(bool,parentExited);QTemporaryDir temporary;
        const auto directory=temporary.filePath("Polar ü test's & space");QVERIFY(QDir().mkpath(directory));
        const auto target=QDir(directory).filePath("Renamed Polar.exe");
        const auto prefix=QDir(directory).filePath(".polar-update-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto helper=prefix+".exe",staged=prefix+".download",backup=prefix+".backup",manifest=prefix+".json";
        const auto ready=QDir(directory).filePath("parent-ready"),release=ready+".release",probe=QDir(directory).filePath("restarted");
        const auto executable=QCoreApplication::applicationFilePath();
        QVERIFY(QFile::copy(executable,target));QVERIFY(QFile::copy(executable,helper));
        if(invalidImage) {
            QByteArray data(128,'\0');data[0]='M';data[1]='Z';qToLittleEndian<quint32>(64,reinterpret_cast<uchar*>(data.data()+0x3c));data.replace(64,4,QByteArray("PE\0\0",4));
            QFile file(staged);QVERIFY(file.open(QIODevice::WriteOnly));file.write(data);
        } else QVERIFY(QFile::copy(executable,staged));
        QFile settings(QDir(directory).filePath("polar.json"));QVERIFY(settings.open(QIODevice::WriteOnly));settings.write("{\"identifier\":\"12345\",\"language\":\"fr_FR\"}");settings.close();
        QProcess parent;parent.start(target,{"--polar-update-parent",ready});QVERIFY(parent.waitForStarted());QTRY_VERIFY(QFileInfo::exists(ready));
        QJsonObject data{{"target",target},{"helper",helper},{"staged",staged},{"backup",backup},{"pid",qint64(parent.processId())},
            {"size",QFileInfo(staged).size()},{"arguments",QJsonArray{"--polar-update-probe",probe}},{"shortcut",false}};
        QFile file(manifest);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(data).toJson());file.close();
        const auto allowParentExit=[&] {
            QFile allowExit(release);return allowExit.open(QIODevice::WriteOnly);
        };
        if(parentExited) {QVERIFY(allowParentExit());QVERIFY(parent.waitForFinished(5000));}
        QProcess worker;worker.start(helper,{"--polar-apply-update",manifest});QVERIFY(worker.waitForStarted());
        if(!parentExited) {QTest::qWait(100);QVERIFY(allowParentExit());}
        QVERIFY(worker.waitForFinished(15000));
        QFile helperReport(QDir(directory).filePath("polar-update-result.json"));
        if(helperReport.open(QIODevice::ReadOnly)) qInfo().noquote()<<"Update helper result:"<<helperReport.readAll();
        else qInfo().noquote()<<"Update helper output:"<<worker.readAllStandardOutput()<<worker.readAllStandardError();
        QCOMPARE(worker.exitCode(),invalidImage?1:0);
        if(!parentExited) QVERIFY(parent.waitForFinished(5000));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(probe),5000);
        QVERIFY(QFileInfo::exists(target));QVERIFY(Updater::validExecutable(target,QFileInfo(executable).size(),{}));
        QVERIFY(settings.open(QIODevice::ReadOnly));QVERIFY(settings.readAll().contains("12345"));settings.close();
        QFile result(QDir(directory).filePath("polar-update-result.json"));QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(result.readAll()).object().value("ok").toBool(),!invalidImage);
    }
#endif
    void mainWindowStartupAndResources() {
        MainWindow window;window.show();QTest::qWait(80);
        QVERIFY(window.findChild<QWidget*>("tbEditionPicker"));
        QVERIFY(!QPixmap(":/images/chart.png").isNull());
        QTranslator translator;QVERIFY(translator.load(":/i18n/Polar_fr_FR.qm"));
        if(!qEnvironmentVariable("POLAR_SCREENSHOT_DIR").isEmpty()) window.grab().save(qEnvironmentVariable("POLAR_SCREENSHOT_DIR")+"/main-window.png");
    }

};
int main(int argc,char **argv) {
    if(argc==3) {
        const QString mode=QString::fromLocal8Bit(argv[1]);
        if(mode=="--polar-apply-update" || mode=="--polar-update-probe" || mode=="--polar-update-parent") {
            QCoreApplication app(argc,argv);
            const auto path=app.arguments().value(2);
            if(mode=="--polar-apply-update") return Updater::applyStagedUpdate(path);
            if(mode=="--polar-update-probe") {QFile marker(path);return marker.open(QIODevice::WriteOnly)?0:1;}
            QFile ready(path);if(!ready.open(QIODevice::WriteOnly)) return 1;ready.close();
            QTimer poll;QObject::connect(&poll,&QTimer::timeout,&app,[&]{if(QFileInfo::exists(path+".release")) app.quit();});poll.start(25);
            QTimer::singleShot(10000,&app,&QCoreApplication::quit);return app.exec();
        }
    }
    QApplication app(argc,argv);PolarTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "tst_polar.moc"
