/**************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
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

#include "performinstallationform.h"

#include "progresscoordinator.h"
#include "globals.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QImageReader>
#include <QScrollArea>

#include <QtCore/QTimer>

#ifdef Q_OS_WIN
# include <QWinTaskbarButton>
# include <QWinTaskbarProgress>
#endif

using namespace QInstaller;

// -- PerformInstallationForm

/*!
    \class QInstaller::PerformInstallationForm
    \inmodule QtInstallerFramework
    \brief The PerformInstallationForm class shows progress information about
     the installation state.

     A progress bar indicates the progress of the installation, update, or
     uninstallation.

     The page contains a button for showing or hiding detailed information
     about the progress in an \e {details browser}. The text on the button
     changes depending on whether the details browser is currently shown or
     hidden.
*/

/*!
    \fn QInstaller::PerformInstallationForm::showDetailsChanged()

    This signal is emitted when the end users select the details button to show
    or hide progress details.
*/

/*!
    Constructs the perform installation UI with \a parent as parent.
*/
PerformInstallationForm::PerformInstallationForm(bool bInstaller, QObject *parent)
    : QObject(parent)
    , m_worning(nullptr)
    , m_progressBar(nullptr)
    , m_progressLabel(nullptr)
    , m_downloadStatus(nullptr)
    , m_productImagesLabel(nullptr)
    , m_updateTimer(nullptr)
    , m_percentageLabel(nullptr)
    , bInstaller_(bInstaller)
{
#ifdef Q_OS_WIN
    if (QSysInfo::windowsVersion() >= QSysInfo::WV_WINDOWS7) {
        m_taskButton = new QWinTaskbarButton(this);
        m_taskButton->progress()->setVisible(true);
    } else {
        m_taskButton = nullptr;
    }
#endif
}

/*!
    Sets up the perform installation UI specified by \a widget.
*/
void PerformInstallationForm::setupUi(QWidget *widget)
{
    if (bInstaller_) {
        initInstallUi(widget);
    }
    else {
        initUnInstallUi(widget);
    }
}

/*!
    Updates the progress of the installation on the progress bar.
*/
void PerformInstallationForm::updateProgress()
{
    QInstaller::ProgressCoordinator *progressCoordninator = QInstaller::ProgressCoordinator::instance();
    int progressPercentage = progressCoordninator->progressInPercentage();

    if (m_progressBar) {
        if (!bInstaller_)
        {
            progressPercentage = m_progressBar->value() + 5 > 100 ? 100 : m_progressBar->value() + 5;
        }
        m_progressBar->setValue(progressPercentage);
    }
#ifdef Q_OS_WIN
    if (m_taskButton) {
        if (!m_taskButton->window() && QApplication::activeWindow())
            m_taskButton->setWindow(QApplication::activeWindow()->windowHandle());
        m_taskButton->progress()->setValue(progressPercentage);
    }
#endif
    if (m_progressLabel) {
        m_progressLabel->setText(m_message);
    }
    
    if (m_percentageLabel) {
        m_percentageLabel->setText(QString::fromLatin1("%1%").arg(progressPercentage));
    }
}

/*!
    Starts the update progress timer.
*/
void PerformInstallationForm::startUpdateProgress()
{
    m_updateTimer->start();
    updateProgress();
}

/*!
    Stops the update progress timer.
*/
void PerformInstallationForm::stopUpdateProgress()
{
    m_updateTimer->stop();
    updateProgress();
}

void PerformInstallationForm::setMessage(const QString &msg)
{
    m_message = msg;
}

/*!
    Changes the label text according to the changes in the download status
    specified by \a status.
*/
void PerformInstallationForm::onDownloadStatusChanged(const QString &status)
{
    if (m_downloadStatus) {
        m_downloadStatus->setText(status);
    }
}

/*!
    Sets currently shown form image specified by \a fileName.
*/
void PerformInstallationForm::setImageFromFileName(const QString &fileName)
{
    if (!QFile::exists(fileName)) {
        qCWarning(QInstaller::lcDeveloperBuild) << "Image file does not exist:" << fileName;
        return;
    }
    QImageReader reader(fileName);
    QPixmap pixmap = QPixmap::fromImageReader(&reader);
    if (!pixmap.isNull()) {
        m_productImagesLabel->setPixmap(pixmap);
    } else {
        qCWarning(QInstaller::lcDeveloperBuild) <<
            QString::fromLatin1("Failed to load image '%1' : %2.").arg(fileName, reader.errorString());
    }
}

void QInstaller::PerformInstallationForm::initInstallUi(QWidget* widget)
{
    QVBoxLayout* topLayout = new QVBoxLayout(widget);
    topLayout->setContentsMargins(24, 0, 24, 0);

    QHBoxLayout* label_layout = new QHBoxLayout;
    label_layout->setContentsMargins(0, 0, 0, 0);
    {
        m_progressLabel = new QLabel(widget);
        m_progressLabel->setObjectName(QLatin1String("ProgressLabel"));
        m_progressLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        m_worning = new PesWorningLabel(widget);
        QIcon icon(QLatin1String(":/description.png"));
        m_worning->setPixmap(icon.pixmap(18, 18));
        m_worning->setFixedSize(18, 18);

        tool_tips_ = new PesInstallationFormToolTip();
        tool_tips_->setMessage(tr("Interrupting installation process may cause PES to work unexpectedly"));
        connect(m_worning, &PesWorningLabel::showWorning, this, &PerformInstallationForm::showToolTip);
        connect(m_worning, &PesWorningLabel::hideWorning, this, &PerformInstallationForm::hideTooltip);

        m_percentageLabel = new QLabel(widget);
        m_percentageLabel->setObjectName(QLatin1String("percentageLabel"));

        label_layout->addWidget(m_progressLabel, 0, Qt::AlignLeft);
        label_layout->addWidget(m_worning, 0, Qt::AlignLeft);
        label_layout->addWidget(m_percentageLabel, 0, Qt::AlignRight);
    }

    topLayout->addLayout(label_layout);
    topLayout->addSpacing(11);

    QHBoxLayout* progressLayout = new QHBoxLayout;
    progressLayout->setContentsMargins(0, 0, 0, 0);
    {
        m_progressBar = new QProgressBar(widget);
        m_progressBar->setFixedHeight(6);
        m_progressBar->setRange(1, 100);
        m_progressBar->setObjectName(QLatin1String("ProgressBar"));

        progressLayout->addWidget(m_progressBar);
    }
    topLayout->addLayout(progressLayout);
    topLayout->addSpacing(44);
    topLayout->addStretch();

    m_updateTimer = new QTimer(widget);
    connect(m_updateTimer, &QTimer::timeout,
        this, &PerformInstallationForm::updateProgress); //updateProgress includes label
    m_updateTimer->setInterval(30);

    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
}

void QInstaller::PerformInstallationForm::initUnInstallUi(QWidget* widget)
{
    QVBoxLayout* topLayout = new QVBoxLayout(widget);
    topLayout->setContentsMargins(24, 0, 24, 0);
    topLayout->setObjectName(QLatin1String("TopLayout"));

    QHBoxLayout* label_layout = new QHBoxLayout;
    label_layout->setContentsMargins(0, 20, 0, 0);
    {
        m_progressLabel = new QLabel(widget);
        m_progressLabel->setFixedHeight(24);
        m_progressLabel->setObjectName(QLatin1String("UnInstallProgressLabel"));
        m_progressLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        label_layout->addWidget(m_progressLabel, 0, Qt::AlignLeft);
        label_layout->addStretch();
    }

    m_progressBar = new QProgressBar(widget);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setRange(1, 100);
    m_progressBar->setObjectName(QLatin1String("ProgressBar"));

    topLayout->addLayout(label_layout);
    topLayout->addSpacing(20);
    topLayout->addWidget(m_progressBar);
    topLayout->addSpacing(32);

    m_updateTimer = new QTimer(widget);
    connect(m_updateTimer, &QTimer::timeout,
        this, &PerformInstallationForm::updateProgress); //updateProgress includes label
    m_updateTimer->setInterval(30);

    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
}
