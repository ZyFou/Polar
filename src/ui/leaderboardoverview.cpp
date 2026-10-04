#include "leaderboardoverview.h"
#include "listtransition.h"
#include "switchbutton.h"
#include "appsettings.h"
#include "performanceanalysis.h"
#include "wtdata.h"
#include <QListWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
int rankOf(const QJsonObject &player) {const auto ranks=WtData::numbers(player.value("ranks"));return ranks.isEmpty()?0:int(ranks.last());}
QString score(double value) {return std::isfinite(value)?QString::number(value/1e6,'f',1)+"M":QStringLiteral("—");}
QString hours(double value) {return std::isfinite(value)?QString::number(value,'f',1)+" h":QStringLiteral("—");}
}
LeaderboardOverview::LeaderboardOverview(QWidget *parent) : QWidget(parent) {
    setObjectName("leaderboardOverview");
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(12,8,12,8);
    auto *controls=new QHBoxLayout;
    window=new QComboBox(this);window->setObjectName("leaderboardWindow");
    for(int h:{1,2,6}) window->addItem(QString::number(h)+" h",h);window->setCurrentIndex(1);
    pause=new QDoubleSpinBox(this);pause->setObjectName("leaderboardPause");pause->setRange(0,24);pause->setSingleStep(0.25);pause->setSuffix(" h");
    background=new SwitchButton({},this);background->setObjectName("leaderboardPaceBackground");background->setChecked(AppSettings::leaderboardPaceBackground);
    windowLabel=new QLabel(this);pauseLabel=new QLabel(this);
    controls->addWidget(windowLabel);controls->addWidget(window);controls->addSpacing(12);
    controls->addWidget(pauseLabel);controls->addWidget(pause);controls->addStretch();controls->addWidget(background);
    layout->addLayout(controls);
    columns=new QLabel(this);columns->setWordWrap(true);layout->addWidget(columns);
    list=new QListWidget(this);list->setObjectName("overviewPlayers");list->setSpacing(6);list->setUniformItemSizes(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setStyleSheet("QListWidget#overviewPlayers { background:transparent; border:0; padding:0; } QListWidget::item:selected { background:rgba(26,115,232,35); border-radius:6px; }");
    list->viewport()->setAutoFillBackground(false);list->viewport()->setStyleSheet("background:transparent;");layout->addWidget(list,1);
    status=new QLabel(this);status->setObjectName("leaderboardFreshness");layout->addWidget(status);
    connect(list,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){focus=item->data(Qt::UserRole).toJsonObject();refresh();emit playerChosen(focus);});
    connect(window,&QComboBox::currentIndexChanged,this,[this]{refresh();});
    connect(pause,&QDoubleSpinBox::valueChanged,this,[this]{refresh();});
    connect(background,&QCheckBox::toggled,this,[this](bool enabled){AppSettings::leaderboardPaceBackground=enabled;AppSettings::save();update();});
    auto *age=new QTimer(this);connect(age,&QTimer::timeout,this,[this]{if(isVisible()) refresh();});age->start(60000);
    retranslate();refresh();
}
void LeaderboardOverview::retranslate() {
    windowLabel->setText(tr("Scoring window"));pauseLabel->setText(tr("Future pause"));
    background->setText(tr("Pace in background"));background->updateGeometry();
    columns->setText(tr("Top 100 · Select a player to compare gaps and catch-up times. Finish: recent pace / observed pace."));
    pause->setToolTip(tr("Additional future pause for the selected player. Scenarios are not probabilities."));
}
void LeaderboardOverview::changeEvent(QEvent *event) {if(event->type()==QEvent::LanguageChange){retranslate();refresh();}QWidget::changeEvent(event);}
void LeaderboardOverview::setMetadata(qint64 start,qint64 end) {startEpoch=start;endEpoch=end;refresh();}
void LeaderboardOverview::clearSnapshot() {players.clear();focus={};received={};refresh();}
void LeaderboardOverview::setSnapshot(const QJsonArray &data) {
    players.clear();
    for(const auto &v:data) {const auto player=v.toObject();if(rankOf(player)>0 && rankOf(player)<=100) players.append(player);}
    std::sort(players.begin(),players.end(),[](const auto &a,const auto &b){return rankOf(a)<rankOf(b);});
    const auto key=ListTransition::playerKey(focus);bool found=false;
    for(const auto &player:players) if(ListTransition::playerKey(player)==key) {focus=player;found=true;break;}
    if(!found) {
        focus=players.isEmpty()?QJsonObject():players.first();
        for(const auto &player:players) if(ListTransition::playerKey(player)==AppSettings::savedIdentifier) {focus=player;break;}
    }
    received=QDateTime::currentDateTime();refresh();
}
void LeaderboardOverview::selectPlayer(const QJsonObject &player) {focus=player;refresh();}
void LeaderboardOverview::refresh() {
    const double duration=(endEpoch-startEpoch)/3600.0;
    const bool datesValid=startEpoch>0 && duration>0;
    const bool ended=datesValid && endEpoch<=QDateTime::currentSecsSinceEpoch();
    const auto reference=Performance::summarize(Performance::samples(focus),window->currentData().toDouble());
    const double nowHour=(QDateTime::currentSecsSinceEpoch()-startEpoch)/3600.0;
    const bool referenceFresh=datesValid && reference.valid && nowHour-reference.lastHour>=-0.05 && nowHour-reference.lastHour<=1.0;
    ListTransition::reconcile(list,players,[&](int i)->QWidget * {
        const auto &player=players[i];const int rank=rankOf(player);
        const auto s=Performance::summarize(Performance::samples(player),window->currentData().toDouble());
        const bool selected=ListTransition::playerKey(player)==ListTransition::playerKey(focus);
        const bool fresh=datesValid && s.valid && nowHour-s.lastHour>=-0.05 && nowHour-s.lastHour<=1.0;
        const double remaining=datesValid?std::max(0.0,duration-s.lastHour):Performance::unavailable;
        const double plannedPause=selected?pause->value():0;
        const auto points=WtData::numbers(player.value("points"));
        const double current=points.isEmpty()?Performance::unavailable:points.last();
        const double recent=ended?current:(fresh?Performance::project(s,remaining,plannedPause):Performance::unavailable);
        const double observed=ended?current:(fresh?Performance::project(s,remaining,plannedPause,true):Performance::unavailable);
        const bool comparable=referenceFresh && fresh && !ended && std::abs(reference.lastHour-s.lastHour)<=0.05;
        const double gap=comparable?s.points-reference.points:Performance::unavailable;
        const double eta=comparable?Performance::catchHours(std::abs(gap),gap>0?reference.recentRate:s.recentRate,
            gap>0?s.recentRate:reference.recentRate,std::max(0.0,duration-std::max(s.lastHour,reference.lastHour))):Performance::unavailable;
        const auto pace=WtData::numbers(player.value("wins_pace"));
        const QString winsPace=pace.isEmpty()?QStringLiteral("—"):QString::number(pace.last(),'f',1);
        auto *row=new QFrame;row->setObjectName("overviewRow");row->setAttribute(Qt::WA_TransparentForMouseEvents);
        const QString accent=rank==1?"#d4a721":rank==2?"#9aa6b2":rank==3?"#b87c44":selected?"#1a73e8":"#627386";
        const auto base=palette().base().color();
        row->setStyleSheet(QString("QFrame#overviewRow { background:rgba(%1,%2,%3,205); border:1px solid rgba(128,128,128,50); border-left:4px solid %4; border-radius:6px; } QLabel { background:transparent; border:0; }")
            .arg(base.red()).arg(base.green()).arg(base.blue()).arg(accent));
        auto *cells=new QHBoxLayout(row);cells->setContentsMargins(12,6,12,6);cells->setSpacing(12);
        auto cell=[&](const QString &title,const QString &value,int width,const QString &color=QString()) {
            auto *box=new QWidget(row);auto *stack=new QVBoxLayout(box);stack->setContentsMargins(0,0,0,0);stack->setSpacing(2);
            auto *heading=new QLabel(title,box);heading->setStyleSheet("font-size:10px; color:palette(mid);");stack->addWidget(heading);
            auto *label=new QLabel(value,box);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);
            label->setStyleSheet("font-weight:bold;"+(color.isEmpty()?QString():"color:"+color+";"));stack->addWidget(label);
            if(width>0) box->setFixedWidth(width);else box->setMinimumWidth(100);
            cells->addWidget(box,width>0?0:1);
        };
        cell(tr("Rank"),QString("#%1").arg(rank),40,accent);
        cell(tr("Player"),player.value("name").toString(),0);
        cell(tr("Points"),score(current),80);
        cell(tr("Wins/h"),winsPace,60);
        cell(tr("Status"),!s.valid?tr("Unknown"):s.trailingIdleHours>0?tr("Idle %1").arg(hours(s.trailingIdleHours)):tr("Active"),90,s.trailingIdleHours>0?"#b57426":"#279e6a");
        cell(tr("Active / idle"),hours(s.activeHours)+" / "+hours(s.idleHours),105);
        cell(tr("Finish scenarios"),score(recent)+" / "+score(observed),140);
        cell(tr("Gap"),selected?QStringLiteral("—"):score(gap),70,std::isfinite(gap)?(gap>0?"#bd514f":"#279e6a"):QString());
        cell(tr("Catch-up"),selected?QStringLiteral("—"):hours(eta),65);
        row->setToolTip(tr("Known sample coverage: %1%. Missing intervals and score resets are excluded.").arg(int(s.coverage*100)));
        return row;
    },80);
    status->setText(players.isEmpty()?tr("No leaderboard data. Refresh to load the Top 100."):
        ended?tr("Tournament ended · recorded scores"):
        !datesValid?tr("Tournament dates unavailable"):
        !referenceFresh?tr("Stale data · projections unavailable"):
        tr("Snapshot received at %1 · %2 min old").arg(received.toString("HH:mm:ss")).arg(qMax(0,int((nowHour-reference.lastHour)*60))));
    update();
}
void LeaderboardOverview::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);
    if(!background->isChecked() || endEpoch<=startEpoch || startEpoch<=0) return;
    const auto x=WtData::numbers(focus.value("hour")),y=WtData::numbers(focus.value("wins_pace"));
    if(x.size()!=y.size() || x.size()<2) return;
    const double duration=(endEpoch-startEpoch)/3600.0;
    const QRectF area(list->geometry().adjusted(32,32,-24,-36));if(area.width()<=0 || area.height()<=0) return;
    double maxPace=15;for(double value:y) maxPace=std::max(maxPace,value);
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setClipRect(list->geometry());
    QPainterPath path;bool segment=false;
    for(int i=0;i<x.size();++i) {
        if(x[i]<0 || x[i]>duration || y[i]<0) {segment=false;continue;}
        const QPointF point(area.left()+x[i]/duration*area.width(),area.bottom()-y[i]/maxPace*area.height());
        if(!segment || (i>0 && (x[i]-x[i-1]>0.75 || x[i]<=x[i-1]))) path.moveTo(point);else path.lineTo(point);
        segment=true;
    }
    QLinearGradient gradient(area.topLeft(),area.bottomLeft());gradient.setColorAt(0,QColor(26,115,232,160));gradient.setColorAt(1,QColor(26,115,232,15));
    p.setPen(QPen(QBrush(gradient),3));p.drawPath(path);
    p.setPen(QColor(128,128,128,100));p.drawLine(area.bottomLeft(),area.bottomRight());
    p.drawText(QRectF(area.left(),area.bottom()+4,80,22),tr("0 h"));
    p.drawText(QRectF(area.right()-100,area.bottom()+4,100,22),Qt::AlignRight,QString::number(duration,'f',1)+" h");
    p.drawText(QRectF(area.left(),area.top()-25,area.width(),22),tr("%1 · pace (wins/h)").arg(focus.value("name").toString()));
}
