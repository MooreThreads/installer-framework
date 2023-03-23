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
#include "packagemanagergui.h"

#include "component.h"
#include "componentmodel.h"
#include "errors.h"
#include "fileutils.h"
#include "messageboxhandler.h"
#include "packagemanagercore.h"
#include "progresscoordinator.h"
#include "performinstallationform.h"
#include "settings.h"
#include "utils.h"
#include "scriptengine.h"
#include "productkeycheck.h"
#include "repositorycategory.h"
#include "componentselectionpage_p.h"
#include "loggingutils.h"

#include "sysinfo.h"
#include "globals.h"

#include <QApplication>
#include <QUiLoader>

#include <QtCore/QDir>
#include <QtCore/QPair>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtCore/QSettings>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QStringListModel>
#include <QTextBrowser>
#include <QScrollArea>
#include <QScrollBar>
#include <QPainter>
#include <QTextFrame>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QDesktopWidget>
#include <QScreen>

#include <QXmlStreamReader>

#ifdef Q_OS_WIN
# include <qt_windows.h>
# include <QWinTaskbarButton>
# include <QWinTaskbarProgress>
#endif

using namespace KDUpdater;
using namespace QInstaller;

static const char* kConfigSetupName = "path";
static int kShadowLen = 8;

class DynamicInstallerPage : public PackageManagerPage
{
    Q_OBJECT
    Q_DISABLE_COPY(DynamicInstallerPage)

    Q_PROPERTY(bool final READ isFinal WRITE setFinal)
    Q_PROPERTY(bool commit READ isCommit WRITE setCommit)
    Q_PROPERTY(bool complete READ isComplete WRITE setComplete)

public:
    explicit DynamicInstallerPage(QWidget *widget, PackageManagerCore *core = nullptr)
        : PackageManagerPage(core)
        , m_widget(widget)
    {
        setObjectName(QLatin1String("Dynamic") + widget->objectName());
        setPixmap(QWizard::WatermarkPixmap, QPixmap());

        setColoredSubTitle(QLatin1String(" "));
        setColoredTitle(widget->windowTitle());
        m_widget->setProperty("complete", true);
        m_widget->setProperty("final", false);
        m_widget->setProperty("commit", false);
        widget->installEventFilter(this);

        setLayout(new QVBoxLayout);
        layout()->addWidget(widget);
        layout()->setContentsMargins(0, 0, 0, 0);

        addPageAndProperties(packageManagerCore()->controlScriptEngine());
        addPageAndProperties(packageManagerCore()->componentScriptEngine());
    }

    QWidget *widget() const
    {
        return m_widget;
    }

    bool isComplete() const Q_DECL_OVERRIDE
    {
        return m_widget->property("complete").toBool();
    }

    void setFinal(bool final) {
        if (isFinal() == final)
            return;
        m_widget->setProperty("final", final);
    }
    bool isFinal() const {
        return m_widget->property("final").toBool();
    }

    void setCommit(bool commit) {
        if (isCommit() == commit)
            return;
        m_widget->setProperty("commit", commit);
    }
    bool isCommit() const {
        return m_widget->property("commit").toBool();
    }

    void setComplete(bool complete) {
        if (isComplete() == complete)
            return;
        m_widget->setProperty("complete", complete);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event)
    {
        if (obj == m_widget) {
            switch(event->type()) {
            case QEvent::WindowTitleChange:
                setColoredTitle(m_widget->windowTitle());
                break;

            case QEvent::DynamicPropertyChange:
                emit completeChanged();
                if (m_widget->property("final").toBool() != isFinalPage())
                    setFinalPage(m_widget->property("final").toBool());
                if (m_widget->property("commit").toBool() != isCommitPage())
                    setCommitPage(m_widget->property("commit").toBool());
                break;

            default:
                break;
            }
        }
        return PackageManagerPage::eventFilter(obj, event);
    }

    void addPageAndProperties(ScriptEngine *engine)
    {
        engine->addToGlobalObject(this);
        engine->addToGlobalObject(widget());

        static const QStringList properties = QStringList() << QStringLiteral("final")
            << QStringLiteral("commit") << QStringLiteral("complete");
        foreach (const QString &property, properties) {
            engine->evaluate(QString::fromLatin1(
                "Object.defineProperty(%1, \"%2\", {"
                    "get : function() { return Dynamic%1.%2; },"
                    "set: function(val) { Dynamic%1.%2 = val; }"
                "});"
            ).arg(m_widget->objectName(), property));
        }
    }

private:
    QWidget *const m_widget;
};
Q_DECLARE_METATYPE(DynamicInstallerPage*)


// -- PackageManagerGui::Private

class PackageManagerGui::Private
{
public:
    Private()
        : m_currentId(-1)
        , m_modified(false)
        , m_autoSwitchPage(true)
        , m_showSettingsButton(false)
        , m_silent(false)
    {
        m_wizardButtonTypes.insert(QWizard::BackButton, QLatin1String("QWizard::BackButton"));
        m_wizardButtonTypes.insert(QWizard::NextButton, QLatin1String("QWizard::NextButton"));
        m_wizardButtonTypes.insert(QWizard::CommitButton, QLatin1String("QWizard::CommitButton"));
        m_wizardButtonTypes.insert(QWizard::FinishButton, QLatin1String("QWizard::FinishButton"));
        m_wizardButtonTypes.insert(QWizard::CancelButton, QLatin1String("QWizard::CancelButton"));
        m_wizardButtonTypes.insert(QWizard::HelpButton, QLatin1String("QWizard::HelpButton"));
        m_wizardButtonTypes.insert(QWizard::CustomButton1, QLatin1String("QWizard::CustomButton1"));
        m_wizardButtonTypes.insert(QWizard::CustomButton2, QLatin1String("QWizard::CustomButton2"));
        m_wizardButtonTypes.insert(QWizard::CustomButton3, QLatin1String("QWizard::CustomButton3"));
        m_wizardButtonTypes.insert(QWizard::Stretch, QLatin1String("QWizard::Stretch"));
    }

    QString buttonType(int wizardButton)
    {
        return m_wizardButtonTypes.value(static_cast<QWizard::WizardButton>(wizardButton),
            QLatin1String("unknown button"));
    }

    int m_currentId;
    bool m_modified;
    bool m_autoSwitchPage;
    bool m_showSettingsButton;
    bool m_silent;
    QHash<int, QWizardPage*> m_defaultPages;
    QHash<int, QString> m_defaultButtonText;

    QJSValue m_controlScriptContext;
    QHash<QWizard::WizardButton, QString> m_wizardButtonTypes;   
};


// -- PackageManagerGui

/*!
    \class QInstaller::PackageManagerGui
    \inmodule QtInstallerFramework
    \brief The PackageManagerGui class provides the core functionality for non-interactive
        installations.
*/

/*!
    \fn void QInstaller::PackageManagerGui::interrupted()
    \sa {gui::interrupted}{gui.interrupted}
*/

/*!
    \fn void QInstaller::PackageManagerGui::languageChanged()
    \sa {gui::languageChanged}{gui.languageChanged}
*/

/*!
    \fn void QInstaller::PackageManagerGui::finishButtonClicked()
    \sa {gui::finishButtonClicked}{gui.finishButtonClicked}
*/

/*!
    \fn void QInstaller::PackageManagerGui::gotRestarted()
    \sa {gui::gotRestarted}{gui.gotRestarted}
*/

/*!
    \fn void QInstaller::PackageManagerGui::settingsButtonClicked()
    \sa {gui::settingsButtonClicked}{gui.settingsButtonClicked}
*/

/*!
    \fn void QInstaller::PackageManagerGui::packageManagerCore() const

    Returns the package manager core.
*/

/*!
    Constructs a package manager UI with package manager specified by \a core
    and \a parent as parent.
*/
PackageManagerGui::PackageManagerGui(PackageManagerCore *core, QWidget *parent)
    : QWizard(parent)
    , d(new Private)
    , m_core(core)
    , space_label_(nullptr)
    , size_adjust_(0, 0)
    , origin_dpi_(0)
    , origin_size_(0, 0)
    , current_dpi_(0)
{
    QWidget* flickerWidget = (QWidget*)(children()[0]);
    flickerWidget->layout()->setContentsMargins(0, 0, 0, 0);

    setObjectName(QLatin1String("PackageManagerGui"));
    if(wizardConcise()){
        space_label_ = new QLabel(this);

        space_label_->setFixedHeight(core->isInstaller() ? 2 : 6);
        space_label_->setStyleSheet(QLatin1String("border-image: url(:/space.png)"));
        space_label_->move(kShadowLen, 32 + kShadowLen);
        space_label_->setVisible(true);
    } else {
        if (m_core->isInstaller()) {
            setWindowTitle(tr("%1 Installation guide").arg(m_core->value(scTitle)));
        }
    }

#ifndef Q_OS_MACOS
    setWindowIcon(QIcon(m_core->settings().installerWindowIcon()));
#endif
    if (!m_core->settings().wizardShowPageList()) {
        QString pixmapStr = m_core->settings().background();
        QInstaller::replaceHighDpiImage(pixmapStr);
        setPixmap(QWizard::BackgroundPixmap, pixmapStr);
    }
#ifdef Q_OS_LINUX
    setWizardStyle(QWizard::ModernStyle);
    setSizeGripEnabled(true);
#endif

    if (!m_core->settings().wizardStyle().isEmpty()){
        setWizardStyle(ClassicStyle);
    }

    // set custom stylesheet
    const QString styleSheetFile = m_core->settings().styleSheet();
    if (!styleSheetFile.isEmpty()) {
        QFile sheet(styleSheetFile);
        if (sheet.exists()) {
            if (sheet.open(QIODevice::ReadOnly)) {
                qApp->setStyleSheet(QString::fromLatin1(sheet.readAll()));
            } else {
                qCWarning(QInstaller::lcDeveloperBuild) << "The specified style sheet file "
                    "can not be opened.";
            }
        } else {
            qCWarning(QInstaller::lcDeveloperBuild) << "A style sheet file is specified, "
                "but it does not exist.";
        }
    }

    setOption(QWizard::NoBackButtonOnStartPage);
    setOption(QWizard::NoBackButtonOnLastPage);

    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    if (m_core->settings().wizardShowPageList()) {
        QWidget *sideWidget = new QWidget(this);
        sideWidget->setObjectName(QLatin1String("SideWidget"));

        m_pageListWidget = new QListWidget(sideWidget);
        m_pageListWidget->setObjectName(QLatin1String("PageListWidget"));
        m_pageListWidget->viewport()->setAutoFillBackground(false);
        m_pageListWidget->setFrameShape(QFrame::NoFrame);
        m_pageListWidget->setMinimumWidth(200);
        // The widget should be view-only but we do not want it to be grayed out,
        // so instead of calling setEnabled(false), do not accept focus.
        m_pageListWidget->setFocusPolicy(Qt::NoFocus);
        m_pageListWidget->setSelectionMode(QAbstractItemView::NoSelection);
        m_pageListWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

        QVBoxLayout *sideWidgetLayout = new QVBoxLayout(sideWidget);

        QString pageListPixmap = m_core->settings().pageListPixmap();
        if (!pageListPixmap.isEmpty()) {
            QInstaller::replaceHighDpiImage(pageListPixmap);
            QLabel *pageListPixmapLabel = new QLabel(sideWidget);
            pageListPixmapLabel->setObjectName(QLatin1String("PageListPixmapLabel"));
            pageListPixmapLabel->setPixmap(pageListPixmap);
            pageListPixmapLabel->setMinimumWidth(QPixmap(pageListPixmap).width());
            sideWidgetLayout->addWidget(pageListPixmapLabel);
        }
        sideWidgetLayout->addWidget(m_pageListWidget);
        sideWidget->setLayout(sideWidgetLayout);

        setSideWidget(sideWidget);
    }

    connect(this, &QDialog::rejected, m_core, &PackageManagerCore::setCanceled);
    connect(this, &PackageManagerGui::interrupted, m_core, &PackageManagerCore::interrupt);

    // both queued to show the finished page once everything is done
    connect(m_core, &PackageManagerCore::installationFinished,
            this, &PackageManagerGui::showFinishedPage,
        Qt::QueuedConnection);
    connect(m_core, &PackageManagerCore::uninstallationFinished,
            this, &PackageManagerGui::showFinishedPage,
        Qt::QueuedConnection);

    connect(this, &QWizard::currentIdChanged, this, &PackageManagerGui::currentPageChanged);
    connect(this, &QWizard::currentIdChanged, m_core, &PackageManagerCore::currentPageChanged);
    connect(button(QWizard::FinishButton), &QAbstractButton::clicked,
            this, &PackageManagerGui::finishButtonClicked);
    connect(button(QWizard::FinishButton), &QAbstractButton::clicked,
            m_core, &PackageManagerCore::finishButtonClicked);

    // make sure the QUiLoader's retranslateUi is executed first, then the script
    connect(this, &PackageManagerGui::languageChanged,
            m_core, &PackageManagerCore::languageChanged, Qt::QueuedConnection);
    connect(this, &PackageManagerGui::languageChanged,
            this, &PackageManagerGui::onLanguageChanged, Qt::QueuedConnection);

    connect(m_core,
        &PackageManagerCore::wizardPageInsertionRequested,
        this, &PackageManagerGui::wizardPageInsertionRequested);
    connect(m_core, &PackageManagerCore::wizardPageRemovalRequested,
            this, &PackageManagerGui::wizardPageRemovalRequested);
    connect(m_core, &PackageManagerCore::wizardWidgetInsertionRequested,
        this, &PackageManagerGui::wizardWidgetInsertionRequested);
    connect(m_core, &PackageManagerCore::wizardWidgetRemovalRequested,
            this, &PackageManagerGui::wizardWidgetRemovalRequested);
    connect(m_core, &PackageManagerCore::wizardPageVisibilityChangeRequested,
            this, &PackageManagerGui::wizardPageVisibilityChangeRequested, Qt::QueuedConnection);

    connect(m_core, &PackageManagerCore::setValidatorForCustomPageRequested,
            this, &PackageManagerGui::setValidatorForCustomPageRequested);

    connect(m_core, &PackageManagerCore::setAutomatedPageSwitchEnabled,
            this, &PackageManagerGui::setAutomatedPageSwitchEnabled);

    connect(this, &QWizard::customButtonClicked, this, &PackageManagerGui::customButtonClicked);

    for (int i = QWizard::BackButton; i < QWizard::CustomButton1; ++i)
        d->m_defaultButtonText.insert(i, buttonText(QWizard::WizardButton(i)));

    m_core->setGuiObject(this);

    // We need to create this ugly hack so that the installer doesn't exceed the maximum size of the
    // screen. The screen size where the widget lies is not available until the widget is visible.
    QTimer::singleShot(30, this, SLOT(setMaxSize()));

#ifdef WIN32
    HDC hdcScreen = GetDC(nullptr);
    origin_dpi_ = GetDeviceCaps(hdcScreen, LOGPIXELSX);
    ReleaseDC(nullptr, hdcScreen);
#else
    int screenNum = qApp->desktop()->screenNumber(this);
    if (screenNum >= 0) {
        QScreen* screen = qApp->screens().at(screenNum);
        origin_dpi_ = screen->logicalDotsPerInch();
    }
#endif
}

/*!
    Limits installer maximum size to screen size.
*/
void PackageManagerGui::setMaxSize()
{
    QSize size = qApp->desktop()->availableGeometry(this).size();
    int windowFrameHeight = frameGeometry().height() - geometry().height();
    int availableHeight = size.height() - windowFrameHeight;

    size.setHeight(availableHeight);
    setMaximumSize(size);
}

/*!
    Updates the installer page list.
*/
void PackageManagerGui::updatePageListWidget()
{
    if (!m_core->settings().wizardShowPageList() || !m_pageListWidget)
        return;

    static const QRegularExpression regExp1 {QLatin1String("(.)([A-Z][a-z]+)")};
    static const QRegularExpression regExp2 {QLatin1String("([a-z0-9])([A-Z])")};

    m_pageListWidget->clear();
    foreach (int id, pageIds()) {
        PackageManagerPage *page = qobject_cast<PackageManagerPage *>(pageById(id));
        if (!page->showOnPageList())
            continue;

        // Use page list title if set, otherwise try to use the normal title. If that
        // is not set either, use the object name with spaces added between words.
        QString itemText;
        if (!page->pageListTitle().isEmpty()) {
            itemText = page->pageListTitle();
        } else if (!page->title().isEmpty()) {
            // Title may contain formatting, return only contained plain text
            QTextDocument doc;
            doc.setHtml(page->title());
            itemText = doc.toPlainText().trimmed();
        } else {
            // Remove "Page" suffix from object name if exists and add spaces between words
            itemText = page->objectName();
            itemText.remove(QLatin1String("Page"), Qt::CaseInsensitive);
            itemText.replace(regExp1, QLatin1String("\\1 \\2"));
            itemText.replace(regExp2, QLatin1String("\\1 \\2"));
        }
        QListWidgetItem *item = new QListWidgetItem(itemText, m_pageListWidget);
        item->setSizeHint(QSize(item->sizeHint().width(), 30));

        // Give visual indication about current & non-visited pages
        if (id == d->m_currentId) {
            QFont currentItemFont = item->font();
            currentItemFont.setBold(true);
            item->setFont(currentItemFont);
            // Current item should be always visible on the list
            m_pageListWidget->scrollToItem(item);
        } else if (id > d->m_currentId) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
    }
}

/*!
    Destructs a package manager UI.
*/
PackageManagerGui::~PackageManagerGui()
{
    m_core->setGuiObject(nullptr);
    delete d;
}

/*!
    Returns the style of the package manager UI depending on \a name:

    \list
        \li \c Classic - Classic UI style for Windows 7 and earlier.
        \li \c Modern - Modern UI style for Windows 8.
        \li \c Mac - UI style for macOS.
        \li \c Aero - Aero Peek for Windows 7.
    \endlist
*/
QWizard::WizardStyle PackageManagerGui::getStyle(const QString &name)
{
    if (name == QLatin1String("Classic"))
        return QWizard::ClassicStyle;

    if (name == QLatin1String("Modern"))
        return QWizard::ModernStyle;

    if (name == QLatin1String("Mac"))
        return QWizard::MacStyle;

    if (name == QLatin1String("Aero"))
        return QWizard::AeroStyle;
    return QWizard::ModernStyle;
}

/*!
   Hides the GUI when \a silent is \c true.
*/
void PackageManagerGui::setSilent(bool silent, bool bSilentInstall)
{
  d->m_silent = silent;
  setVisible(!silent);

  if (silent && bSilentInstall) {
      foreach(const int id, pageIds()) {
          PackageManagerPage* wizardPage = dynamic_cast<PackageManagerPage*>(page(id));
          if (wizardPage) {
              wizardPage->setSilent(true);
          }
      }

      QSettings last_setting(QSettings::NativeFormat, QSettings::UserScope,
          packageManagerCore()->value(scPublisher),
          packageManagerCore()->value(scName));
      QString target_dir = last_setting.value(QLatin1String(kConfigSetupName)).toString();
      if (target_dir.isEmpty())
          target_dir = packageManagerCore()->value(scTargetDir);

      QString publisher = packageManagerCore()->value(scPublisher);
      QString title = packageManagerCore()->value(scTitle);
      QString base_path = QDir::separator() + publisher + QDir::separator() + title;
      if (!target_dir.contains(base_path))
      {
          target_dir = target_dir + base_path;
      }
      target_dir = QDir::toNativeSeparators(QDir(target_dir).absolutePath());

      QString version = packageManagerCore()->value(QLatin1String("Version"));
      target_dir = target_dir + QDir::separator() + version;
      packageManagerCore()->setValue(QLatin1String("TargetDir"), target_dir);

      QTimer::singleShot(100, packageManagerCore(), SLOT(runInstaller()));
  }
}

/*!
    Returns the current silent state.
*/
bool PackageManagerGui::isSilent() const
{
  return d->m_silent;
}

/*!
    Updates the model of \a object (which must be a QComboBox or
    QAbstractItemView) such that it contains the given \a items.
*/
void PackageManagerGui::setTextItems(QObject *object, const QStringList &items)
{
    if (QComboBox *comboBox = qobject_cast<QComboBox*>(object)) {
        comboBox->setModel(new QStringListModel(items));
        return;
    }

    if (QAbstractItemView *view = qobject_cast<QAbstractItemView*>(object)) {
        view->setModel(new QStringListModel(items));
        return;
    }

    qCWarning(QInstaller::lcDeveloperBuild) << "Cannot set text items on object of type"
                                            << object->metaObject()->className() << ".";
}

/*!
    Enables automatic page switching when \a request is \c true.
*/
void PackageManagerGui::setAutomatedPageSwitchEnabled(bool request)
{
    d->m_autoSwitchPage = request;
}

/*!
    Returns the default text for the button specified by \a wizardButton.

    \sa {gui::defaultButtonText}{gui.defaultButtonText}
*/
QString PackageManagerGui::defaultButtonText(int wizardButton) const
{
    return d->m_defaultButtonText.value(wizardButton);
}

/*
    Check if we need to "transform" the finish button into a cancel button, caused by the misuse of
    cancel as the finish button on the FinishedPage. This is only a problem if we run as updater or
    package manager, as then there will be two button shown on the last page with the cancel button
    renamed to "Finish".
*/
static bool swapFinishButton(PackageManagerCore *core, int currentId, int button)
{
    if (button != QWizard::FinishButton)
        return false;

    if (currentId != PackageManagerCore::InstallationFinished)
        return false;

    if (core->isInstaller() || core->isUninstaller())
        return false;

    return true;
}

/*!
    Clicks the button specified by \a wb after the delay specified by \a delay.

    \sa {gui::clickButton}{gui.clickButton}
*/
void PackageManagerGui::clickButton(int wb, int delay)
{
    // We need to to swap here, cause scripts expect to call this function with FinishButton on the
    // finish page.
    if (swapFinishButton(m_core, currentId(), wb))
        wb = QWizard::CancelButton;

    if (QAbstractButton *b = button(static_cast<QWizard::WizardButton>(wb)))
        QTimer::singleShot(delay, b, &QAbstractButton::click);
    else
        qCWarning(QInstaller::lcDeveloperBuild) << "Button with type: " << d->buttonType(wb) << "not found!";
}

/*!
    Clicks the button specified by \a objectName after the delay specified by \a delay.

    \sa {gui::clickButton}{gui.clickButton}
*/
void PackageManagerGui::clickButton(const QString &objectName, int delay) const
{
    QPushButton *button = findChild<QPushButton *>(objectName);
    if (button)
        QTimer::singleShot(delay, button, &QAbstractButton::click);
    else
        qCWarning(QInstaller::lcDeveloperBuild) << "Button with objectname: " << objectName << "not found!";
}

/*!
    Returns \c true if the button specified by \a wb is enabled. Returns \c false
    if a button of the specified type is not found.

    \sa {gui::isButtonEnabled}{gui.isButtonEnabled}
*/
bool PackageManagerGui::isButtonEnabled(int wb)
{
    // We need to to swap here, cause scripts expect to call this function with FinishButton on the
    // finish page.
    if (swapFinishButton(m_core, currentId(), wb))
            wb = QWizard::CancelButton;

    if (QAbstractButton *b = button(static_cast<QWizard::WizardButton>(wb)))
        return b->isEnabled();

    qCWarning(QInstaller::lcDeveloperBuild) << "Button with type: " << d->buttonType(wb) << "not found!";
    return false;
}

/*!
    Sets a validator for the custom page specified by \a name and
    \a callbackName requested by \a component.
*/
void PackageManagerGui::setValidatorForCustomPageRequested(Component *component,
    const QString &name, const QString &callbackName)
{
    component->setValidatorCallbackName(callbackName);

    const QString componentName = QLatin1String("Dynamic") + name;
    const QList<int> ids = pageIds();
    foreach (const int i, ids) {
        PackageManagerPage *const p = qobject_cast<PackageManagerPage*> (page(i));
        if (p && p->objectName() == componentName) {
            p->setValidatePageComponent(component);
            return;
        }
    }
}

/*!
    Loads the script specified by \a scriptPath to perform the installation non-interactively.
    Throws QInstaller::Error if the script is not readable or it cannot be
    parsed.
*/
void PackageManagerGui::loadControlScript(const QString &scriptPath)
{
    d->m_controlScriptContext = m_core->controlScriptEngine()->loadInContext(
        QLatin1String("Controller"), scriptPath);
    qCDebug(QInstaller::lcInstallerInstallLog) << "Loaded control script" << scriptPath;
}

/*!
    Calls the control script method specified by \a methodName.
*/
void PackageManagerGui::callControlScriptMethod(const QString &methodName)
{
    if (d->m_controlScriptContext.isUndefined())
        return;
    try {
        const QJSValue returnValue = m_core->controlScriptEngine()->callScriptMethod(
            d->m_controlScriptContext, methodName);
        if (returnValue.isUndefined()) {
            qCDebug(QInstaller::lcDeveloperBuild) << "Control script callback" << methodName
                << "does not exist.";
            return;
        }
    } catch (const QInstaller::Error &e) {
        qCritical() << qPrintable(e.message());
    }
}

/*!
    Executes the control script on the page specified by \a pageId.
*/
void PackageManagerGui::executeControlScript(int pageId)
{
    if (PackageManagerPage *const p = qobject_cast<PackageManagerPage*> (page(pageId)))
        callControlScriptMethod(p->objectName() + QLatin1String("Callback"));
}


/*!
    Replaces the default button text with translated text when the application
    language changes.
*/
void PackageManagerGui::onLanguageChanged()
{
    d->m_defaultButtonText.clear();
    for (int i = QWizard::BackButton; i < QWizard::CustomButton1; ++i)
        d->m_defaultButtonText.insert(i, buttonText(QWizard::WizardButton(i)));
}

/*!
    \reimp
*/
bool PackageManagerGui::event(QEvent *event)
{
    switch(event->type()) {
    case QEvent::LanguageChange:
        emit languageChanged();
        break;
    default:
        break;
    }
    return QWizard::event(event);
}

/*!
    \reimp
*/
void PackageManagerGui::showEvent(QShowEvent *event)
{
    if (!event->spontaneous()) {
        foreach (int id, pageIds()) {
            const QString subTitle = page(id)->subTitle();
            if (subTitle.isEmpty()) {
                const QWizard::WizardStyle style = wizardStyle();
                if ((style == QWizard::ClassicStyle) || (style == QWizard::ModernStyle)) {
                    // otherwise the colors might screw up
                    page(id)->setSubTitle(QLatin1String(" "));
                }
            }
        }

        if (m_core->isInstaller()) {
            QSize minimumSize;
            minimumSize.setWidth(m_core->settings().wizardMinimumWidth()
                ? m_core->settings().wizardMinimumWidth()
                : width());

            minimumSize.setHeight(m_core->settings().wizardMinimumHeight()
                ? m_core->settings().wizardMinimumHeight()
                : height());

            origin_size_ = minimumSize;
            setMinimumSize(minimumSize);
            if (minimumWidth() < m_core->settings().wizardDefaultWidth()) {
                origin_size_.setWidth(m_core->settings().wizardDefaultWidth() + 2 * kShadowLen);
            }
                
            if (minimumHeight() < m_core->settings().wizardDefaultHeight()) {
                origin_size_.setHeight(m_core->settings().wizardDefaultHeight() + 2 * kShadowLen);
            }

            resize(origin_size_);
        }
        else {
            origin_size_.setWidth(337 + 2 * kShadowLen);
            origin_size_.setHeight(172 + 2 * kShadowLen);
            resize(origin_size_);
        }

        current_size_ = origin_size_;
    }
    QWizard::showEvent(event);
    QMetaObject::invokeMethod(this, "dependsOnLocalInstallerBinary", Qt::QueuedConnection);
}

/*!
    Requests the insertion of the page specified by \a widget at the position specified by \a page.
    If that position is already occupied by another page, the value is decremented until an empty
    slot is found.
*/
void PackageManagerGui::wizardPageInsertionRequested(QWidget *widget,
    QInstaller::PackageManagerCore::WizardPage page)
{
    // just in case it was already in there...
    wizardPageRemovalRequested(widget);

    int pageId = static_cast<int>(page) - 1;
    while (QWizard::page(pageId) != nullptr)
        --pageId;
    qInfo() << "add dynamic wizard page ";
    // add it
    setPage(pageId, new DynamicInstallerPage(widget, m_core));
    updatePageListWidget();
}

/*!
    Requests the removal of the page specified by \a widget.
*/
void PackageManagerGui::wizardPageRemovalRequested(QWidget *widget)
{
    foreach (int pageId, pageIds()) {
        DynamicInstallerPage *const dynamicPage = qobject_cast<DynamicInstallerPage*>(page(pageId));
        if (dynamicPage == nullptr)
            continue;
        if (dynamicPage->widget() != widget)
            continue;
        removePage(pageId);
        d->m_defaultPages.remove(pageId);
        packageManagerCore()->controlScriptEngine()->removeFromGlobalObject(dynamicPage);
        packageManagerCore()->componentScriptEngine()->removeFromGlobalObject(dynamicPage);
    }
    updatePageListWidget();
}

/*!
    Requests the insertion of \a widget on \a page. Widget with lower
    \a position number will be inserted on top.
*/
void PackageManagerGui::wizardWidgetInsertionRequested(QWidget *widget,
    QInstaller::PackageManagerCore::WizardPage page, int position)
{
    Q_ASSERT(widget);

    if (PackageManagerPage *p = qobject_cast<PackageManagerPage *>(QWizard::page(page))) {
        p->m_customWidgets.insert(position, widget);
        if (p->m_customWidgets.count() > 1 ) {
            //Reorder the custom widgets based on their position
            QMultiMap<int, QWidget*>::Iterator it = p->m_customWidgets.begin();
            while (it != p->m_customWidgets.end()) {
                p->layout()->removeWidget(it.value());
                ++it;
            }
            it = p->m_customWidgets.begin();
            while (it != p->m_customWidgets.end()) {
                p->layout()->addWidget(it.value());
                ++it;
            }
        } else {
            p->layout()->addWidget(widget);
        }
        packageManagerCore()->controlScriptEngine()->addToGlobalObject(p);
        packageManagerCore()->componentScriptEngine()->addToGlobalObject(p);
    }
}

/*!
    Requests the removal of \a widget from installer pages.
*/
void PackageManagerGui::wizardWidgetRemovalRequested(QWidget *widget)
{
    Q_ASSERT(widget);

    const QList<int> pages = pageIds();
    foreach (int id, pages) {
        PackageManagerPage *managerPage = qobject_cast<PackageManagerPage *>(page(id));
        managerPage->removeCustomWidget(widget);
    }
    widget->setParent(nullptr);
    packageManagerCore()->controlScriptEngine()->removeFromGlobalObject(widget);
    packageManagerCore()->componentScriptEngine()->removeFromGlobalObject(widget);
}

/*!
    Requests changing the visibility of the page specified by \a p to
    \a visible.
*/
void PackageManagerGui::wizardPageVisibilityChangeRequested(bool visible, int p)
{
    if (visible && page(p) == nullptr) {
        setPage(p, d->m_defaultPages[p]);
    } else if (!visible && page(p) != nullptr) {
        d->m_defaultPages[p] = page(p);
        removePage(p);
    }
    updatePageListWidget();
}

/*!
    Returns the page specified by \a id.

    \sa {gui::pageById}{gui.pageById}
*/
QWidget *PackageManagerGui::pageById(int id) const
{
    return page(id);
}

/*!
    Returns the page specified by the object name \a name from a UI file.

    \sa {gui::pageByObjectName}{gui.pageByObjectName}
*/
QWidget *PackageManagerGui::pageByObjectName(const QString &name) const
{
    const QList<int> ids = pageIds();
    foreach (const int i, ids) {
        PackageManagerPage *const p = qobject_cast<PackageManagerPage*> (page(i));
        if (p && p->objectName() == name)
            return p;
    }
    qCDebug(QInstaller::lcDeveloperBuild) << "No page found for object name" << name;
    return nullptr;
}

/*!
    \sa {gui::currentPageWidget}{gui.currentPageWidget}
*/
QWidget *PackageManagerGui::currentPageWidget() const
{
    return currentPage();
}

/*!
    For dynamic pages, returns the widget specified by \a name read from the UI
    file.

    \sa {gui::pageWidgetByObjectName}{gui.pageWidgetByObjectName}
*/
QWidget *PackageManagerGui::pageWidgetByObjectName(const QString &name) const
{
    QWidget *const widget = pageByObjectName(name);
    if (PackageManagerPage *const p = qobject_cast<PackageManagerPage*> (widget)) {
        // For dynamic pages, return the contained widget (as read from the UI file), not the
        // wrapper page
        if (DynamicInstallerPage *dp = qobject_cast<DynamicInstallerPage *>(p))
            return dp->widget();
        return p;
    }
    qCDebug(QInstaller::lcDeveloperBuild) << "No page found for object name" << name;
    return nullptr;
}

/*!
    \sa {gui::cancelButtonClicked}{gui.cancelButtonClicked}
*/
void PackageManagerGui::cancelButtonClicked()
{
    const int id = currentId();
    if (id == PackageManagerCore::Introduction || id == PackageManagerCore::InstallationFinished
        || id == PackageManagerCore::PesFinished || id == PackageManagerCore::PesError) {
        m_core->setNeedsHardRestart(false);
        QDialog::reject(); return;
    }

    if(id == PackageManagerCore::ReadyForInstallation && packageManagerCore()->isUninstaller()){
        QDialog::reject();
        return;
    }

    QString question;
    PackageManagerPage *const page = qobject_cast<PackageManagerPage*> (currentPage());
    if (page && page->isInterruptible()
        && m_core->status() != PackageManagerCore::Canceled
        && m_core->status() != PackageManagerCore::Failure) {
            question = tr("Do you want to cancel \"%1\" installation process ?").arg(m_core->value(scTitle));
            if (m_core->isUninstaller())
                question = tr("Do you want to cancel the removal process ?");
    } else {
        question = tr("Do you want to quit \"%1\"installer application ?").arg(m_core->value(scTitle));
        if (m_core->isUninstaller())
            question = tr("Do you want to quit the \"%1\"uninstaller application ?").arg(m_core->value(scTitle));;
        if (m_core->isMaintainer())
            question = tr("Do you want to quit the \"%1\"maintenance application ?").arg(m_core->value(scTitle));;
    }

    if (page && !page->isInterruptible()) {
        return;
    }

    const QMessageBox::StandardButton button =
        MessageBoxHandler::question(MessageBoxHandler::currentBestSuitParent(),
        QLatin1String("cancelInstallation"), tr("%1").arg(m_core->value(scTitle)), question,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    
    if (button == QMessageBox::Yes) {
        QDialog::reject();
    }
}

/*!
   \sa {gui::rejectWithoutPrompt}{gui.rejectWithoutPrompt}
*/
void PackageManagerGui::rejectWithoutPrompt()
{
    QDialog::reject();
}

/*!
    \reimp
*/
void PackageManagerGui::reject()
{
    cancelButtonClicked();
}

/*!
    \internal
*/
void PackageManagerGui::setModified(bool value)
{
    d->m_modified = value;
}

/*!
    \sa {gui::showFinishedPage}{gui.showFinishedPage}
*/
void PackageManagerGui::showFinishedPage()
{
    if (isSilent()) {
        QDialog::done(Accepted);
        qApp->exit(0);
        return;
    }
    if (d->m_autoSwitchPage)
        next();
    else
        qobject_cast<QPushButton*>(button(QWizard::CancelButton))->setEnabled(false);
}

/*!
    Shows the \uicontrol Settings button if \a show is \c true.

    \sa {gui::showSettingsButton}{gui.showSettingsButton}
*/
void PackageManagerGui::showSettingsButton(bool show)
{
    if (d->m_showSettingsButton == show)
        return;

    d->m_showSettingsButton = show;
    setOption(QWizard::HaveCustomButton1, show);
    setButtonText(QWizard::CustomButton1, tr("Settings"));
    button(QWizard::CustomButton1)->setToolTip(
        PackageManagerGui::tr("Specify proxy settings and configure repositories for add-on components."));

    updateButtonLayout();
}

/*!
    Forces an update of our own button layout. Needs to be called whenever a
    button option has been set.
*/
void PackageManagerGui::updateButtonLayout()
{
    QVector<QWizard::WizardButton> buttons(12, QWizard::NoButton);
    if (options() & QWizard::HaveHelpButton)
        buttons[(options() & QWizard::HelpButtonOnRight) ? 11 : 0] = QWizard::HelpButton;

    buttons[1] = QWizard::Stretch;
    if (options() & QWizard::HaveCustomButton1) {
        buttons[1] = QWizard::CustomButton1;
        buttons[2] = QWizard::Stretch;
    }

    if (options() & QWizard::HaveCustomButton2)
        buttons[3] = QWizard::CustomButton2;

    if (options() & QWizard::HaveCustomButton3)
        buttons[4] = QWizard::CustomButton3;

    if (!(options() & QWizard::NoCancelButton))
        buttons[(options() & QWizard::CancelButtonOnLeft) ? 5 : 10] = QWizard::CancelButton;

    buttons[6] = QWizard::BackButton;
    buttons[7] = QWizard::NextButton;
    buttons[8] = QWizard::CommitButton;
    buttons[9] = QWizard::FinishButton;

    setOption(QWizard::NoBackButtonOnLastPage, true);
    setOption(QWizard::NoBackButtonOnStartPage, true);

    setButtonLayout(buttons.toList());
}

/*!
    Enables the \uicontrol Settings button by setting \a enabled to \c true.

    \sa {gui::setSettingsButtonEnabled}{gui.setSettingsButtonEnabled}
*/
void PackageManagerGui::setSettingsButtonEnabled(bool enabled)
{
    if (QAbstractButton *btn = button(QWizard::CustomButton1))
        btn->setEnabled(enabled);
}

/*!
    Emits the settingsButtonClicked() signal when the custom button specified by \a which is
    clicked if \a which is the \uicontrol Settings button.
*/
void PackageManagerGui::customButtonClicked(int which)
{
    if (QWizard::WizardButton(which) == QWizard::CustomButton1 && d->m_showSettingsButton)
        emit settingsButtonClicked();
}

/*!
    Prevents installation from a network location by determining that a local
    installer binary must be used.
*/
void PackageManagerGui::dependsOnLocalInstallerBinary()
{
    if (m_core->settings().dependsOnLocalInstallerBinary() && !m_core->localInstallerBinaryUsed()) {
        MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(),
            QLatin1String("Installer_Needs_To_Be_Local_Error"), tr("Error"),
            tr("It is not possible to install from network location.\n"
               "Please copy the installer to a local drive"), QMessageBox::Ok);
        rejectWithoutPrompt();
    }
}

/*!
    Called when the current page changes to \a newId. Calls the leaving() method for the old page
    and the entering() method for the new one. Also, executes the control script associated with the
    new page by calling executeControlScript(). Updates the page list set as QWizard::sideWidget().


    Emits the left() and entered() signals.
*/
void PackageManagerGui::currentPageChanged(int newId)
{
    PackageManagerPage *oldPage = qobject_cast<PackageManagerPage *>(page(d->m_currentId));
    if (oldPage) {
        oldPage->leaving();
        emit oldPage->left();
    }

    d->m_currentId = newId;

    PackageManagerPage *newPage = qobject_cast<PackageManagerPage *>(page(d->m_currentId));
    if (newPage) {
        newPage->entering();
        emit newPage->entered();
        updatePageListWidget();
    }

    executeControlScript(newId);
}

bool QInstaller::PackageManagerGui::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
#ifdef WIN32
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_DPICHANGED) {
        DWORD dpi = LOWORD(msg->wParam);
        current_dpi_ = dpi;
        if (origin_dpi_ != 0 && dpi != 0) {
            int mWidth = origin_size_.width() / origin_dpi_ * dpi;
            int mHeight = origin_size_.height() / origin_dpi_ * dpi;

            QSize mSize(mWidth, mHeight);
            current_size_ = mSize;

            resize(mSize);
            repaint();
        }
    }
#endif
    return QWizard::nativeEvent(eventType, message, result);
}

void QInstaller::PackageManagerGui::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QBrush(Qt::white));
    painter.setPen(Qt::transparent);

    int radius = 8;

    QColor color(102, 102, 102, 200);
    for (int i = 0; i < kShadowLen; i++)
    {
        int nAlpha = 120 - sqrt(i) * 50;
        if (nAlpha < 0)
            break;
        color.setAlpha(nAlpha);
        painter.setPen(color);
        painter.setBrush(QBrush(Qt::transparent));
        painter.drawRoundedRect(
            kShadowLen - i, kShadowLen - i,
            width() - (kShadowLen - i) * 2,
            height() - (kShadowLen - i) * 2,
            radius, radius);
    }

    painter.setBrush(QBrush(Qt::white));
    QRect drawRect(kShadowLen, kShadowLen, width() - 2 * kShadowLen, height() - 2 * kShadowLen);
    painter.drawRoundedRect(drawRect, radius, radius);

    if (size_adjust_ != size()) {
        QResizeEvent* e = new QResizeEvent(size(), size_adjust_);
        qApp->sendEvent(this, e);
    }
}

void QInstaller::PackageManagerGui::resizeEvent(QResizeEvent* event)
{
    if (space_label_) {
        space_label_->setFixedWidth(width() - 2 * kShadowLen);
    }

    size_adjust_ = event->size();

    if (!origin_size_.isNull() && origin_dpi_ != 0 && current_dpi_ != 0) {
        int mWidth = origin_size_.width() / origin_dpi_ * current_dpi_;
        int mHeight = origin_size_.height() / origin_dpi_ * current_dpi_;
        if (size_adjust_.width() != mWidth || size_adjust_.height() != mHeight) {
//             resize(mWidth, mHeight);
//             return;
        }
    }
   
    QWizard::resizeEvent(event);
}

// -- PackageManagerPage

/*!
    \class QInstaller::PackageManagerPage
    \inmodule QtInstallerFramework
    \brief The PackageManagerPage class displays information about the product
    to install.
*/

/*!
    \fn QInstaller::PackageManagerPage::~PackageManagerPage()

    Destructs a package manager page.
*/

/*!
    \fn QInstaller::PackageManagerPage::gui() const

    Returns the wizard this page belongs to.
*/

/*!
    \fn QInstaller::PackageManagerPage::isInterruptible() const

    Returns \c true if the installation can be interrupted.
*/

/*!
    \fn QInstaller::PackageManagerPage::settingsButtonRequested() const

    Returns \c true if the page requests the wizard to show the \uicontrol Settings button.
*/

/*!
    \fn QInstaller::PackageManagerPage::setSettingsButtonRequested(bool request)

    Determines that the page should request the \uicontrol Settings button if \a request is \c true.
*/

/*!
    \fn QInstaller::PackageManagerPage::entered()

    This signal is called when a page is entered.
*/

/*!
    \fn QInstaller::PackageManagerPage::left()

    This signal is called when a page is left.
*/

/*!
    \fn QInstaller::PackageManagerPage::entering()

    Called when end users enter the page and the PackageManagerGui:currentPageChanged()
    signal is triggered. Supports the QWizardPage::​initializePage() function to ensure
    that the page's fields are properly initialized based on fields from previous pages.
    Otherwise, \c initializePage() would only be called once if the installer has been
    set to QWizard::IndependentPages.
*/

/*!
    \fn QInstaller::PackageManagerPage::showOnPageListChanged()

    Called when page visibility on page list has changed and refresh is needed.
*/

/*!
    \fn QInstaller::PackageManagerPage::leaving()

    Called when end users leave the page and the PackageManagerGui:currentPageChanged()
    signal is triggered.
*/

/*!
    Constructs a package manager page with \a core as parent.
*/
PackageManagerPage::PackageManagerPage(PackageManagerCore *core)
    : m_complete(true)
    , m_titleColor(QString())
    , m_needsSettingsButton(false)
    , m_core(core)
    , validatorComponent(nullptr)
    , m_showOnPageList(true)
    , m_bSilent(false)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    setContentsMargins(0, 0, 0, 0);
    if (!m_core->settings().titleColor().isEmpty())
        m_titleColor = m_core->settings().titleColor();

    if (!m_core->settings().wizardShowPageList())
        setPixmap(QWizard::WatermarkPixmap, wizardPixmap(scWatermark));

    setPixmap(QWizard::BannerPixmap, wizardPixmap(scBanner));
    setPixmap(QWizard::LogoPixmap, wizardPixmap(scLogo));

    // Can't use PackageManagerPage::gui() here as the page is not set yet
    if (PackageManagerGui *gui = qobject_cast<PackageManagerGui *>(core->guiObject())) {
        connect(this, &PackageManagerPage::showOnPageListChanged,
                gui, &PackageManagerGui::updatePageListWidget);
    }
}

/*!
    Returns the package manager core.
*/
PackageManagerCore *PackageManagerPage::packageManagerCore() const
{
    return m_core;
}

/*!
    Returns the pixmap specified by \a pixmapType. \a pixmapType can be \c <Banner>,
    \c <Logo> or \c <Watermark> element of the package information file. If @2x image
    is provided, returns that instead for high DPI displays.
*/
QPixmap PackageManagerPage::wizardPixmap(const QString &pixmapType) const
{
    QString pixmapStr = m_core->value(pixmapType);
    QInstaller::replaceHighDpiImage(pixmapStr);
    QPixmap pixmap(pixmapStr);
    if (pixmapType == scBanner) {
        if (!pixmap.isNull()) {
            int width;
            if (m_core->settings().containsValue(QLatin1String("WizardDefaultWidth")) )
                width = m_core->settings().wizardDefaultWidth();
            else
                width = size().width();
            pixmap = pixmap.scaledToWidth(width, Qt::SmoothTransformation);
        }
    }
    return pixmap;
}

/*!
    Returns the product name of the application being installed.
*/
QString PackageManagerPage::productName() const
{
    return m_core->value(QLatin1String("ProductName"));
}

/*!
    Sets the font color of \a title. The title is specified in the \c <Title>
    element of the package information file. It is the name of the installer as
    displayed on the title bar.
*/
void PackageManagerPage::setColoredTitle(const QString &title)
{
    setTitle(QString::fromLatin1("<font color=\"%1\">%2</font>").arg(m_titleColor, title));
}

/*!
    Sets the font color of \a subTitle.
*/
void PackageManagerPage::setColoredSubTitle(const QString &subTitle)
{
    setSubTitle(QString::fromLatin1("<font color=\"%1\">%2</font>").arg(m_titleColor, subTitle));
}

/*!
    Sets the title shown on installer page indicator for this page to \a title.
    Pages that do not set this will use a fallback title instead.
*/
void PackageManagerPage::setPageListTitle(const QString &title)
{
    m_pageListTitle = title;
}

/*!
    Returns the title shown on installer page indicator for this page. If empty,
    a fallback title is being used instead.
*/
QString PackageManagerPage::pageListTitle() const
{
    return m_pageListTitle;
}

/*!
    Sets the page visibility on installer page indicator based on \a show.
    All pages are shown by default.
*/
void PackageManagerPage::setShowOnPageList(bool show)
{
    if (m_showOnPageList != show)
        emit showOnPageListChanged();

    m_showOnPageList = show;
}

/*!
    Returns \c true if the page should be shown on installer page indicator.
*/
bool PackageManagerPage::showOnPageList() const
{
    return m_showOnPageList;
}

/*!
    Returns \c true if the page is complete; otherwise, returns \c false.
*/
bool PackageManagerPage::isComplete() const
{
    return m_complete;
}

/*!
    Sets the package manager page to complete if \a complete is \c true. Emits
    the completeChanged() signal.
*/
void PackageManagerPage::setComplete(bool complete)
{
    m_complete = complete;
    if (QWizard *w = wizard()) {
        if (QAbstractButton *cancel = w->button(QWizard::CancelButton)) {
            if (cancel->hasFocus()) {
                if (QAbstractButton *next = w->button(QWizard::NextButton))
                    next->setFocus();
            }
        }
    }
    emit completeChanged();
}

/*!
    Sets the \a component that validates the page.
*/
void PackageManagerPage::setValidatePageComponent(Component *component)
{
    validatorComponent = component;
}

/*!
    Returns \c true if the end user has entered complete and valid information.
*/
bool PackageManagerPage::validatePage()
{
    if (validatorComponent)
        return validatorComponent->validatePage();
    return true;
}

/*!
    \internal
*/
void PackageManagerPage::removeCustomWidget(const QWidget *widget)
{
    for (auto it = m_customWidgets.begin(); it != m_customWidgets.end();) {
        if (it.value() == widget)
            it = m_customWidgets.erase(it);
        else
            ++it;
    }
}

/*!
    Inserts \a widget at the position specified by \a offset in relation to
    another widget specified by \a siblingName. The default position is directly
    behind the sibling.
*/
void PackageManagerPage::insertWidget(QWidget *widget, const QString &siblingName, int offset)
{
    QWidget *sibling = findChild<QWidget *>(siblingName);
    QWidget *parent = sibling ? sibling->parentWidget() : nullptr;
    QLayout *layout = parent ? parent->layout() : nullptr;
    QBoxLayout *blayout = qobject_cast<QBoxLayout *>(layout);

    if (blayout) {
        const int index = blayout->indexOf(sibling) + offset;
        blayout->insertWidget(index, widget);
    }
}

/*!
    Returns the widget specified by \a objectName.
*/
QWidget *PackageManagerPage::findWidget(const QString &objectName) const
{
    return findChild<QWidget*> (objectName);
}

/*!
    Determines which page should be shown next depending on whether the
    application is being installed, updated, or uninstalled.

    The license check page is shown only if a component that provides a license
    is selected for installation. It is hidden during uninstallation and update.
*/
int PackageManagerPage::nextId() const
{
    const int next = QWizardPage::nextId(); // the page to show next
    if (next == PackageManagerCore::LicenseCheck) {
        // calculate the page after the license page
        const int nextNextId = gui()->pageIds().value(gui()->pageIds().indexOf(next) + 1, -1);
        const PackageManagerCore *const core = packageManagerCore();
        if (core->isUninstaller())
            return nextNextId;  // forcibly hide the license page if we run as uninstaller

        core->calculateComponentsToInstall();
        foreach (Component* component, core->orderedComponentsToInstall()) {
            if (core->isMaintainer() && component->isInstalled())
                continue; // package manager or updater, hide as long as the component is installed

            // The component is about to be installed and provides a license, so the page needs to
            // be shown.
            if (!component->licenses().isEmpty())
                return next;
        }
        return nextNextId;  // no component with a license or all components with license installed
    }
    return next;    // default, show the next page
}

void QInstaller::PackageManagerPage::setSilent(bool bSilent)
{
    m_bSilent = bSilent;
}

bool QInstaller::PackageManagerPage::isSilent() const
{
    return m_bSilent;
}

// -- IntroductionPage

/*!
    \class QInstaller::IntroductionPage
    \inmodule QtInstallerFramework
    \brief The IntroductionPage class displays information about the product to
    install.
*/

/*!
    \fn QInstaller::IntroductionPage::packageManagerCoreTypeChanged()

    This signal is emitted when the package manager core type changes.
*/

/*!
    Constructs an introduction page with \a core as parent.
*/
IntroductionPage::IntroductionPage(PackageManagerCore *core)
    : PackageManagerPage(core)
    , m_updatesFetched(false)
    , m_allPackagesFetched(false)
    , m_label(nullptr)
    , m_msgLabel(nullptr)
    , m_errorLabel(nullptr)
    , m_progressBar(nullptr)
    , m_packageManager(nullptr)
    , m_updateComponents(nullptr)
    , m_removeAllComponents(nullptr)
{
    setObjectName(QLatin1String("IntroductionPage"));
    setColoredTitle(tr("Setup - %1").arg(productName()));

    QVBoxLayout *layout = new QVBoxLayout(this);
    setLayout(layout);

    m_msgLabel = new QLabel(this);
    m_msgLabel->setWordWrap(true);
    m_msgLabel->setObjectName(QLatin1String("MessageLabel"));
    m_msgLabel->setText(tr("Welcome to the %1 Setup Wizard.").arg(productName()));

    QWidget *widget = new QWidget(this);
    QVBoxLayout *boxLayout = new QVBoxLayout(widget);

    m_packageManager = new QRadioButton(tr("&Add or remove components"), this);
    m_packageManager->setObjectName(QLatin1String("PackageManagerRadioButton"));
    boxLayout->addWidget(m_packageManager);
    connect(m_packageManager, &QAbstractButton::toggled, this, &IntroductionPage::setPackageManager);

    m_updateComponents = new QRadioButton(tr("&Update components"), this);
    m_updateComponents->setObjectName(QLatin1String("UpdaterRadioButton"));
    boxLayout->addWidget(m_updateComponents);
    connect(m_updateComponents, &QAbstractButton::toggled, this, &IntroductionPage::setUpdater);

    m_removeAllComponents = new QRadioButton(tr("&Remove all components"), this);
    m_removeAllComponents->setObjectName(QLatin1String("UninstallerRadioButton"));
    boxLayout->addWidget(m_removeAllComponents);
    connect(m_removeAllComponents, &QAbstractButton::toggled,
            this, &IntroductionPage::setUninstaller);
    connect(m_removeAllComponents, &QAbstractButton::toggled,
            core, &PackageManagerCore::setCompleteUninstallation);

    boxLayout->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setObjectName(QLatin1String("InformationLabel"));
    m_label->setText(tr("Retrieving information from remote installation sources..."));
    boxLayout->addWidget(m_label);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    boxLayout->addWidget(m_progressBar);
    m_progressBar->setObjectName(QLatin1String("InformationProgressBar"));

    boxLayout->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    boxLayout->addWidget(m_errorLabel);
    m_errorLabel->setObjectName(QLatin1String("ErrorLabel"));

    layout->addWidget(m_msgLabel);
    layout->addWidget(widget);
    layout->addItem(new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));

    connect(core, &PackageManagerCore::metaJobProgress, this, &IntroductionPage::onProgressChanged);
    connect(core, &PackageManagerCore::metaJobTotalProgress, this, &IntroductionPage::setTotalProgress);
    connect(core, &PackageManagerCore::metaJobInfoMessage, this, &IntroductionPage::setMessage);
    connect(core, &PackageManagerCore::coreNetworkSettingsChanged,
            this, &IntroductionPage::onCoreNetworkSettingsChanged);

    m_updateComponents->setEnabled(ProductKeyCheck::instance()->hasValidKey());

#ifdef Q_OS_WIN
    if (QSysInfo::windowsVersion() >= QSysInfo::WV_WINDOWS7) {
        m_taskButton = new QWinTaskbarButton(this);
        connect(core, &PackageManagerCore::metaJobProgress,
                m_taskButton->progress(), &QWinTaskbarProgress::setValue);
    } else {
        m_taskButton = nullptr;
    }
#endif
}

/*!
    Determines which page should be shown next depending on whether the
    application is being installed, updated, or uninstalled.
*/
int IntroductionPage::nextId() const
{
    if (packageManagerCore()->isUninstaller())
        return PackageManagerCore::ReadyForInstallation;

    return PackageManagerPage::nextId();
}

/*!
    For an uninstaller, always returns \c true. For the package manager and updater, at least
    one valid repository is required. For the online installer, package manager, and updater, valid
    meta data has to be fetched successfully to return \c true.
*/
bool IntroductionPage::validatePage()
{
    PackageManagerCore *core = packageManagerCore();
    if (core->isUninstaller())
        return true;

    setComplete(false);
    bool isOfflineOnlyInstaller = core->isInstaller() && core->isOfflineOnly();
    // If not offline only installer, at least one valid repository needs to be available
    if (!isOfflineOnlyInstaller && !validRepositoriesAvailable()) {
        setErrorMessage(QLatin1String("<font color=\"red\">") + tr("At least one valid and enabled "
            "repository required for this action to succeed.") + QLatin1String("</font>"));
        return isComplete();
    }

    gui()->setSettingsButtonEnabled(false);
    if (core->isMaintainer()) {
        showAll();
        setMaintenanceToolsEnabled(false);
    } else {
        //showMetaInfoUpdate();
    }

#ifdef Q_OS_WIN
    if (m_taskButton) {
        if (!m_taskButton->window()) {
            if (QWidget *widget = QApplication::activeWindow())
                m_taskButton->setWindow(widget->windowHandle());
        }

        m_taskButton->progress()->reset();
        m_taskButton->progress()->resume();
        m_taskButton->progress()->setVisible(true);
    }
#endif

    // fetch updater packages
    if (core->isUpdater()) {
        if (!m_updatesFetched) {
            m_updatesFetched = core->fetchRemotePackagesTree();
            if (!m_updatesFetched)
                setErrorMessage(core->error());
        }

        if (m_updatesFetched) {
            if (core->components(QInstaller::PackageManagerCore::ComponentType::Root).count() <= 0)
                setErrorMessage(QString::fromLatin1("<b>%1</b>").arg(tr("No updates available.")));
            else
                setComplete(true);
        }
    }

    // fetch common packages
    if (core->isInstaller() || core->isPackageManager()) {
        bool localPackagesTreeFetched = false;
        if (!m_allPackagesFetched) {
            // first try to fetch the server side packages tree
            m_allPackagesFetched = core->fetchRemotePackagesTree();
            if (!m_allPackagesFetched) {
                QString error = core->error();
                if (core->isPackageManager() && core->status() != PackageManagerCore::ForceUpdate) {
                    // if that fails and we're in maintenance mode, try to fetch local installed tree
                    localPackagesTreeFetched = core->fetchLocalPackagesTree();
                    if (localPackagesTreeFetched) {
                        // if that succeeded, adjust error message
                        error = QLatin1String("<font color=\"red\">") + error + tr(" Only local package "
                            "management available.") + QLatin1String("</font>");
                    }
                } else if (core->status() == PackageManagerCore::ForceUpdate) {
                    // replaces the error string from packagemanagercore
                    error = tr("There is an important update available. Please select '%1' first")
                        .arg(m_updateComponents->text().remove(QLatin1Char('&')));
                }
                setErrorMessage(error);
            }
        }

        if (m_allPackagesFetched || localPackagesTreeFetched)
            setComplete(true);
    }

    if (core->isMaintainer()) {
        showMaintenanceTools();
        setMaintenanceToolsEnabled(true);
    } else {
        hideAll();
    }
    gui()->setSettingsButtonEnabled(true);

#ifdef Q_OS_WIN
    if (m_taskButton)
        m_taskButton->progress()->setVisible(!isComplete());
#endif
    return isComplete();
}

/*!
    Shows all widgets on the page.
*/
void IntroductionPage::showAll()
{
    showWidgets(true);
}

/*!
    Hides all widgets on the page.
*/
void IntroductionPage::hideAll()
{
    showWidgets(false);
}

/*!
    Hides the widgets on the page except a text label and progress bar.
*/
void IntroductionPage::showMetaInfoUpdate()
{
    showWidgets(false);
    m_label->setVisible(true);
    m_progressBar->setVisible(true);
}

/*!
    Shows the options to install, add, and unistall components on the page.
*/
void IntroductionPage::showMaintenanceTools()
{
    showWidgets(true);
    m_label->setVisible(false);
    m_progressBar->setVisible(false);
}

/*!
    Sets \a enable to \c true to enable the options to install, add, and
    uninstall components on the page.
*/
void IntroductionPage::setMaintenanceToolsEnabled(bool enable)
{
    m_packageManager->setEnabled(enable);
    m_updateComponents->setEnabled(enable && ProductKeyCheck::instance()->hasValidKey());
    m_removeAllComponents->setEnabled(enable);
}

// -- public slots

/*!
    Displays the message \a msg on the page.
*/
void IntroductionPage::setMessage(const QString &msg)
{
    m_label->setText(msg);
}

/*!
    Updates the value of \a progress on the progress bar.
*/
void IntroductionPage::onProgressChanged(int progress)
{
    m_progressBar->setValue(progress);
}

/*!
    Sets total \a totalProgress value to progress bar.
*/
void IntroductionPage::setTotalProgress(int totalProgress)
{
    if (m_progressBar)
        m_progressBar->setRange(0, totalProgress);
}

/*!
    Displays the error message \a error on the page.
*/
void IntroductionPage::setErrorMessage(const QString &error)
{
    QPalette palette;
    const PackageManagerCore::Status s = packageManagerCore()->status();
    if (s == PackageManagerCore::Failure) {
        palette.setColor(QPalette::WindowText, Qt::red);
    } else {
        palette.setColor(QPalette::WindowText, palette.color(QPalette::WindowText));
    }

    m_errorLabel->setText(error);
    m_errorLabel->setPalette(palette);

#ifdef Q_OS_WIN
    if (m_taskButton) {
        m_taskButton->progress()->stop();
        m_taskButton->progress()->setValue(100);
    }
#endif
}

/*!
    Returns \c true if at least one valid and enabled repository is available.
*/
bool IntroductionPage::validRepositoriesAvailable() const
{
    const PackageManagerCore *const core = packageManagerCore();
    bool valid = false;

    foreach (const Repository &repo, core->settings().repositories()) {
        if (repo.isEnabled() && repo.isValid()) {
            valid = true;
            break;
        }
    }
    return valid;
}

// -- private slots

void IntroductionPage::setUpdater(bool value)
{
    if (value) {
        entering();
        gui()->showSettingsButton(true);
        packageManagerCore()->setUpdater();
        emit packageManagerCoreTypeChanged();

        gui()->updatePageListWidget();
    }
}

void IntroductionPage::setUninstaller(bool value)
{
    if (value) {
        entering();
        gui()->showSettingsButton(false);
        packageManagerCore()->setUninstaller();
        emit packageManagerCoreTypeChanged();

        gui()->updatePageListWidget();
    }
}

void IntroductionPage::setPackageManager(bool value)
{
    if (value) {
        entering();
        gui()->showSettingsButton(true);
        packageManagerCore()->setPackageManager();
        emit packageManagerCoreTypeChanged();

        gui()->updatePageListWidget();
    }
}
/*!
    Initializes the page.
*/
void IntroductionPage::initializePage()
{
    PackageManagerCore *core = packageManagerCore();
    if (core->isPackageManager()) {
        m_packageManager->setChecked(true);
    } else if (core->isUpdater()) {
        m_updateComponents->setChecked(true);
    } else if (core->isUninstaller()) {
        // If we are running maintenance tool and the default uninstaller
        // marker is not overridden, set the default checked radio button
        // based on if we have valid repositories available.
        if (!core->isUserSetBinaryMarker() && validRepositoriesAvailable()) {
            m_packageManager->setChecked(true);
        } else {
            // No repositories available, default to complete uninstallation.
            m_removeAllComponents->setChecked(true);
            core->setCompleteUninstallation(true);
        }
    }
}

/*!
    Resets the internal page state, so that on clicking \uicontrol Next the metadata needs to be
    fetched again.
*/
void IntroductionPage::onCoreNetworkSettingsChanged()
{
    m_updatesFetched = false;
    m_allPackagesFetched = false;
}

// -- private

/*!
    Initializes the page's fields.
*/
void IntroductionPage::entering()
{
    setComplete(true);
    showWidgets(false);
    setMessage(QString());
    setErrorMessage(QString());
    setButtonText(QWizard::CancelButton, tr("&Quit"));

    m_progressBar->setValue(0);
    m_progressBar->setRange(0, 0);
    PackageManagerCore *core = packageManagerCore();
    if (core->isUninstaller() || core->isMaintainer()) {
        showMaintenanceTools();
        setMaintenanceToolsEnabled(true);
    }
    setSettingsButtonRequested((!core->isOfflineOnly()) && (!core->isUninstaller()));
}

/*!
    Called when end users leave the page and the PackageManagerGui:currentPageChanged()
    signal is triggered.
*/
void IntroductionPage::leaving()
{
    m_progressBar->setValue(0);
    m_progressBar->setRange(0, 0);
    setButtonText(QWizard::CancelButton, gui()->defaultButtonText(QWizard::CancelButton));
}

/*!
    Displays widgets on the page.
*/
void IntroductionPage::showWidgets(bool show)
{
    m_label->setVisible(show);
    m_progressBar->setVisible(show);
    m_packageManager->setVisible(show);
    m_updateComponents->setVisible(show);
    m_removeAllComponents->setVisible(show);
}

/*!
    Displays the text \a text on the page.
*/
void IntroductionPage::setText(const QString &text)
{
    m_msgLabel->setText(text);
}

// -- ComponentSelectionPage

/*!
    \class QInstaller::ComponentSelectionPage
    \inmodule QtInstallerFramework
    \brief The ComponentSelectionPage class changes the checked state of
    components.
*/

/*!
    Constructs a component selection page with \a core as parent.
*/
ComponentSelectionPage::ComponentSelectionPage(PackageManagerCore *core)
    : PackageManagerPage(core)
    , d(new ComponentSelectionPagePrivate(this, core))
{
    setPixmap(QWizard::WatermarkPixmap, QPixmap());
    setObjectName(QLatin1String("ComponentSelectionPage"));
    setColoredTitle(tr("Select Components"));
}

/*!
    Destructs a component selection page.
*/
ComponentSelectionPage::~ComponentSelectionPage()
{
    delete d;
}

/*!
    Initializes the page's fields based on values from fields on previous
    pages. The text to display depends on whether the page is being used in an
    installer, updater, or uninstaller.
*/
void ComponentSelectionPage::entering()
{
    static const char *strings[] = {
        QT_TR_NOOP("Please select the components you want to update."),
        QT_TR_NOOP("Please select the components you want to install."),
        QT_TR_NOOP("Please select the components you want to uninstall."),
        QT_TR_NOOP("Select the components to install. Deselect installed components to uninstall them. Any components already installed will not be updated."),
        QT_TR_NOOP("Mandatory components need to be updated first before you can select other components to update.")
     };

    int index = 0;
    PackageManagerCore *core = packageManagerCore();
    if (core->isInstaller()) index = 1;
    if (core->isUninstaller()) index = 2;
    if (core->isPackageManager()) index = 3;
    if (core->foundEssentialUpdate() && core->isUpdater()) index = 4;
    setColoredSubTitle(tr(strings[index]));

    d->updateTreeView();

    // check component model state so we can enable needed component selection buttons
    if (core->isUpdater())
        d->onModelStateChanged(d->m_currentModel->checkedState());

    setModified(isComplete());
    if (core->settings().repositoryCategories().count() > 0 && !core->isOfflineOnly()
        && !core->isUpdater()) {
        d->showCategoryLayout(true);
        core->settings().setAllowUnstableComponents(true);
    } else {
        d->showCategoryLayout(false);
    }
    d->showCompressedRepositoryButton();
}

/*!
    Called when end users leave the page and the PackageManagerGui:currentPageChanged()
    signal is triggered.
*/
void ComponentSelectionPage::leaving()
{
    d->hideCompressedRepositoryButton();
}

/*!
    Called when the show event \a event occurs. Switching pages back and forth might restore or
    remove the checked state of certain components the end users have checked or not checked,
    because the dependencies are resolved and checked when clicking \uicontrol Next. So as not to
    confuse the end users with newly checked components they did not check, the state they left the
    page in is restored.
*/
void ComponentSelectionPage::showEvent(QShowEvent *event)
{
    // remove once we deprecate isSelected, setSelected etc...
    if (!event->spontaneous())
        packageManagerCore()->restoreCheckState();
    QWizardPage::showEvent(event);
}

/*!
    Selects all components in the component tree.
*/
void ComponentSelectionPage::selectAll()
{
    d->selectAll();
}

/*!
    Deselects all components in the component tree.
*/
void ComponentSelectionPage::deselectAll()
{
    d->deselectAll();
}

/*!
    Selects the components that have the \c <Default> element set to \c true in
    the package information file.
*/
void ComponentSelectionPage::selectDefault()
{
    if (packageManagerCore()->isInstaller())
        d->selectDefault();
}

/*!
    Selects the component with \a id in the component tree.
*/
void ComponentSelectionPage::selectComponent(const QString &id)
{
    d->m_core->selectComponent(id);
}

/*!
    Deselects the component with \a id in the component tree.
*/
void ComponentSelectionPage::deselectComponent(const QString &id)
{
    d->m_core->deselectComponent(id);
}

/*!
   Adds the possibility to install a compressed repository on component selection
   page. A new button which opens a file browser is added for compressed
   repository selection.
*/
void ComponentSelectionPage::allowCompressedRepositoryInstall()
{
    d->allowCompressedRepositoryInstall();
}

/*!
    Adds an additional virtual component with the \a name to be installed.

    Returns \c true if the virtual component is found and not installed.
*/
bool ComponentSelectionPage::addVirtualComponentToUninstall(const QString &name)
{
    PackageManagerCore *core = packageManagerCore();
    const QList<Component *> allComponents = core->components(PackageManagerCore::ComponentType::All);
    Component *component = PackageManagerCore::componentByName(
                name, allComponents);
    if (component && component->isInstalled() && component->isVirtual()) {
        component->setCheckState(Qt::Unchecked);
        core->componentsToInstallNeedsRecalculation();
        qCDebug(QInstaller::lcDeveloperBuild) << "Virtual component " << name << " was selected for uninstall by script.";
        return true;
    }
    return false;
}

void ComponentSelectionPage::setModified(bool modified)
{
    setComplete(modified);
}

/*!
    Returns \c true if at least one component is checked on the page.
*/
bool ComponentSelectionPage::isComplete() const
{
    if (packageManagerCore()->isInstaller() || packageManagerCore()->isUpdater())
        return d->m_currentModel->checked().count();

    if (d->m_currentModel->checkedState().testFlag(ComponentModel::DefaultChecked) == false)
        return true;

    const QSet<Component *> uncheckable = d->m_currentModel->uncheckable();
    for (auto &component : uncheckable) {
        if (component->forcedInstallation() && !component->isInstalled())
            return true; // allow installation for new forced components
    }
    return false;
}

// -- PerformInstallationPage

/*!
    \class QInstaller::PerformInstallationPage
    \inmodule QtInstallerFramework
    \brief The PerformInstallationPage class shows progress information about the installation state.

    This class is a container for the PerformInstallationForm class, which
    constructs the actual UI for the page.
*/

/*!
    \fn QInstaller::PerformInstallationPage::isInterruptible() const

    Returns \c true if the installation can be interrupted.
*/

/*!
    \fn QInstaller::PerformInstallationPage::setAutomatedPageSwitchEnabled(bool request)

    Enables automatic switching of pages when \a request is \c true.
*/

/*!
    Constructs a perform installation page with \a core as parent. The page
    contains a PerformInstallationForm that defines the UI for the page.
*/
PerformInstallationPage::PerformInstallationPage(PackageManagerCore *core)
    : PackageManagerPage(core)
    , m_performInstallationForm(new PerformInstallationForm(core->isInstaller(), this))
{
    setPixmap(QWizard::WatermarkPixmap, QPixmap());
    setObjectName(QLatin1String("PerformInstallationPage"));
    updatePageListTitle();

    QHBoxLayout* pBackGroundLayout = nullptr;
    if (core->isInstaller()) {
        pBackGroundLayout = new QHBoxLayout();
        pBackGroundLayout->setContentsMargins(0, 0, 0, 0);
        pBackGroundLayout->addItem(new QSpacerItem(0, 426, QSizePolicy::Ignored, QSizePolicy::Expanding));

        QWidget* background = new QLabel(this);
        background->setStyleSheet(QLatin1String("border-image: url(:/install_face.png);"));
       
        pBackGroundLayout->addWidget(background);
    }

    QHBoxLayout* pProgressLayout = new QHBoxLayout();
    pProgressLayout->setContentsMargins(0, 0, 0, 0);
    {
        QWidget* widget = new QWidget(this);
        widget->setFixedHeight(core->isInstaller() ? 92: 98);
        m_performInstallationForm->setupUi(widget);

        pProgressLayout->addWidget(widget);
    }

    QVBoxLayout* mainlayout = new QVBoxLayout(this);
    mainlayout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, kShadowLen);

    auto customTitle = new CustomTitle(this);
    customTitle->setObjectName(QLatin1String("CustomTitle"));
    customTitle->setButtonVisible(CustomTitle::CloseButton, false);
    customTitle->setFixedHeight(32);

    mainlayout->addWidget(customTitle);
   
    if (core->isInstaller()) {
        mainlayout->addSpacing(-8);
        customTitle->setTitle(tr("Installation guide"));

        mainlayout->addLayout(pBackGroundLayout);
        mainlayout->addStretch();
        mainlayout->addSpacing(26);
    }
    mainlayout->addLayout(pProgressLayout);
    mainlayout->addStretch();

    m_imageChangeTimer.setInterval(10000);

    connect(m_performInstallationForm, &PerformInstallationForm::showDetailsChanged,
            this, &PerformInstallationPage::toggleDetailsWereChanged);

    connect(core, &PackageManagerCore::installationStarted,
            this, &PerformInstallationPage::installationStarted);
    connect(core, &PackageManagerCore::installationFinished,
            this, &PerformInstallationPage::installationFinished);

    connect(core, &PackageManagerCore::uninstallationStarted,
            this, &PerformInstallationPage::uninstallationStarted);
    connect(core, &PackageManagerCore::uninstallationFinished,
            this, &PerformInstallationPage::uninstallationFinished);

    connect(core, &PackageManagerCore::titleMessageChanged,
            this, &PerformInstallationPage::setTitleMessage);
    connect(this, &PerformInstallationPage::setAutomatedPageSwitchEnabled,
            core, &PackageManagerCore::setAutomatedPageSwitchEnabled);

    connect(core, &PackageManagerCore::installerBinaryMarkerChanged,
            this, &PerformInstallationPage::updatePageListTitle);

    connect(&m_imageChangeTimer, &QTimer::timeout,
            this, &PerformInstallationPage::changeCurrentImage);

    setCommitPage(true);
}

/*!
    Destructs a perform installation page.
*/
PerformInstallationPage::~PerformInstallationPage()
{
    delete m_performInstallationForm;
}

/*!
    Returns \c true if automatically switching to the page is requested.
*/
bool PerformInstallationPage::isAutoSwitching() const
{
    return true;
}

int PerformInstallationPage::nextId() const
{
    if(packageManagerCore()->status() == PackageManagerCore::Failure){
        return PackageManagerCore::PesError;
    } else {
        return PackageManagerCore::PesFinished;
    }
}

// -- protected

/*!
    Initializes the page's fields based on values from fields on previous
    pages. The text to display depends on whether the page is being used in an
    installer, updater, or uninstaller.
*/
void PerformInstallationPage::entering()
{
    setComplete(false);

    if (packageManagerCore()->isInstaller()) {
        QString targetDir = packageManagerCore()->value(QLatin1String("TargetDir"));

        QSettings setting(QSettings::NativeFormat, QSettings::UserScope,
            packageManagerCore()->value(scPublisher),
            packageManagerCore()->value(scName));

        QString ConfigTargetDir(targetDir);
        ConfigTargetDir = ConfigTargetDir.mid(0, ConfigTargetDir.lastIndexOf(QLatin1String("/")));
        ConfigTargetDir = ConfigTargetDir.mid(0, ConfigTargetDir.lastIndexOf(QLatin1String("/")));
        setting.setValue(QLatin1String(kConfigSetupName), ConfigTargetDir);
        setting.sync();

        QString version = packageManagerCore()->value(QLatin1String("Version"));
        targetDir = targetDir + QDir::separator() + version;
        packageManagerCore()->setValue(QLatin1String("TargetDir"), targetDir);
    }

    emit setAutomatedPageSwitchEnabled(true);

    changeCurrentImage();
    // No need to start the timer if we only have one, or no images
    if (packageManagerCore()->settings().productImages().count() > 1)
        m_imageChangeTimer.start();

    if (LoggingHandler::instance().isVerbose()) {
        
    }
    if (packageManagerCore()->isUninstaller()) {
        setButtonText(QWizard::CommitButton, tr("U&ninstall"));
        setColoredTitle(tr("Uninstalling %1").arg(productName()));

        QTimer::singleShot(30, packageManagerCore(), SLOT(runUninstaller()));
    } else if (packageManagerCore()->isMaintainer()) {
        setButtonText(QWizard::CommitButton, tr("&Update"));
        setColoredTitle(tr("Updating components of %1").arg(productName()));

        QTimer::singleShot(30, packageManagerCore(), SLOT(runPackageUpdater()));
    } else {
        setButtonText(QWizard::CommitButton, tr("&Install"));
        setColoredTitle(tr("Installing %1").arg(productName()));

        QTimer::singleShot(30, packageManagerCore(), SLOT(runInstaller()));
    }
}

/*!
    Called when end users leave the page and the PackageManagerGui:currentPageChanged()
    signal is triggered.
*/
void PerformInstallationPage::leaving()
{
    QSettings setting(QSettings::NativeFormat, QSettings::UserScope,
                      packageManagerCore()->value(scPublisher),
                      packageManagerCore()->value(scName));
    if (!packageManagerCore()->isInstaller()) {
        setting.remove(QLatin1String(kConfigSetupName));
    }

    setButtonText(QWizard::CommitButton, gui()->defaultButtonText(QWizard::CommitButton));
    m_imageChangeTimer.stop();
}

/*!
    Updates page list title based on installer binary type.
*/
void PerformInstallationPage::updatePageListTitle()
{
    PackageManagerCore *core = packageManagerCore();
    if (core->isInstaller())
        setPageListTitle(tr("Installing"));
    else if (core->isMaintainer())
        setPageListTitle(tr("Updating"));
    else if (core->isUninstaller())
        setPageListTitle(tr("Uninstalling"));
}

// -- public slots

/*!
    Sets \a title as the title of the perform installation page.
*/
void PerformInstallationPage::setTitleMessage(const QString &title)
{
    setColoredTitle(title);
}

/*!
    Changes the currently shown product image to the next available
    image from installer configuration.
*/
void PerformInstallationPage::changeCurrentImage()
{
    const QStringList productImages = packageManagerCore()->settings().productImages();
    if (productImages.isEmpty())
        return;

    const QString nextImage = (m_currentImage.isEmpty() || m_currentImage == productImages.last())
       ? productImages.first()
       : productImages.at(productImages.indexOf(m_currentImage) + 1);

    // Do not update the pixmap if there was only one image available
    if (nextImage != m_currentImage) {
        m_performInstallationForm->setImageFromFileName(nextImage);
        m_currentImage = nextImage;
    }
}

// -- private slots

void PerformInstallationPage::installationStarted()
{
    m_performInstallationForm->startUpdateProgress();

    QString ver_value = packageManagerCore()->value(QLatin1String("Version"));
    m_performInstallationForm->setMessage(tr("Installing PES %1, do not turn off the software or power").arg(ver_value));
}

void PerformInstallationPage::installationFinished()
{
    m_performInstallationForm->stopUpdateProgress();
    if (!isAutoSwitching()) {
        setComplete(true);
        setButtonText(QWizard::CommitButton, gui()->defaultButtonText(QWizard::NextButton));
    }
}

void PerformInstallationPage::uninstallationStarted()
{
    m_performInstallationForm->startUpdateProgress();
    m_performInstallationForm->setMessage(tr("Uninstalling PES..."));
    if (QAbstractButton *cancel = gui()->button(QWizard::CancelButton))
        cancel->setEnabled(false);
}

void PerformInstallationPage::uninstallationFinished()
{
    installationFinished();
    if (QAbstractButton *cancel = gui()->button(QWizard::CancelButton))
        cancel->setEnabled(false);
}

void PerformInstallationPage::toggleDetailsWereChanged()
{
    emit setAutomatedPageSwitchEnabled(isAutoSwitching());
}


// -- FinishedPage

/*!
    \class QInstaller::FinishedPage
    \inmodule QtInstallerFramework
    \brief The FinishedPage class completes the installation wizard.

    You can add the option to open the installed application to the page.
*/

/*!
    Constructs an installation finished page with \a core as parent.
*/
FinishedPage::FinishedPage(PackageManagerCore *core)
    : PackageManagerPage(core)
    , m_commitButton(nullptr)
{
    setObjectName(QLatin1String("FinishedPage"));
    setColoredTitle(tr("Completing the %1 Wizard").arg(productName()));
    setPageListTitle(tr("Finished"));

    m_msgLabel = new QLabel(this);
    m_msgLabel->setWordWrap(true);
    m_msgLabel->setObjectName(QLatin1String("MessageLabel"));

    m_runItCheckBox = new QCheckBox(this);
    m_runItCheckBox->setObjectName(QLatin1String("RunItCheckBox"));
    m_runItCheckBox->setChecked(true);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_msgLabel);
    layout->addWidget(m_runItCheckBox);
    setLayout(layout);

    setCommitPage(true);
}

/*!
    Initializes the page's fields based on values from fields on previous
    pages.
*/
void FinishedPage::entering()
{
    m_msgLabel->setText(tr("Click %1 to exit the %2 Wizard.")
                        .arg(gui()->defaultButtonText(QWizard::FinishButton).remove(QLatin1Char('&')))
                        .arg(productName()));

    if (m_commitButton) {
        disconnect(m_commitButton, &QAbstractButton::clicked, this, &FinishedPage::handleFinishClicked);
        m_commitButton = nullptr;
    }

    if (packageManagerCore()->isMaintainer()) {
#ifdef Q_OS_MACOS
        gui()->setOption(QWizard::NoCancelButton, false);
#endif
        if (QAbstractButton *cancel = gui()->button(QWizard::CancelButton)) {
            m_commitButton = cancel;
            cancel->setEnabled(true);
            cancel->setVisible(true);
            // we don't use the usual FinishButton so we need to connect the misused CancelButton
            connect(cancel, &QAbstractButton::clicked, gui(), &PackageManagerGui::finishButtonClicked);
            connect(cancel, &QAbstractButton::clicked, packageManagerCore(), &PackageManagerCore::finishButtonClicked);
            // for the moment we don't want the rejected signal connected
            disconnect(gui(), &QDialog::rejected, packageManagerCore(), &PackageManagerCore::setCanceled);

            connect(gui()->button(QWizard::CommitButton), &QAbstractButton::clicked,
                    this, &FinishedPage::cleanupChangedConnects);
        }
        setButtonText(QWizard::CommitButton, tr("Restart"));
        setButtonText(QWizard::CancelButton, gui()->defaultButtonText(QWizard::FinishButton));
    } else {
        if (packageManagerCore()->isInstaller()) {
            m_commitButton = wizard()->button(QWizard::FinishButton);
            if (QPushButton *const b = qobject_cast<QPushButton *>(m_commitButton))
                b->setDefault(true);
        }

        gui()->setOption(QWizard::NoCancelButton, true);
        if (QAbstractButton *cancel = gui()->button(QWizard::CancelButton))
            cancel->setVisible(false);
    }

    gui()->updateButtonLayout();

    if (m_commitButton) {
        disconnect(m_commitButton, &QAbstractButton::clicked, this, &FinishedPage::handleFinishClicked);
        connect(m_commitButton, &QAbstractButton::clicked, this, &FinishedPage::handleFinishClicked);
    }

    if (packageManagerCore()->status() == PackageManagerCore::Success) {
        const QString finishedText = packageManagerCore()->value(QLatin1String("FinishedText"));
        if (!finishedText.isEmpty())
            m_msgLabel->setText(finishedText);

        if (!packageManagerCore()->isUninstaller() && !packageManagerCore()->value(scRunProgram)
            .isEmpty()) {
                m_runItCheckBox->show();
                m_runItCheckBox->setText(packageManagerCore()->value(scRunProgramDescription,
                    tr("Run %1 now.")).arg(productName()));
            return; // job done
        }
    } else {
        // TODO: how to handle this using the config.xml
        setColoredTitle(tr("The %1 Wizard failed.").arg(productName()));
    }

    m_runItCheckBox->hide();
    m_runItCheckBox->setChecked(false);
}

/*!
    Called when end users leave the page and the PackageManagerGui:currentPageChanged()
    signal is triggered.
*/
void FinishedPage::leaving()
{
#ifdef Q_OS_MACOS
    gui()->setOption(QWizard::NoCancelButton, true);
#endif

    if (QAbstractButton *cancel = gui()->button(QWizard::CancelButton))
        cancel->setVisible(false);
    gui()->updateButtonLayout();

    setButtonText(QWizard::CommitButton, gui()->defaultButtonText(QWizard::CommitButton));
    setButtonText(QWizard::CancelButton, gui()->defaultButtonText(QWizard::CancelButton));
}

/*!
    Performs the necessary operations when end users select the \uicontrol Finish
    button.
*/
void FinishedPage::handleFinishClicked()
{
    const QString program =
        packageManagerCore()->replaceVariables(packageManagerCore()->value(scRunProgram));

    const QStringList args = packageManagerCore()->replaceVariables(packageManagerCore()
        ->values(scRunProgramArguments));
    if (!m_runItCheckBox->isChecked() || program.isEmpty())
        return;

    qCDebug(QInstaller::lcInstallerInstallLog) << "starting" << program << args;
    QProcess::startDetached(program, args);
}

/*!
    Removes changed connects from the page.
*/
void FinishedPage::cleanupChangedConnects()
{
    if (QAbstractButton *cancel = gui()->button(QWizard::CancelButton)) {
        // remove the workaround connect from entering page
        disconnect(cancel, &QAbstractButton::clicked, gui(), &PackageManagerGui::finishButtonClicked);
        disconnect(cancel, &QAbstractButton::clicked, packageManagerCore(), &PackageManagerCore::finishButtonClicked);
        connect(gui(), &QDialog::rejected, packageManagerCore(), &PackageManagerCore::setCanceled);

        disconnect(gui()->button(QWizard::CommitButton), &QAbstractButton::clicked,
                   this, &FinishedPage::cleanupChangedConnects);
    }
}

// -- RestartPage

/*!
    \class QInstaller::RestartPage
    \inmodule QtInstallerFramework
    \brief The RestartPage class enables restarting the installer.

    The restart installation page enables end users to restart the wizard.
    This is useful, for example, if the maintenance tool itself needs to be
    updated before updating the application components. When updating is done,
    end users can select \uicontrol Restart to start the maintenance tool.
*/

/*!
    \fn QInstaller::RestartPage::restart()

    This signal is emitted when the installer is restarted.
*/

/*!
    Constructs a restart installation page with \a core as parent.
*/
RestartPage::RestartPage(PackageManagerCore *core)
    : PackageManagerPage(core)
{
    setObjectName(QLatin1String("RestartPage"));
    setColoredTitle(tr("Completing the %1 Setup Wizard").arg(productName()));

    // Never show this page on the page list
    setShowOnPageList(false);
    setFinalPage(false);
}

/*!
    Returns the introduction page.
*/
int RestartPage::nextId() const
{
    return PackageManagerCore::Introduction;
}

/*!
    Initializes the page's fields based on values from fields on previous
    pages.
*/
void RestartPage::entering()
{
    if (!packageManagerCore()->needsHardRestart()) {
        if (QAbstractButton *finish = wizard()->button(QWizard::FinishButton))
            finish->setVisible(false);
        QMetaObject::invokeMethod(this, "restart", Qt::QueuedConnection);
    } else {
        gui()->accept();
    }
}

/*!
    Called when end users leave the page and the PackageManagerGui:currentPageChanged()
    signal is triggered.
*/
void RestartPage::leaving()
{
}

PesHomePage::PesHomePage(PackageManagerCore *core, PesLicenceInfo* info)
    : PackageManagerPage(core)
    , m_all_packages_fetched(false)
    , background_widget_(new QWidget)
    , welcom_label_(new QLabel)
    , introduce_label_(new QLabel)
    , install_button_(new QPushButton)
    , space_label_(new QLabel)
    , warning_button_(new QPushButton)
    , licence_check_box_(new QCheckBox)
    , user_service_btn_(new QPushButton)
    , user_privacy_btn_(new QPushButton)
    , licence_info_(info)
{
    QVBoxLayout* mid_layout = new QVBoxLayout();
    mid_layout->setContentsMargins(0, 0, 0, 0);
    {
        background_widget_->setObjectName(QLatin1String("peshomepagebackground"));

        welcom_label_->setText(tr("Welcome to install PES"));
        welcom_label_->setObjectName(QLatin1String("WelcomLabel"));

        introduce_label_->setText(tr("Just for you to better use the computer"));
        introduce_label_->setObjectName(QLatin1String("IntroduceLabel"));

        QVBoxLayout* background_widget_layout_ = new QVBoxLayout();

        background_widget_layout_->setContentsMargins(32, 0, 0, 0);
        background_widget_layout_->addSpacing(72);
        background_widget_layout_->addWidget(welcom_label_);
        background_widget_layout_->addSpacing(4);
        background_widget_layout_->addWidget(introduce_label_);
        background_widget_layout_->addSpacerItem(new QSpacerItem(0, 203, QSizePolicy::Ignored, QSizePolicy::Expanding));
        background_widget_->setLayout(background_widget_layout_);

        mid_layout->addWidget(background_widget_);
        /*
        QLabel* space = new QLabel();
        space->setFixedSize(800, 6);
        space->setStyleSheet(QLatin1String("border-image: url(:/space.png)"));

        mid_layout->addSpacing(-6);
        mid_layout->addWidget(space);*/
    }

    QVBoxLayout* dir_choose_layout = new QVBoxLayout();
    dir_choose_layout->setContentsMargins(32, 10, 32, 0);
    dir_choose_layout->setAlignment(Qt::AlignLeft);
    {
        dir_choose_label_ = new QLabel(this);
        dir_choose_label_->setText(tr("Please select an installation directory:"));
       
        // file select browse
//        QSettings last_setting(QSettings::NativeFormat, QSettings::UserScope,
//            packageManagerCore()->value(scPublisher),
//            packageManagerCore()->value(scName));

//        target_dir_ = last_setting.value(QLatin1String(kConfigSetupName)).toString();
        if (target_dir_.isEmpty() || !QDir(target_dir_).exists()) {
            target_dir_ = packageManagerCore()->value(scTargetDir);
        }
            
        target_dir_ = QDir::toNativeSeparators(QDir(target_dir_).absolutePath());

        dir_text_ = new QLineEdit(this);
        dir_text_->setText(target_dir_);
        dir_text_->setEnabled(false);
        dir_text_->setObjectName(QLatin1String("chooseDirText"));

        dir_choose_button_ = new QPushButton(this);
        dir_choose_button_->setText(tr("Browse"));
        dir_choose_button_->setObjectName(QLatin1String("chooseDirButton"));

        QHBoxLayout* dir_layout = new QHBoxLayout;
        dir_layout->setSpacing(0);
        dir_layout->setAlignment(Qt::AlignLeft);
        dir_layout->addWidget(dir_text_);
        dir_layout->addSpacing(-1);
        dir_layout->addWidget(dir_choose_button_);

       
        dir_choose_layout->addWidget(dir_choose_label_);
        dir_choose_layout->addLayout(dir_layout);
    }
    
    // space label
    QString htmlOutput;
    bool componentsOk = packageManagerCore()->calculateComponents(&htmlOutput);
    setComplete(componentsOk);
    space_label_->setObjectName(QLatin1String("spaceLabel"));

    QIcon icon(QPixmap(QLatin1String(":/worningIcon.png")));
    warning_button_->setIcon(icon);
    warning_button_->setIconSize(QSize(16, 16));
    warning_button_->setObjectName(QLatin1String("worningbutton"));
    warning_button_->setText(tr("Not enough space"));
    warning_button_->adjustSize();

    QHBoxLayout* space_layout = new QHBoxLayout();
    space_layout->setContentsMargins(0, 0, 0, 0);
    space_layout->setAlignment(Qt::AlignLeft);
    space_layout->addWidget(space_label_);
    space_layout->addSpacing(8);
    space_layout->addWidget(warning_button_);

    dir_choose_layout->addLayout(space_layout);

    mid_layout->addLayout(dir_choose_layout);

    QHBoxLayout* licence_layout = new QHBoxLayout;
    licence_layout->setMargin(0);
    licence_layout->setSpacing(0);
    licence_layout->addSpacing(32);
    {
        // user_service  and install_button
        user_service_btn_->setText(tr("User Services Agreement"));
        user_service_btn_->setObjectName(QLatin1String("userServiceButton"));
        QFont fs(user_service_btn_->font());
        fs.setUnderline(true);
        user_service_btn_->setFont(fs);

        // user_privacy
        user_privacy_btn_->setObjectName(QLatin1String("userPrivacyButton"));
        user_privacy_btn_->setText(tr("Privacy Policy"));
        QFont fp(user_privacy_btn_->font());
        fp.setUnderline(true);
        user_privacy_btn_->setFont(fp);

        licence_check_box_->setText(tr("read and agree "));

        install_button_->setText(tr("Install Now"));
        install_button_->setFixedSize(109, 45);
        install_button_->setObjectName(QLatin1String("startButton"));

        licence_layout->addWidget(licence_check_box_);
        licence_layout->addWidget(user_service_btn_);
        licence_layout->addSpacing(1);
        licence_layout->addWidget(user_privacy_btn_);
        licence_layout->addItem(new QSpacerItem(388, 0, QSizePolicy::Expanding, QSizePolicy::Ignored));;
        licence_layout->addWidget(install_button_);
        licence_layout->addSpacing(32);
    }
    
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, kShadowLen);

    auto customTitle = new CustomTitle(this);
    customTitle->setObjectName(QLatin1String("CustomTitle"));
    customTitle->setTitle(tr("Installation guide"));
    customTitle->setFixedHeight(32);

    main_layout->addWidget(customTitle);
    main_layout->addSpacing(-8);
    main_layout->addLayout(mid_layout);
    main_layout->addLayout(licence_layout);
    main_layout->addSpacing(26);
    
    connectAll();

    setLicenceAgreed(true);

    tool_tip_ = new PesToolTip(this);
    tool_tip_->setGeometry(208, 58, 440, 48);
    tool_tip_->setVisible(false);
}

void PesHomePage::setLicenceAgreed(bool agree)
{
    licence_info_->is_licence_agreed = agree;
}

void PesHomePage::initializePage()
{
    if (!packageManagerCore()->checkEnv()) {
        return;
    }

    bool bGpuExist = packageManagerCore()->checkGpuExists();
    if (!bGpuExist) {
        if (gui()->isSilent()) {
            qApp->exit(QInstaller::PackageManagerCore::GpuNotExist);
            return;
        }
        PesEnvDetectMessageBox* messageBox = new PesEnvDetectMessageBox(MessageBoxHandler::currentBestSuitParent(), tr("MT card not detected and cannot be installed"));
        int ret = messageBox->exec();
        if ((ret == QMessageBox::Cancel) | (ret == QMessageBox::Close)) {
            qApp->exit(0);
        }
        else if(ret == QMessageBox::Ok) {
            initializePage();
        }
    }

#ifdef Q_OS_WIN
    typedef void(__stdcall*NTPROC)(DWORD*, DWORD*, DWORD*);
    HINSTANCE hinst = LoadLibrary(TEXT("ntdll.dll"));
    NTPROC GetNtVersionNumbers = (NTPROC)GetProcAddress(hinst, "RtlGetNtVersionNumbers");
    DWORD dwMajor, dwMinor, dwBuildNumber;
    GetNtVersionNumbers(&dwMajor, &dwMinor, &dwBuildNumber);
    if(dwMajor < 10){
        QWidget *p = MessageBoxHandler::currentBestSuitParent();
        if (p != nullptr) {
            p->hide();
        }
        if (gui()->isSilent()) {
            qApp->exit(QInstaller::PackageManagerCore::SystemNotSupport);
            return;
        }
        PesEnvDetectMessageBox *messageBox = new PesEnvDetectMessageBox(MessageBoxHandler::currentBestSuitParent(), tr("You need at least Windows 10, Version Not Supported"));
        int ret = messageBox->exec();
        if ((ret == QMessageBox::Cancel) | (ret == QMessageBox::Close)) {
            qApp->exit(0);
        }
        else if(ret == QMessageBox::Ok) {
            initializePage();
        }
    }

    QString path = QCoreApplication::applicationDirPath() + QLatin1String("/pes_resizebar_temp");
    QDir dir(path);
    if (dir.exists() || dir.mkdir(path))
    {
        QFile::copy(QLatin1String(":/resizebar/didriver64.sys"), path + QLatin1String("/didriver64.sys"));
        QFile::copy(QLatin1String(":/resizebar/pciutil64.dll"), path + QLatin1String("/pciutil64.dll"));
        QFile::copy(QLatin1String(":/resizebar/resizebar_detect.exe"), path + QLatin1String("/resizebar_detect.exe"));

        std::shared_ptr<QProcess> process = std::make_shared<QProcess>(new QProcess(this));
        int flag = process->execute(path + QLatin1String("/resizebar_detect.exe"));

        if (flag != 0)
        {
            QWidget *p = MessageBoxHandler::currentBestSuitParent();
            if (p != nullptr) {
                p->hide();
            }
            if (gui()->isSilent()) {
                qApp->exit(QInstaller::PackageManagerCore::GpuNotExist);
                dir.removeRecursively();
                return;
            }
            PesEnvDetectMessageBox* messageBox = new PesEnvDetectMessageBox(MessageBoxHandler::currentBestSuitParent(), tr("resizebar check error, error code: %1").arg(int(flag)));
            int ret = messageBox->exec();
            if ((ret == QMessageBox::Cancel) | (ret == QMessageBox::Close)) {
                dir.removeRecursively();
                qApp->exit(0);
            }
            else if (ret == QMessageBox::Ok) {
                initializePage();
            }
        }
        else {
            dir.removeRecursively();
        }
    }
#endif
}

int PesHomePage::nextId() const
{
    if (isSilent()) {
        return PackageManagerCore::End;
    }

    if (!licence_info_->is_licence_agreed || force_to_licence_page_) {
        return PackageManagerCore::PesLicence;
    } else {
        return PackageManagerCore::PesInstallation;
    }
}

void PesHomePage::entering()
{
    if (isSilent()) return;

    licence_check_box_->setChecked(licence_info_->is_licence_agreed);
    need_space_ = packageManagerCore()->requiredDiskSpace();

    target_dir_= QDir::toNativeSeparators(QDir(target_dir_).absolutePath());

    checkCanInstall(target_dir_);

    QString dirTxt = target_dir_;
    QString publisher = packageManagerCore()->value(scPublisher);
    QString title = packageManagerCore()->value(scTitle);
    QString base_path = QDir::separator() + publisher + QDir::separator() + title;
    if (!dirTxt.contains(base_path))
    {
        dirTxt = dirTxt + base_path;
    }

    dir_text_->setText(dirTxt);
}

void PesHomePage::connectAll()
{
    connect(install_button_, &QAbstractButton::clicked, this, &PesHomePage::startInstall);
    connect(dir_choose_button_, &QAbstractButton::clicked, this, &PesHomePage::chooseDirectory);
    connect(licence_check_box_, &QAbstractButton::toggled, this, &PesHomePage::setLicenceAgreed);
    connect(licence_check_box_, &QAbstractButton::toggled, this, &PesHomePage::updateButtonBackground);
    connect(user_service_btn_, &QAbstractButton::clicked, this, &PesHomePage::showUserService);
    connect(user_privacy_btn_, &QAbstractButton::clicked, this, &PesHomePage::showUserPrivacy);
}

void PesHomePage::startInstall()
{
    force_to_licence_page_ = false;
    if(dir_text_->text().isEmpty()) {
        return;
    }
    if(!licence_info_->is_licence_agreed) {
        PesLicenMessageBox* pLicenseMsgBox = new PesLicenMessageBox(MessageBoxHandler::currentBestSuitParent());
        int ret = pLicenseMsgBox->exec();
        if(ret == QMessageBox::Ok) {
            licence_check_box_->setChecked(true);
        }
        return;
    }

    bool bCanInstall = checkCanInstall(target_dir_);
    if (!bCanInstall) {
        return;
    }

    QList<ProcessInfo> pesProcessInfo;
    QList<ProcessInfo> processInfos = runningProcesses();
    Q_FOREACH(const ProcessInfo & item, processInfos) {
        if (item.name.contains(QLatin1String("pes_gui.exe"))) {
            pesProcessInfo.push_back(item);
        }
    }

    if (!pesProcessInfo.isEmpty()) {
        ProcessDetectMessageBox* pMsgBox = new ProcessDetectMessageBox(MessageBoxHandler::currentBestSuitParent());
        int ret = pMsgBox->exec();

        if (ret == QMessageBox::Ok) {
            Q_FOREACH(const ProcessInfo& item, pesProcessInfo) {
                killProcess(item, 100);
            }
            startInstall();
            return;
        }
        else if (ret == QMessageBox::Retry) {
            startInstall();
            return;
        }
        else {
            return;
        }
    }
    
    packageManagerCore()->setValue(scTargetDir, dir_text_->text());
    wizard()->next();
}

void PesHomePage::showUserService()
{
    force_to_licence_page_ = true;
    licence_info_->licence_type = PesLicenceInfo::UserService;
    wizard()->next();
}

void PesHomePage::showUserPrivacy()
{
    force_to_licence_page_ = true;
    licence_info_->licence_type = PesLicenceInfo::UserPrivacy;
    wizard()->next();
}

void PesHomePage::chooseDirectory()
{
    QString target_dir = packageManagerCore()->value(scTargetDir);
    target_dir = QDir::toNativeSeparators(QDir(target_dir).absolutePath());
    QString dir_name = QFileDialog::getExistingDirectory(this, tr("Open Directory"),
                                                         target_dir,
                                                         QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir_name.isEmpty()) {
        target_dir_ = dir_name;

        QString publisher = packageManagerCore()->value(scPublisher);
        QString title = packageManagerCore()->value(scTitle);
        dir_name = dir_name + QDir::separator() + publisher + QDir::separator() + title;
        dir_name = QDir::toNativeSeparators(QDir(dir_name).absolutePath());

        dir_text_->setText(dir_name);

        checkCanInstall(target_dir_);
    }
}

bool PesHomePage::setSpaceMessage(quint64 need_space, quint64 available_space)
{
    if(need_space >= available_space) {
        space_label_->setText(tr("Space required: %1   Available space: %2")
                                  .arg(humanReadableSize(need_space))
                                  .arg(humanReadableSize(available_space)) );
        warning_button_->setVisible(true);
        warning_button_->setText(tr("Not enough space"));
        dir_text_->setStyleSheet(QLatin1String("color: rgb(255, 103, 29);"));

        tool_tip_->setMessage(tr("There is not enough disk space, please select again"));
        tool_tip_->start();
        return false;
    } else{
        space_label_->setText(tr("Space required: %1   Available space: %2")
                                  .arg(humanReadableSize(need_space))
                                  .arg(humanReadableSize(available_space)) );
        warning_button_->setVisible(false);
        dir_text_->setStyleSheet(QLatin1String("color: rgb(51, 51, 51);"));
        tool_tip_->stop();
        return true;
    }
}

void PesHomePage::updateButtonBackground(bool agree)
{
    if(agree) {
        install_button_->setStyleSheet(
            QLatin1String("QPushButton{ \
                    background-color: rgb(255, 103, 29);\
                    border: none;\
                    border-radius: 4px;\
                    font-weight: 700;\
                    font-size: 13px;\
                }\
                QPushButton:hover{\
                     background-color: rgb(255, 118, 52);}\
                QPushButton:pressed {\
                    background-color: rgb(240, 88, 14);}"));
    } else {
        install_button_->setStyleSheet(
            QLatin1String("QPushButton{ \
                    background-color: rgba(255, 103, 29, 0.5);\
                    border: none;\
                    border-radius: 4px;\
                    font-weight: 700;\
                    font-size: 13px;\
                };"));
    }
}

bool QInstaller::PesHomePage::checkDirWritable(const QString& dirPath)
{
	bool writable = true;
#ifdef WIN32
    QString winDirPath = QDir::toNativeSeparators(dirPath);
    const std::wstring dirPath_ = winDirPath.toStdWString();
    HANDLE hDir = CreateFile(dirPath_.c_str(), FILE_TRAVERSE | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);

    if (hDir != INVALID_HANDLE_VALUE) CloseHandle(hDir);
    writable = (hDir != INVALID_HANDLE_VALUE);
#else
    QFileInfo file_info(dirPath);
    writable = file_info.isWritable();
#endif

	if (writable) {
        qCInfo(QInstaller::lcInstallerInstallLog) << dirPath << "is writable";
    }
    else {
        qCWarning(QInstaller::lcInstallerInstallLog) << dirPath << "not writable" << "error code: " << GetLastError();
    }
    return writable;
}

bool QInstaller::PesHomePage::checkCanInstall(const QString& dirPath)
{
    bool bCanInstall = false;
    quint64 availabe_size = VolumeInfo::fromPath(dirPath).availableSize();
    if (setSpaceMessage(need_space_, availabe_size)) {
        if (!checkDirWritable(dirPath)) {
            warning_button_->setVisible(true);
            warning_button_->setText(tr("Path permissions Not Open"));

            tool_tip_->setMessage(tr("This Path permissions Not Open"));
            tool_tip_->start();
        }
        else {
            tool_tip_->stop();
            bCanInstall = true;
        }
    }
    return bCanInstall;
}

PesLicencePage::PesLicencePage(PackageManagerCore *core, PesLicenceInfo* info)
    : PackageManagerPage(core)
    , back_button_(new QPushButton)
    , licence_info_(info)
{
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, 27 + kShadowLen);
    
    auto customTitle = new CustomTitle(this);
    customTitle->setObjectName(QLatin1String("CustomTitle"));
    customTitle->setTitle(tr("Installation guide"));

    main_layout->addWidget(customTitle);
    main_layout->addSpacing(-8);

    text_edit_ = new QTextEdit(this);
    text_edit_->setContextMenuPolicy(Qt::NoContextMenu);
    text_edit_->setReadOnly(true);
    text_edit_->setObjectName(QLatin1String("licensetext"));
    text_edit_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    text_edit_->setFixedHeight(446);

    text_edit_->setStyleSheet(QLatin1String("QTextEdit {margin-right:6px, color: #F5F5F5;}"));

    text_edit_->verticalScrollBar()->setStyleSheet(QLatin1String(
        "QScrollArea {background-color: #F5F5F5;}" \
        "QScrollBar:vertical{border: none; width: 6px; background-color:transparent; border-radius:37px;}"\
        "QScrollBar::handle:vertical{background-color:rgba(64, 65, 71, 0.2); width: 6px; border-radius:37px;}"
        "QScrollBar::add-page:Vertical, QScrollBar::sub-page:Vertical{ background: #F5F5F5; border-radius:37px;}"
        "QScrollBar::sub-line:vertical, QScrollBar::add-line:vertical { height: 0px; border-radius:37px; }"));

    back_button_->setObjectName(QLatin1String("licenseBackbutton"));
    back_button_->setText(tr("Back"));

    main_layout->addWidget(text_edit_);
    main_layout->addSpacing(27);
    main_layout->addWidget(back_button_, 0, Qt::AlignCenter);
    main_layout->addStretch();

    connect(back_button_, &QAbstractButton::clicked, this, &PesLicencePage::backeButtonClicked);
}

void PesLicencePage::entering()
{
    packageManagerCore()->calculateComponentsToInstall();
    foreach (QInstaller::Component *component, packageManagerCore()->orderedComponentsToInstall())
        packageManagerCore()->addLicenseItem(component->licenses());

    QHash<QString, QMap<QString, QString>> priorityHash = packageManagerCore()->sortedLicenses();

    QStringList priorities = priorityHash.keys();
    if(priorities.length() == 0) {
        qCWarning(QInstaller::lcDeveloperBuild) << "no licence find";
    }
    if(priorities.length() > 1)
        qCInfo(QInstaller::lcDeveloperBuild) << "licence have more than one, use first";

    QString priority = priorities.first();
    QMap<QString, QString> licenses = priorityHash.value(priority);
    QString license_txt;

    if (licence_info_->licence_type == PesLicenceInfo::UserService) {
        license_txt = licenses.value(QLatin1String("UserService"));
    }
    else {
        license_txt = licenses.value(QLatin1String("UserPrivacy"));
    }
    text_edit_->setText(license_txt);

    packageManagerCore()->clearLicenses();
}

void PesLicencePage::backeButtonClicked()
{
    wizard()->back();
}

PesFinishPage::PesFinishPage(PackageManagerCore *core)
    : PackageManagerPage(core)
    , result_page_(nullptr)
    , uninstall_result_page_(nullptr)
{
    if (core->isInstaller()) {
        initInstallFinishedPage();
    }
    else {
        initUninstallFinishedPage();
    }
}

void PesFinishPage::handleReboot()
{
    packageManagerCore()->rebootSystem();
}

void PesFinishPage::handleStartNow()
{
    const QString program =
        packageManagerCore()->replaceVariables(packageManagerCore()->value(scRunProgram));

    const QStringList args = packageManagerCore()->replaceVariables(packageManagerCore()
                                                                        ->values(scRunProgramArguments));
    if (program.isEmpty())
        return;

    qCDebug(QInstaller::lcInstallerInstallLog) << "starting" << program << args;
    QProcess::startDetached(program, args);
    wizard()->close();
}

void PesFinishPage::handleFinish()
{
    wizard()->close();
}

void QInstaller::PesFinishPage::initInstallFinishedPage()
{
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, kShadowLen);

    auto customTitle = new CustomTitle(this);
    customTitle->setObjectName(QLatin1String("CustomTitle"));
    customTitle->setTitle(tr("Installation guide"));
    customTitle->setButtonVisible(CustomTitle::CloseButton, false);

    pLayout->addWidget(customTitle);

    result_page_ = new PesResultPage(this);
    result_page_->initUI();

    QPixmap pixmap(QLatin1String(":/install_yes.png"));
    result_page_->icon_pixmap_= pixmap;

    if (packageManagerCore()->isInstaller()) {
        result_page_->messageLabel->setText(tr("Install Failed"));
    }
    else {
        result_page_->messageLabel->setText(tr("UnInstall Failed"));
    }
    pLayout->addSpacing(-8);
    pLayout->addWidget(result_page_);

    if (packageManagerCore()->isInstaller()) {
        result_page_->messageLabel->setText(tr("Finish Install"));
        result_page_->detailLabel->setText(packageManagerCore()->settings().value(scInstallFinish).toString());
    }
    else {
        result_page_->messageLabel->setText(tr("Finish Uninstall"));
        result_page_->detailLabel->setText(packageManagerCore()->settings().value(scUninstallFinish).toString());
    }

    if (packageManagerCore()->settings().needRestart()) {
        result_page_->leftButton->setText(tr("Reboot Later"));
        result_page_->rightButton->setText(tr("Reboot Now"));
        connect(result_page_->leftButton, &QAbstractButton::clicked, this, &PesFinishPage::handleFinish);
        connect(result_page_->rightButton, &QAbstractButton::clicked, this, &PesFinishPage::handleReboot);
    }
    else {
        result_page_->leftButton->setVisible(false);
        result_page_->rightButton->setText(tr("Start Now"));
        connect(result_page_->rightButton, &QAbstractButton::clicked, this, &PesFinishPage::handleStartNow);
    }

    setCommitPage(true);
}

void QInstaller::PesFinishPage::initUninstallFinishedPage()
{
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, kShadowLen);

    auto customTitle = new CustomTitle(this);
    customTitle->setObjectName(QLatin1String("CustomTitle"));
    customTitle->setButtonVisible(CustomTitle::CloseButton, false);
    pLayout->addWidget(customTitle);
    
    uninstall_result_page_ = new PesUnInstallResultPage(true, this);
    uninstall_result_page_->initUI();
   
    pLayout->addSpacing(-8);
    pLayout->addWidget(uninstall_result_page_);

    uninstall_result_page_->iconLabel_->setText(tr("Restart Computer to complete UnInstallation"));

    uninstall_result_page_->leftButton_->setText(tr("Reboot Later"));
    uninstall_result_page_->rightButton_->setText(tr("Reboot Now"));

    connect(uninstall_result_page_->leftButton_, &QAbstractButton::clicked, this, &PesFinishPage::handleFinish);
    connect(uninstall_result_page_->rightButton_, &QAbstractButton::clicked, this, &PesFinishPage::handleReboot);

    setCommitPage(true);
}

PesUninstallHomePage::PesUninstallHomePage(PackageManagerCore *core)
    : PackageManagerPage(core)
    , cancel_btn_(nullptr)
    , uninstall_btn_(nullptr)
    , clear_account_info_(false)
{
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(kShadowLen, kShadowLen , kShadowLen, kShadowLen);

    auto customTitle = new CustomTitle(this);
    customTitle->setObjectName(QLatin1String("CustomTitle"));
    customTitle->setButtonVisible(CustomTitle::CloseButton, false);
    pLayout->addWidget(customTitle);

    QHBoxLayout* pMessageLayout = new QHBoxLayout();
    pMessageLayout->setContentsMargins(0, 0, 0, 0);
    {
        QLabel* pTextLabel = new QLabel(this);
        pTextLabel->setObjectName(QString::fromUtf8("uninstallLabelMessage"));
        pTextLabel->setText(tr("Are you sure to uninstall PES?"));

        pMessageLayout->addStretch(20);
        pMessageLayout->addWidget(pTextLabel);
        pMessageLayout->addStretch(167);
    }

    QHBoxLayout* pClearAccoutLayout = new QHBoxLayout();
    pClearAccoutLayout->setContentsMargins(0, 0, 0, 0);
    {
        QCheckBox* clearAccountCheckBox = new QCheckBox(this);
        clearAccountCheckBox->setObjectName(QLatin1String("ClearAccountCheckBox"));
        clearAccountCheckBox->setText(tr("Clear Account"));

        pClearAccoutLayout->addStretch(223);
        pClearAccoutLayout->addWidget(clearAccountCheckBox);
        pClearAccoutLayout->addStretch(20);

        connect(clearAccountCheckBox, SIGNAL(toggled(bool)), this, SLOT(onClearAccountToggle(bool)));
    }

    QHBoxLayout* pBtnLayout = new QHBoxLayout();
    pBtnLayout->setContentsMargins(0, 0, 0, 0);
    {
        cancel_btn_ = new QPushButton(this);
        cancel_btn_->setText(tr("Cancel"));
        cancel_btn_->setObjectName(QLatin1String("cancelButton"));

        uninstall_btn_ = new QPushButton(this);
        uninstall_btn_->setText(tr("UnInstall"));
        uninstall_btn_->setObjectName(QLatin1String("agreeButton"));

        pBtnLayout->addStretch(179);
        pBtnLayout->addWidget(cancel_btn_);
        pBtnLayout->addStretch(10);
        pBtnLayout->addWidget(uninstall_btn_);
        pBtnLayout->addStretch(20);
    }

    pLayout->addStretch(22);
    pLayout->addLayout(pMessageLayout);
    pLayout->addStretch(12);
    pLayout->addLayout(pClearAccoutLayout);
    pLayout->addStretch(11);
    pLayout->addLayout(pBtnLayout);
    pLayout->addStretch(20);

    connect(cancel_btn_, &QAbstractButton::clicked, this, &PesUninstallHomePage::cancelUninstall);
    connect(uninstall_btn_, &QAbstractButton::clicked, this, &PesUninstallHomePage::startUninstall);
    setCommitPage(true);
}

void PesUninstallHomePage::startUninstall()
{
    QList<ProcessInfo> pesProcessInfo;
    QList<ProcessInfo> processInfos = runningProcesses();
    Q_FOREACH(const ProcessInfo & item, processInfos) {
        if (item.name.contains(QLatin1String("pes_gui.exe"))) {
            pesProcessInfo.push_back(item);
        }
    }

    if (!pesProcessInfo.isEmpty()) {
        Q_FOREACH(const ProcessInfo& item, pesProcessInfo) {
            killProcess(item);
        }
    }

    if (clear_account_info_) {
        const QString base_path = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QLatin1String("/.mthreads");
        QDir baseDir(QFileInfo(base_path).absoluteFilePath());
        if (baseDir.exists()) {
            baseDir.removeRecursively();
        }
    }
    wizard()->next();
}

void PesUninstallHomePage::cancelUninstall()
{
    wizard()->button(QWizard::CancelButton)->click();
}

void QInstaller::PesUninstallHomePage::onClearAccountToggle(bool bChecked)
{
    clear_account_info_ = bChecked;
}

PesErrorPage::PesErrorPage(PackageManagerCore* core)
    : PackageManagerPage(core)
    , result_page_(nullptr)
    , uninstall_result_page_(nullptr)
{
    if (core->isInstaller()) {
        initInstallErrorPage();
    }
    else {
        initUninstallErrorPage();
    }
}
void PesErrorPage::entering()
{
    QString msg = packageManagerCore()->error();
    if(packageManagerCore()->isInstaller() && !packageManagerCore()->settings().value(scInstallError).toString().isEmpty()){
        msg += QLatin1String("\n") + packageManagerCore()->settings().value(scInstallError).toString();
    } else if(packageManagerCore()->isUninstaller() && !packageManagerCore()->settings().value(scUninstallError).toString().isEmpty()) {
        msg += QLatin1String("\n") + packageManagerCore()->settings().value(scUninstallError).toString();
    }
    result_page_->detailLabel->setText(msg);
}

void PesErrorPage::handleFinish()
{
    wizard()->close();
}

void QInstaller::PesErrorPage::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    
    QRect iconRect(0, 32, 0, 392 + 34);
    painter.fillRect(iconRect, 0xF0F0F0);
}

void QInstaller::PesErrorPage::initInstallErrorPage()
{
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, kShadowLen);

    auto customTitle = new CustomTitle(this);
    customTitle->setObjectName(QLatin1String("CustomTitle"));
    customTitle->setTitle(tr("Installation guide"));
    customTitle->setButtonVisible(CustomTitle::CloseButton, false);

    pLayout->addWidget(customTitle);

    result_page_ = new PesResultPage(this);
    result_page_->initUI();

    QPixmap pixmap(QLatin1String(":/failed.png"));
    result_page_->icon_pixmap_ = pixmap;

    result_page_->messageLabel->setText(tr("Install Failed"));
   
    result_page_->leftButton->setVisible(false);
    result_page_->rightButton->setText(tr("Exit"));

    pLayout->addSpacing(-8);
    pLayout->addWidget(result_page_);

    connect(result_page_->rightButton, &QAbstractButton::clicked, this, &PesErrorPage::handleFinish);
    setCommitPage(true);
}

void QInstaller::PesErrorPage::initUninstallErrorPage()
{
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, kShadowLen);

    auto customTitle = new CustomTitle(this);
    customTitle->setObjectName(QLatin1String("CustomTitle"));
    customTitle->setButtonVisible(CustomTitle::CloseButton, false);

    pLayout->addWidget(customTitle);

    uninstall_result_page_ = new PesUnInstallResultPage(false, this);
    uninstall_result_page_->initUI();

    uninstall_result_page_->iconLabel_->setText(tr("UnInstall Failed"));

    uninstall_result_page_->leftButton_->setVisible(false);
    uninstall_result_page_->rightButton_->setText(tr("Close"));

    pLayout->addWidget(uninstall_result_page_);

    connect(uninstall_result_page_->rightButton_, &QAbstractButton::clicked, this, &PesErrorPage::handleFinish);

    setCommitPage(true);
}

QInstaller::PesResultPage::PesResultPage(QWidget* parent)
    : QWidget(parent)
{
}

QInstaller::PesResultPage::~PesResultPage()
{
}

void QInstaller::PesResultPage::initUI()
{
    QVBoxLayout* verticalLayout = new QVBoxLayout(this);
    verticalLayout->setContentsMargins(0, 0, 0, 0);
    verticalLayout->setSpacing(0);
    verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));

    messageLabel = new QLabel(this);
    messageLabel->setObjectName(QString::fromUtf8("messageLabel"));
    messageLabel->setAlignment(Qt::AlignCenter);
    messageLabel->setWordWrap(true);

    detailLabel = new QLabel(this);
    detailLabel->setObjectName(QString::fromUtf8("detailLabel"));
    detailLabel->setAlignment(Qt::AlignCenter);
    detailLabel->setWordWrap(true);

    QHBoxLayout* horizontalLayout = new QHBoxLayout();
    horizontalLayout->setContentsMargins(0, 0, 0, 0);
    horizontalLayout->setSpacing(20);
    horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
    {
        leftButton = new QPushButton(this);
        leftButton->setObjectName(QString::fromUtf8("leftButton"));
        leftButton->setFixedSize(QSize(109, 46));

        rightButton = new QPushButton(this);
        rightButton->setObjectName(QString::fromUtf8("rightButton"));
        rightButton->setFixedSize(QSize(109, 46));

        horizontalLayout->addStretch();
        horizontalLayout->addWidget(leftButton);
        horizontalLayout->addWidget(rightButton);
        horizontalLayout->addStretch();
    }

    QSpacerItem* spacer = new QSpacerItem(0, 0, QSizePolicy::Ignored, QSizePolicy::MinimumExpanding);
    verticalLayout->addItem(spacer);

    verticalLayout->addWidget(messageLabel);
    verticalLayout->addSpacing(8);
    verticalLayout->addWidget(detailLabel);
    verticalLayout->addSpacing(99);
    verticalLayout->addLayout(horizontalLayout);
    verticalLayout->addSpacing(53);
}

void QInstaller::PesResultPage::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRect bgRect(0, 0, width(), height() - 152);
    painter.fillRect(bgRect, 0xF5F5F5);

    QRect iconRect(0, 88, width(), messageLabel->pos().y() - 88 + 15);
    qreal ratio = (qreal)iconRect.width() / iconRect.height();
    qreal iconRatio = (qreal)icon_pixmap_.width() / icon_pixmap_.height();

    int iconWidth = iconRatio * iconRect.height();
    iconRect.setX((width() - iconWidth) / 2.0);
    iconRect.setWidth(iconWidth);

    painter.drawPixmap(iconRect, icon_pixmap_);
}

#include "packagemanagergui.moc"
#include "moc_packagemanagergui.cpp"

QInstaller::PesUnInstallResultPage::PesUnInstallResultPage(bool bSucceed, QWidget* parent)
    : QWidget(parent)
    , install_succeed_(bSucceed)
{
    
}

QInstaller::PesUnInstallResultPage::~PesUnInstallResultPage()
{
}

void QInstaller::PesUnInstallResultPage::initUI()
{
    QVBoxLayout* verticalLayout = new QVBoxLayout(this);
    verticalLayout->setContentsMargins(0, 0, 0, 0);
    verticalLayout->setSpacing(0);
    verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));

    QHBoxLayout* pIconLayout = new QHBoxLayout();
    pIconLayout->setContentsMargins(0, 0, 0, 0);
    {
        if (!install_succeed_) {
            QLabel* pWarningLabel = new QLabel(this);
            pWarningLabel->setFixedSize(16, 16);

            QPixmap pixmap(QLatin1String(":/license_warning.png"));
            pWarningLabel->setPixmap(pixmap.scaled(16, 16));

            pIconLayout->addSpacing(5);
            pIconLayout->addWidget(pWarningLabel);
            pIconLayout->addSpacing(17);
        }

        iconLabel_ = new QLabel(this);
        iconLabel_->setObjectName(QString::fromUtf8("unInstallMessageLabel"));

        pIconLayout->addStretch(20);
        pIconLayout->addWidget(iconLabel_);
        pIconLayout->addStretch(141);
    }

    QHBoxLayout* horizontalLayout = new QHBoxLayout();
    horizontalLayout->setContentsMargins(0, 0, 0, 0);
    horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
    {
        leftButton_ = new QPushButton(this);
        leftButton_->setObjectName(QString::fromUtf8("leftButton"));
        leftButton_->setFixedSize(QSize(88, 34));

        rightButton_ = new QPushButton(this);
        rightButton_->setObjectName(QString::fromUtf8("rightButton"));
        rightButton_->setFixedSize(QSize(88, 34));

        horizontalLayout->addStretch(131);
        horizontalLayout->addWidget(leftButton_);
        horizontalLayout->addStretch(10);
        horizontalLayout->addWidget(rightButton_);
        horizontalLayout->addStretch(20);
    }

    verticalLayout->addStretch(22);
    verticalLayout->addLayout(pIconLayout);
    verticalLayout->addStretch(install_succeed_ ? 39 : 9);
    verticalLayout->addLayout(horizontalLayout);
    verticalLayout->addStretch(20);

    if (!install_succeed_) {
        leftButton_->setVisible(false);
        rightButton_->setObjectName(QString::fromUtf8("rightButton"));
    }
}

void QInstaller::PesUnInstallResultPage::setMessage(const QString& msg)
{
    message_text_ = msg;
}


QInstaller::ProcessDetectMessageBox::ProcessDetectMessageBox(QWidget* parent)
    : RoundShadowDiag(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(400 + 2 * kShadowLen, 194 + 2 * kShadowLen);

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, kShadowLen);

    QHBoxLayout* pTitleLayout = new QHBoxLayout();
    pTitleLayout->setContentsMargins(16, 18, 0, 0);
    {
        QLabel* pWarningLabel = new QLabel(this);
        pWarningLabel->setFixedSize(22, 22);

        QPixmap pixmap(QLatin1String(":/license_warning.png"));
        pWarningLabel->setPixmap(pixmap.scaled(22, 22));

        QLabel* pTextLabel = new QLabel(this);
        pTextLabel->setObjectName(QLatin1String("PesLicenseMsgLabel"));
        pTextLabel->setText(tr("Program running"));
        pTextLabel->setAlignment(Qt::AlignLeft);

        close_btn_ = new QPushButton(this);
        QPixmap close_pixmap(QLatin1String(":/close_window_gray@2x.png"));
        close_btn_->setIcon(close_pixmap.scaled(16, 16));

        close_btn_->setObjectName(QLatin1String("windowCloseButton"));

        pTitleLayout->addWidget(pWarningLabel);
        pTitleLayout->addWidget(pTextLabel);
        pTitleLayout->addSpacing(213);
        pTitleLayout->addWidget(close_btn_);
        pTitleLayout->addStretch();
    }

    QHBoxLayout* pContentLayout = new QHBoxLayout();
    pContentLayout->setContentsMargins(16, 18, 0, 0);
    {
        QLabel* pTextLabel = new QLabel(this);
        pTextLabel->setFixedSize(376, 66);
        pTextLabel->setObjectName(QLatin1String("PesLicenseMsgLabel"));
        pTextLabel->setWordWrap(true);
        pTextLabel->setText(tr("When PES is running, program cannot be installed. You can close the PES window and click Retry, or just force close and continue"));
        pTextLabel->setAlignment(Qt::AlignLeft);

        pContentLayout->addWidget(pTextLabel);
    }

    QHBoxLayout* pBtnLayout = new QHBoxLayout();
    pBtnLayout->setContentsMargins(20, 0, 0, 0);
    {
        force_close_btn_ = new QPushButton(this);
        force_close_btn_->setText(tr("Force Close"));
        force_close_btn_->setObjectName(QLatin1String("forceButton"));

        retry_btn_ = new QPushButton(this);
        retry_btn_->setText(tr("Retry"));
        retry_btn_->setObjectName(QLatin1String("agreeButton"));

        quit_btn_ = new QPushButton(this);
        quit_btn_->setText(tr("Quit"));
        quit_btn_->setObjectName(QLatin1String("cancelButton"));

        pBtnLayout->addWidget(force_close_btn_);
        pBtnLayout->addSpacing(112);
        pBtnLayout->addWidget(retry_btn_);
        pBtnLayout->addSpacing(6);
        pBtnLayout->addWidget(quit_btn_);
        pBtnLayout->addStretch();
    }
    pLayout->addLayout(pTitleLayout);
    pLayout->addLayout(pContentLayout);
    pLayout->addLayout(pBtnLayout);
    pLayout->addStretch();

    connect(close_btn_, &QPushButton::clicked, this, [&] {
        done(QMessageBox::Close);
        });

    connect(force_close_btn_, &QPushButton::clicked, this, [&] {
        done(QMessageBox::Ok);
        });

    connect(retry_btn_, &QPushButton::clicked, this, [&] {
        done(QMessageBox::Retry);
        });

    connect(quit_btn_, &QPushButton::clicked, this, [&] {
        done(QMessageBox::Close);
        });
}

QInstaller::PesLicenMessageBox::PesLicenMessageBox(QWidget* parent)
    : RoundShadowDiag(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(357 + 2 * kShadowLen, 136 + 2 * kShadowLen);

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, kShadowLen);

    QHBoxLayout* pTitleLayout = new QHBoxLayout();
    pTitleLayout->setContentsMargins(16, 18, 0, 0);
    {
        QLabel* pWarningLabel = new QLabel(this);
        pWarningLabel->setFixedSize(22, 22);

        QPixmap pixmap(QLatin1String(":/license_warning.png"));
        pWarningLabel->setPixmap(pixmap.scaled(22, 22));

        QLabel* pTextLabel = new QLabel(this);
        pTextLabel->setObjectName(QLatin1String("PesLicenseMsgLabel"));
        pTextLabel->setText(tr("Please Agree"));
        pTextLabel->setAlignment(Qt::AlignLeft);

        QLabel* pUserLicenseLabel = new QLabel(this);
        pUserLicenseLabel->setObjectName(QLatin1String("PesLicenseMsgLabel"));
        pUserLicenseLabel->setText(tr("User License Agreement"));
        pUserLicenseLabel->setAlignment(Qt::AlignLeft);
        {
            QFont fs(pUserLicenseLabel->font());
            fs.setUnderline(true);
            pUserLicenseLabel->setFont(fs);
        }

        QLabel* pAndLabel = new QLabel(this);
        pAndLabel->setObjectName(QLatin1String("PesLicenseMsgLabel"));
        pAndLabel->setText(tr("And"));
        pAndLabel->setAlignment(Qt::AlignLeft);

        QLabel* pUserPrivacyLabel = new QLabel(this);
        pUserPrivacyLabel->setObjectName(QLatin1String("PesLicenseMsgLabel"));
        pUserPrivacyLabel->setText(tr("User Privacy Agreement"));
        pUserPrivacyLabel->setAlignment(Qt::AlignLeft);
        {
            QFont fs(pUserPrivacyLabel->font());
            fs.setUnderline(true);
            pUserPrivacyLabel->setFont(fs);
        }

        pTitleLayout->addWidget(pWarningLabel);
        pTitleLayout->addWidget(pTextLabel);
        pTitleLayout->addWidget(pUserLicenseLabel);
        pTitleLayout->addWidget(pAndLabel);
        pTitleLayout->addWidget(pUserPrivacyLabel);
        pTitleLayout->addStretch();
    }

    QHBoxLayout* pBtnLayout = new QHBoxLayout();
    pBtnLayout->setContentsMargins(198, 31, 0, 0);
    {
        cancel_btn_ = new QPushButton(this);
        cancel_btn_->setText(tr("Cancel"));
        cancel_btn_->setObjectName(QLatin1String("cancelButton"));

        agree_btn_ = new QPushButton(this);
        agree_btn_->setText(tr("Agree"));
        agree_btn_->setObjectName(QLatin1String("agreeButton"));

        pBtnLayout->addWidget(cancel_btn_);
        pBtnLayout->addSpacing(2);
        pBtnLayout->addWidget(agree_btn_);
        pBtnLayout->addStretch();
    }

    pLayout->addLayout(pTitleLayout);
    pLayout->addLayout(pBtnLayout);
    pLayout->addStretch();

    connect(cancel_btn_, &QPushButton::clicked, this, [&] {
        done(QMessageBox::Cancel);
        });

    connect(agree_btn_, &QPushButton::clicked, this, [&] {
        done(QMessageBox::Ok);
        });
}

QInstaller::PesLicenMessageBox::~PesLicenMessageBox()
{

}

QInstaller::PesEnvDetectMessageBox::PesEnvDetectMessageBox(QWidget* parent, QString msg)
    : RoundShadowDiag(parent)
    , minimize_btn_(nullptr)
    , close_btn_(nullptr)
    , cancel_btn_(nullptr)
    , detect_btn_(nullptr)
    , message_(msg)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(800 + 2 * kShadowLen, 560 + 2 * kShadowLen);

    QVBoxLayout* pMainLayout = new QVBoxLayout(this);
    pMainLayout->setContentsMargins(kShadowLen, kShadowLen, kShadowLen, kShadowLen);

    QHBoxLayout* pTitleLayout = new QHBoxLayout();
    pTitleLayout->setContentsMargins(720, 8, 8, 0);
    {
        minimize_btn_ = new QPushButton(this);
        close_btn_ = new QPushButton(this);

        minimize_btn_->installEventFilter(this);
        close_btn_->installEventFilter(this);

        QPixmap minimize_pixmap(QLatin1String(":/min_window_gray@2x.png"));
        QPixmap close_pixmap(QLatin1String(":/close_window_gray@2x.png"));

        minimize_btn_->setIcon(minimize_pixmap.scaled(16, 16));
        close_btn_->setIcon(close_pixmap.scaled(16, 16));

        minimize_btn_->setObjectName(QLatin1String("windowMinimizeButton"));
        close_btn_->setObjectName(QLatin1String("windowCloseButton"));

        pTitleLayout->addWidget(minimize_btn_);
        pTitleLayout->addSpacing(30);
        pTitleLayout->addWidget(close_btn_);
        pTitleLayout->addStretch();
    }

    QHBoxLayout* pIconLayout = new QHBoxLayout();
    pIconLayout->setContentsMargins(237, 67, 0, 0);
    {
        QLabel* pIconLabel = new QLabel(this);

        QPixmap pixmap(QLatin1String(":/gpu_undetected.png"));
        pIconLabel->setPixmap(pixmap.scaled(314, 277));

        pIconLayout->addWidget(pIconLabel);
        pIconLayout->addStretch();
    }

    QHBoxLayout* pMessageLayout = new QHBoxLayout();
    pMessageLayout->setContentsMargins(0, 0, 0, 0);
    {
        QLabel* pMessageLabel = new QLabel(this);
        pMessageLabel->setObjectName(QLatin1String("GpuNotExistMessageLabel"));
        pMessageLabel->setText(message_);
        pMessageLabel->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);

        pMessageLayout->addWidget(pMessageLabel, Qt::AlignHCenter);
        pMessageLayout->addStretch();
    }

    QHBoxLayout* pBtnLayout = new QHBoxLayout();
    pBtnLayout->setContentsMargins(281, 18, 0, 0);
    {
        cancel_btn_ = new QPushButton(this);
        cancel_btn_->setText(tr("Cancel Install"));
        cancel_btn_->setObjectName(QLatin1String("cancelInstallButton"));
		cancel_btn_->setFixedSize(109, 46);

        detect_btn_ = new QPushButton(this);
        detect_btn_->setText(tr("Detect"));
        detect_btn_->setObjectName(QLatin1String("detectButton"));
		detect_btn_->setFixedSize(109, 46);

        pBtnLayout->addWidget(cancel_btn_);
        pBtnLayout->addSpacing(10);
        pBtnLayout->addWidget(detect_btn_);
        pBtnLayout->addStretch();
    }

    pMainLayout->addLayout(pTitleLayout);
    pMainLayout->addLayout(pIconLayout);
    pMainLayout->addLayout(pMessageLayout);
    pMainLayout->addLayout(pBtnLayout);
    pMainLayout->addStretch();

    connect(cancel_btn_, &QPushButton::clicked, this, [&] {
        done(QMessageBox::Cancel);
        });

    connect(detect_btn_, &QPushButton::clicked, this, [&] {
        done(QMessageBox::Ok);
        });

    connect(close_btn_, &QPushButton::clicked, this, [&] {
        done(QMessageBox::Close);
        });

    connect(minimize_btn_, &QPushButton::clicked, this, [&] {
        showMinimized();
        });
}

QInstaller::PesEnvDetectMessageBox::~PesEnvDetectMessageBox()
{
    minimize_btn_->removeEventFilter(this);
    close_btn_->removeEventFilter(this);
}

bool QInstaller::PesEnvDetectMessageBox::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == minimize_btn_) {
        if (ev->type() == QEvent::Enter) {
            QPixmap minimize_pixmap(QLatin1String(":/min_window@2x.png"));
            minimize_btn_->setIcon(minimize_pixmap.scaled(16, 16));
        }
        else if (ev->type() == QEvent::Leave) {
            QPixmap minimize_pixmap(QLatin1String(":/min_window_gray@2x.png"));
            minimize_btn_->setIcon(minimize_pixmap.scaled(16, 16));
        }
    }
    else if (obj == close_btn_) {
        if (ev->type() == QEvent::Enter) {
            QPixmap close_pixmap(QLatin1String(":/close_window@2x.png"));
            close_btn_->setIcon(close_pixmap.scaled(16, 16));
        }
        else if (ev->type() == QEvent::Leave) {
            QPixmap close_pixmap(QLatin1String(":/close_window_gray@2x.png"));
            close_btn_->setIcon(close_pixmap.scaled(16, 16));
        }
    }
    return false;
}

void QInstaller::RoundShadowDiag::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QBrush(Qt::white));
    painter.setPen(Qt::transparent);

    int radius = 8;

    QColor color(102, 102, 102, 200);
    for (int i = 0; i < kShadowLen; i++)
    {
        int nAlpha = 120 - sqrt(i) * 50;
        if (nAlpha < 0)
            break;
        color.setAlpha(nAlpha);
        painter.setPen(color);
        painter.setBrush(QBrush(Qt::transparent));
        painter.drawRoundedRect(
            kShadowLen - i, kShadowLen - i,
            width() - (kShadowLen - i) * 2,
            height() - (kShadowLen - i) * 2,
            radius, radius);
    }

    painter.setBrush(QBrush(Qt::white));
    QRect drawRect(kShadowLen, kShadowLen, width() - 2 * kShadowLen, height() - 2 * kShadowLen);
    painter.drawRoundedRect(drawRect, radius, radius);
}

QInstaller::PesToolTip::PesToolTip(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    setFixedSize(440, 48);

    QHBoxLayout* pLayout = new QHBoxLayout(this);
    pLayout->setContentsMargins(140, 12, 0, 12);
    {
        QLabel* pWarningLabel = new QLabel(this);

        QPixmap pixmap(QLatin1String(":/license_warning.png"));
        pWarningLabel->setPixmap(pixmap.scaled(18, 18));

        text_label_ = new QLabel(this);
        text_label_->setObjectName(QLatin1String("warningToolTipLabel"));
        text_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        pLayout->addWidget(pWarningLabel);
        pLayout->addSpacing(14);
        pLayout->addWidget(text_label_);
        pLayout->addStretch();
    }

    timer_ = new QTimer(this);
    bool b = connect(timer_, SIGNAL(timeout()), this, SLOT(onTimeOut()));
}

void QInstaller::PesToolTip::start()
{
    if (timer_) {
        timer_->stop();

        timer_->start(3000);
    }

    show();
}

void QInstaller::PesToolTip::stop()
{
    if (timer_) {
        timer_->stop();
    }

    hide();
}

void QInstaller::PesToolTip::setMessage(const QString& msg)
{
    if (text_label_) {
        text_label_->setText(msg);
    }
}

void QInstaller::PesToolTip::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    p.fillRect(rect(), 0xFFECE5);
}

void QInstaller::PesToolTip::onTimeOut()
{
    if (timer_) {
        timer_->stop();
    }
    hide();
}
