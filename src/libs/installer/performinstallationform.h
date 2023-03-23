/**************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Installer Framework.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
**************************************************************************/

#ifndef PERFORMINSTALLATIONFORM_H
#define PERFORMINSTALLATIONFORM_H

#include "aspectratiolabel.h"

#include <QObject>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QPainter>

QT_BEGIN_NAMESPACE
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QWidget;
class QWinTaskbarButton;
class QScrollArea;
QT_END_NAMESPACE


namespace QInstaller {
class PesInstallationFormToolTip : public QWidget
{
    Q_OBJECT

public:
    PesInstallationFormToolTip(QWidget* parent = nullptr)
        : QWidget(parent)
        , text_label_(new QLabel)
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowFlags());
        setAttribute(Qt::WA_TranslucentBackground, true);
        setContentsMargins(5,20,5,10);
        QHBoxLayout* pLayout = new QHBoxLayout(this);
        text_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        pLayout->addWidget(text_label_);
    }
    ~PesInstallationFormToolTip() {}

    void setMessage(QString str)
    {
        text_label_->setText(str);
        text_label_->setStyleSheet(QLatin1String("QLabel{font: normal bold; color: #FFFFFF}"));
        adjustSize();
    }

protected:
    void paintEvent(QPaintEvent *event)
    {
        QPainter painter(this);
        QBrush brush(QColor(51,51,51,210));
        painter.setBrush(brush);
        painter.setRenderHint(QPainter::Antialiasing);
        QRect rect(0,10,this->width(),this->height() - 10);
        QPainterPath painterPathPath;
        painterPathPath.addRoundRect(rect, 5, 5);
        static const QPoint points[3] = {
                QPoint(width()/2 - 5, 10),
                QPoint(width()/2, 0),
                QPoint(width()/2 + 5, 10),
        };
        QPolygon polygon;
        polygon = painterPathPath.toFillPolygon().toPolygon();
        painter.drawPolygon(polygon);
        painter.drawPolygon(points,3);
        return QWidget::paintEvent(event);
    }
private:
    QLabel* text_label_;
};

class PesWorningLabel: public QLabel
{
    Q_OBJECT

public:
    explicit PesWorningLabel(QWidget* parent)
        :QLabel(parent)
    {};
    ~PesWorningLabel(){};
protected:
    bool event(QEvent *e)
    {
        if(e->type() == QEvent::Enter)
        {
            emit showWorning();
        }
        else if (e->type() == QEvent::Leave)
        {
            emit hideWorning();
        }
        return QLabel::event(e);
    }
signals:
    void showWorning();
    void hideWorning();
};


class PerformInstallationForm : public QObject
{
    Q_OBJECT

public:
    explicit PerformInstallationForm(bool bInstaller, QObject *parent);

    void setupUi(QWidget *widget);
    void startUpdateProgress();
    void stopUpdateProgress();
    void setMessage(const QString& msg);

signals:
    void showDetailsChanged();

private:
    void initInstallUi(QWidget* widget);
    void initUnInstallUi(QWidget* widget);

public slots:
    void updateProgress();
    void onDownloadStatusChanged(const QString &status);
    void setImageFromFileName(const QString &fileName);
    void showToolTip()
    {
        tool_tips_->move(m_worning->mapToGlobal(QPoint(-tool_tips_->width()/2 + 6, 20)));
        tool_tips_->setVisible(true);
    }
    void hideTooltip()
    {
        tool_tips_->setVisible(false);
    };

private:
    bool bInstaller_;
    PesInstallationFormToolTip* tool_tips_;
    PesWorningLabel  *m_worning;
    QProgressBar *m_progressBar;
    QLabel *m_progressLabel;
    QLabel *m_percentageLabel;
    QLabel *m_downloadStatus;
    AspectRatioLabel *m_productImagesLabel;
    QTimer *m_updateTimer;
    QString m_message;

#ifdef Q_OS_WIN
    QWinTaskbarButton *m_taskButton;
#endif
};

} // namespace QInstaller

#endif // PERFORMINSTALLATIONFORM_H
