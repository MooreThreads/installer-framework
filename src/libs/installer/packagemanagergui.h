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

#ifndef PACKAGEMANAGERGUI_H
#define PACKAGEMANAGERGUI_H

#include "packagemanagercore.h"
#include "custom_messgebox.h"

#include <QtCore/QEvent>
#include <QtCore/QMetaType>
#include <QtCore/QTimer>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QSpacerItem>
#include <QIcon>
#include <QString>
#include <QDebug>
#include <QMessageBox>
#include <QWizard>
#include <QWizardPage>
#include <QProxyStyle>
// FIXME: move to private classes
QT_BEGIN_NAMESPACE
class QAbstractButton;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QRadioButton;
class QTextBrowser;
class QWinTaskbarButton;
class QTextEdit;
class QScrollArea;
QT_END_NAMESPACE

namespace QInstaller {

class PackageManagerCore;
class PackageManagerPage;
class PerformInstallationForm;
class ComponentSelectionPagePrivate;

// -- PackageManagerGui
class INSTALLER_EXPORT PackageManagerGui : public QWizard
{
    Q_OBJECT
public:
    explicit PackageManagerGui(PackageManagerCore *core, QWidget *parent = 0);
    virtual ~PackageManagerGui() = 0;

    void loadControlScript(const QString& scriptPath);
    void callControlScriptMethod(const QString& methodName);

    QWidget *pageById(int id) const;
    QWidget *pageByObjectName(const QString &name) const;

    QWidget *currentPageWidget() const;
    QWidget *pageWidgetByObjectName(const QString &name) const;

    QString defaultButtonText(int wizardButton) const;
    void clickButton(int wizardButton, int delayInMs = 0);
    void clickButton(const QString &objectName, int delayInMs = 0) const;
    bool isButtonEnabled(int wizardButton);

    void showSettingsButton(bool show);
    void setSettingsButtonEnabled(bool enable);

    void updateButtonLayout();
    static QWizard::WizardStyle getStyle(const QString &name);

    void setSilent(bool silent, bool bSilentInstall = true);
    bool isSilent() const;

    void setTextItems(QObject *object, const QStringList &items);

protected:
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;

Q_SIGNALS:
    void interrupted();
    void languageChanged();
    void finishButtonClicked();
    void gotRestarted();
    void settingsButtonClicked();

public Q_SLOTS:
    void cancelButtonClicked();
    void reject();
    void rejectWithoutPrompt();
    void showFinishedPage();
    void setModified(bool value);
    void setMaxSize();
    void updatePageListWidget();

protected Q_SLOTS:
    void wizardPageInsertionRequested(QWidget *widget, QInstaller::PackageManagerCore::WizardPage page);
    void wizardPageRemovalRequested(QWidget *widget);
    void wizardWidgetInsertionRequested(QWidget *widget, QInstaller::PackageManagerCore::WizardPage page,
                                        int position);
    void wizardWidgetRemovalRequested(QWidget *widget);
    void wizardPageVisibilityChangeRequested(bool visible, int page);
    void setValidatorForCustomPageRequested(QInstaller::Component *component, const QString &name,
                                            const QString &callbackName);

    void setAutomatedPageSwitchEnabled(bool request);

private Q_SLOTS:
    void onLanguageChanged();
    void customButtonClicked(int which);
    void dependsOnLocalInstallerBinary();
    void currentPageChanged(int newId);

protected:
    bool event(QEvent *event) override;
    void showEvent(QShowEvent*) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    PackageManagerCore *packageManagerCore() const { return m_core; }
    void executeControlScript(int pageId);

private:
    qreal origin_dpi_;
    QSize origin_size_;
    QSize current_size_;
    qreal current_dpi_;
    QSize size_adjust_;
    class Private;
    Private *const d;
    PackageManagerCore *m_core;
    QListWidget *m_pageListWidget;
    QLabel* space_label_;
    QHash<QString, QString> m_messages;
};


// -- PackageManagerPage

class INSTALLER_EXPORT PackageManagerPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit PackageManagerPage(PackageManagerCore *core);
    virtual ~PackageManagerPage() {}

    virtual QString productName() const;
    virtual QPixmap wizardPixmap(const QString &pixmapType) const;

    void setColoredTitle(const QString &title);
    void setColoredSubTitle(const QString &subTitle);

    void setPageListTitle(const QString &title);
    QString pageListTitle() const;

    void setShowOnPageList(bool show);
    bool showOnPageList() const;

    virtual bool isComplete() const;
    void setComplete(bool complete);

    virtual bool isInterruptible() const { return true; }
    PackageManagerGui* gui() const { return qobject_cast<PackageManagerGui*>(wizard()); }

    void setValidatePageComponent(QInstaller::Component *component);

    bool validatePage();

    bool settingsButtonRequested() const { return m_needsSettingsButton; }
    void setSettingsButtonRequested(bool request) { m_needsSettingsButton = request; }
    void removeCustomWidget(const QWidget *widget);

    void setSilent(bool bSilent);
    bool isSilent() const;

signals:
    void entered();
    void left();
    void showOnPageListChanged();

protected:
    PackageManagerCore *packageManagerCore() const;

    // Inserts widget into the same layout like a sibling identified
    // by its name. Default position is just behind the sibling.
    virtual void insertWidget(QWidget *widget, const QString &siblingName, int offset = 1);
    virtual QWidget *findWidget(const QString &objectName) const;

    virtual int nextId() const; // reimp

    // Used to support some kind of initializePage() in the case the wizard has been set
    // to QWizard::IndependentPages. If that option has been set, initializePage() would be only
    // called once. So we provide entering() and leaving() based on currentPageChanged() signal.
    virtual void entering() {} // called on entering
    virtual void leaving() {}  // called on leaving

private:
    bool m_complete;
    QString m_titleColor;
    QString m_pageListTitle;
    bool m_showOnPageList;
    bool m_needsSettingsButton;
    bool m_bSilent;

    PackageManagerCore *m_core;
    QInstaller::Component *validatorComponent;
    QMultiMap<int, QWidget*> m_customWidgets;

    friend class PackageManagerGui;
};


// -- IntroductionPage

class INSTALLER_EXPORT IntroductionPage : public PackageManagerPage
{
    Q_OBJECT

public:
    explicit IntroductionPage(PackageManagerCore *core);

    void setText(const QString &text);

    int nextId() const;
    bool validatePage();

    void showAll();
    void hideAll();
    void showMetaInfoUpdate();
    void showMaintenanceTools();
    void setMaintenanceToolsEnabled(bool enable);

public Q_SLOTS:
    void onCoreNetworkSettingsChanged();
    void setMessage(const QString &msg);
    void onProgressChanged(int progress);
    void setTotalProgress(int totalProgress);
    void setErrorMessage(const QString &error);

Q_SIGNALS:
    void packageManagerCoreTypeChanged();

private Q_SLOTS:
    void setUpdater(bool value);
    void setUninstaller(bool value);
    void setPackageManager(bool value);

private:
    void initializePage();

    void entering();
    void leaving();

    void showWidgets(bool show);
    bool validRepositoriesAvailable() const;

private:
    bool m_updatesFetched;
    bool m_allPackagesFetched;

    QLabel *m_label;
    QLabel *m_msgLabel;
    QLabel *m_errorLabel;
    QProgressBar *m_progressBar;
    QRadioButton *m_packageManager;
    QRadioButton *m_updateComponents;
    QRadioButton *m_removeAllComponents;

#ifdef Q_OS_WIN
    QWinTaskbarButton *m_taskButton;
#endif
};

// -- ComponentSelectionPage

class INSTALLER_EXPORT ComponentSelectionPage : public PackageManagerPage
{
    Q_OBJECT

public:
    explicit ComponentSelectionPage(PackageManagerCore *core);
    ~ComponentSelectionPage();

    bool isComplete() const;

    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselectAll();
    Q_INVOKABLE void selectDefault();
    Q_INVOKABLE void selectComponent(const QString &id);
    Q_INVOKABLE void deselectComponent(const QString &id);
    Q_INVOKABLE void allowCompressedRepositoryInstall();
    Q_INVOKABLE bool addVirtualComponentToUninstall(const QString &name);

protected:
    void entering();
    void leaving();
    void showEvent(QShowEvent *event);

private Q_SLOTS:
    void setModified(bool modified);

private:
    friend class ComponentSelectionPagePrivate;
    ComponentSelectionPagePrivate *const d;
};

// -- PerformInstallationPage

class INSTALLER_EXPORT PerformInstallationPage : public PackageManagerPage
{
    Q_OBJECT
public:
    explicit PerformInstallationPage(PackageManagerCore *core);
    ~PerformInstallationPage();
    bool isAutoSwitching() const;

protected:
    virtual int nextId() const;
    void entering();
    void leaving();
    bool isInterruptible() const { return false; }

public Q_SLOTS:
    void setTitleMessage(const QString& title);
    void changeCurrentImage();

Q_SIGNALS:
    void setAutomatedPageSwitchEnabled(bool request);

private Q_SLOTS:
    void installationStarted();
    void installationFinished();

    void uninstallationStarted();
    void uninstallationFinished();

    void toggleDetailsWereChanged();
    void updatePageListTitle();

private:
    PerformInstallationForm *m_performInstallationForm;
    QTimer m_imageChangeTimer;
    QString m_currentImage;
};


// -- FinishedPage

class INSTALLER_EXPORT FinishedPage : public PackageManagerPage
{
    Q_OBJECT

public:
    explicit FinishedPage(PackageManagerCore *core);

public Q_SLOTS:
    void handleFinishClicked();
    void cleanupChangedConnects();

protected:
    void entering();
    void leaving();

private:
    QLabel *m_msgLabel;
    QCheckBox *m_runItCheckBox;
    QAbstractButton *m_commitButton;
};


// -- RestartPage

class INSTALLER_EXPORT RestartPage : public PackageManagerPage
{
    Q_OBJECT

public:
    explicit RestartPage(PackageManagerCore *core);

    virtual int nextId() const;

protected:
    void entering();
    void leaving();

Q_SIGNALS:
    void restart();
};


struct PesLicenceInfo
{
    enum LicenseType
    {
        UserService,
        UserPrivacy
    };
    PesLicenceInfo()
    : is_licence_agreed(false)
    , licence_type(UserService)
    {}
    
    bool is_licence_agreed;
    LicenseType licence_type;
};

class PesToolTip;
class INSTALLER_EXPORT PesHomePage : public PackageManagerPage
{
    Q_OBJECT
public:
    explicit PesHomePage(PackageManagerCore *core, PesLicenceInfo* info);
    
    void setLicenceAgreed(bool agree);
    
    virtual void initializePage();
    virtual int nextId() const;
    
protected:
    void entering() override;

private:
    void connectAll();
    void startInstall();
    void showUserService();
    void showUserPrivacy();
    void chooseDirectory();
    bool setSpaceMessage(quint64 need_space, quint64 available_space);
    void updateButtonBackground(bool agree);

    bool checkDirWritable(const QString& dirPath);
    bool checkCanInstall(const QString& dirPath);

    bool m_all_packages_fetched;
    QWidget* background_widget_;
    QLabel* welcom_label_;
    QLabel* introduce_label_;
    QLabel* dir_choose_label_;
    QPushButton* install_button_;
    QLineEdit* dir_text_;
    QPushButton* dir_choose_button_;
    QLabel* space_label_;
    QPushButton* warning_button_;
    QCheckBox* licence_check_box_;
    QPushButton* user_service_btn_;
    QPushButton* user_privacy_btn_;
    bool force_to_licence_page_;
    PesLicenceInfo* licence_info_;
    PesToolTip* tool_tip_;
    quint64 need_space_;

    QString target_dir_;
};

class INSTALLER_EXPORT PesLicencePage : public PackageManagerPage
{
    Q_OBJECT
public:
    explicit PesLicencePage(PackageManagerCore *core, PesLicenceInfo* info);
    void entering() override;

private:
    void backeButtonClicked();

private:
    QTextEdit* text_edit_;
    QPushButton* back_button_;
    PesLicenceInfo* licence_info_;
};

class INSTALLER_EXPORT PesUninstallHomePage : public PackageManagerPage
{
    Q_OBJECT
public:
    explicit PesUninstallHomePage(PackageManagerCore* core);
    bool isInterruptible() const override { return false; }

private:
    void startUninstall();
    void cancelUninstall();

protected slots:
    void onClearAccountToggle(bool);

private:
    bool clear_account_info_;
    QPushButton* cancel_btn_;
    QPushButton* uninstall_btn_;
};

//install result page
class PesResultPage : public QWidget
{
public:
    PesResultPage(QWidget* parent);
    ~PesResultPage();

    void initUI();

protected:
    void paintEvent(QPaintEvent* event) override;

public:
    QPixmap icon_pixmap_;

    QLabel* spacelabel = nullptr;
   
    QPushButton* leftButton = nullptr;
    QPushButton* rightButton = nullptr;

    QString message_text_;
    QString detail_text_;

    QLabel* messageLabel = nullptr;
    QLabel* detailLabel = nullptr;
};

//uninstall result page
class PesUnInstallResultPage : public QWidget
{
    Q_OBJECT
public:
    PesUnInstallResultPage(bool bSucceed, QWidget* parent);
    ~PesUnInstallResultPage();

    void initUI();

    void setMessage(const QString& msg);

public:
    QLabel* iconLabel_ = nullptr;

    QPushButton* rightButton_ = nullptr;
    QPushButton* leftButton_ = nullptr;

    QString message_text_;
    bool install_succeed_ = true;
};

class INSTALLER_EXPORT PesFinishPage : public PackageManagerPage
{
    Q_OBJECT
public:
    explicit PesFinishPage(PackageManagerCore *core);
private:
    void handleFinish();
    void handleReboot();
    void handleStartNow();

private:
    void initInstallFinishedPage();
    void initUninstallFinishedPage();
private:
    PesResultPage* result_page_;
    PesUnInstallResultPage* uninstall_result_page_;
};

class INSTALLER_EXPORT PesErrorPage : public PackageManagerPage
{
    Q_OBJECT
public:
    explicit PesErrorPage(PackageManagerCore *core);
protected:
    void entering() override;
    void paintEvent(QPaintEvent* event) override;
private:
    void handleFinish();

    void initInstallErrorPage();
    void initUninstallErrorPage();

private:
    PesResultPage* result_page_;
    PesUnInstallResultPage* uninstall_result_page_;
};

//message box
class RoundShadowDiag : public QDialog
{
    Q_OBJECT
public:
    explicit RoundShadowDiag(QWidget* parent) : QDialog(parent) {}
protected:
    void paintEvent(QPaintEvent* event) override;
};

class ProcessDetectMessageBox : public RoundShadowDiag
{
    Q_OBJECT
public:
    explicit ProcessDetectMessageBox(QWidget* parent);
    ~ProcessDetectMessageBox() {}

private:
    QPushButton* force_close_btn_;
    QPushButton* retry_btn_;
    QPushButton* close_btn_;
    QPushButton* quit_btn_;
};

class PesLicenMessageBox : public RoundShadowDiag
{
    Q_OBJECT
public:
    explicit PesLicenMessageBox(QWidget* parent);
    ~PesLicenMessageBox();

private:
    QPushButton* cancel_btn_;
    QPushButton* agree_btn_;
};

//Env not support dialog
class PesEnvDetectMessageBox : public RoundShadowDiag
{
    Q_OBJECT
public:
    explicit PesEnvDetectMessageBox(QWidget* parent, QString msg);
    ~PesEnvDetectMessageBox();

protected:
    bool eventFilter(QObject*, QEvent*) override;

private:
    QPushButton* minimize_btn_;
    QPushButton* close_btn_;

    QPushButton* cancel_btn_;
    QPushButton* detect_btn_;
    QString message_;
};

class PesToolTip : public QWidget
{
    Q_OBJECT
public:
    explicit PesToolTip(QWidget* parent);
    ~PesToolTip() {}

    void start();
    void stop();

    void setMessage(const QString& msg);

protected:
    void paintEvent(QPaintEvent* event) override;

protected slots:
    void onTimeOut();
private:
    QTimer* timer_;
    QLabel* text_label_;
};

} //namespace QInstaller

#endif  // PACKAGEMANAGERGUI_H
