#include <QLabel>
#include <QSpacerItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QPainter>
#include "custom_messgebox.h"

CustomTitle::CustomTitle(QWidget *parent)
    : QWidget(parent)
    , is_pressed_(false)
{
    setFixedHeight(32);
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);

    {
        title_ = new QLabel(this);
        title_->setObjectName(QLatin1String("CustomTitleLabel"));
        title_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        QLabel* iconLabel = new QLabel(this);
        iconLabel->setFixedSize(28, 16);

        QIcon icon(QLatin1String(":/PES.png"));
        iconLabel->setPixmap(icon.pixmap(28, 16));
        
        layout_->addSpacing(16);
        layout_->addWidget(iconLabel);
        layout_->addSpacing(6);
        layout_->addWidget(title_);
    }

    QSpacerItem* spacer = new QSpacerItem(0, 0, QSizePolicy::Ignored, QSizePolicy::MinimumExpanding);
    layout_->addItem(spacer);

    for (int i = 0; i < ButtonNums; i++) {
        buttons_[i] = new CustomButton(static_cast<ButtonType>(i), this);
        layout_->addWidget(buttons_[i]);
        layout_->addSpacing(8);
        buttons_[i]->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    buttons_[MinButton]->setObjectName(QLatin1String("windowMinimizeButton"));
    buttons_[CloseButton]->setObjectName(QLatin1String("windowCloseButton"));
    connect(buttons_[MinButton], &QAbstractButton::clicked, this, &CustomTitle::minimizeButtonClicked);
    connect(buttons_[CloseButton], &QAbstractButton::clicked, this, &CustomTitle::closeButtonClicked);
}

void CustomTitle::setButtonVisible(ButtonType type, bool visible)
{
    buttons_[type]->setVisible(visible);
}

void CustomTitle::setTitle(const QString &name)
{
    title_->setText(name);
}

void CustomTitle::mousePressEvent(QMouseEvent *event)
{
    is_pressed_ = true;
    start_move_pos_ = event->globalPos();
    QWidget::mousePressEvent(event);
}

void CustomTitle::mouseReleaseEvent(QMouseEvent *event)
{
    is_pressed_ = false;
    QWidget::mouseReleaseEvent(event);
}

void CustomTitle::mouseMoveEvent(QMouseEvent *event)
{
    if(is_pressed_){
        QPoint movePoint = event->globalPos() - start_move_pos_;
        QPoint windowPos = window()->pos();
        start_move_pos_ = event->globalPos();
        this->window()->move(windowPos + movePoint);
    }
    QWidget::mouseMoveEvent(event);
}

CustomMessgeBox::CustomMessgeBox(QWidget *parent)
    : QDialog(parent)
    , label_(new QLabel(this))
    , button_box_(new QDialogButtonBox(this))
    , clicked_button_(nullptr)
    , default_button_(nullptr)
{
    init();
}

CustomMessgeBox::CustomMessgeBox(const QString &title, const QString &text, QMessageBox::StandardButtons buttons, QWidget *parent)
    : QDialog(parent)
    , label_(new QLabel(this))
    , button_box_(new QDialogButtonBox(this))
    , clicked_button_(nullptr)
    , default_button_(nullptr)
{
    init();
    setWindowTitle(title);
    setText(text);
    setStandardButtons(buttons);
}

void CustomMessgeBox::setWindowTitle(const QString &title)
{
    custom_title_->setTitle(title);
}

void CustomMessgeBox::setText(const QString &text)
{
    label_->setText(text);
}

void CustomMessgeBox::setStandardButtons(QMessageBox::StandardButtons buttons)
{
    button_box_->setStandardButtons(QDialogButtonBox::StandardButtons(int(buttons)));
}

QAbstractButton *CustomMessgeBox::clickedButton() const
{
    return clicked_button_;
}

QPushButton* CustomMessgeBox::addButton(QMessageBox::StandardButton button)
{
    QPushButton* btn = button_box_->addButton(QDialogButtonBox::StandardButton(int(button)));
    translateButtonText(btn, button);
    return btn;
}

void CustomMessgeBox::init()
{
    setObjectName(QLatin1String("customMessageBox"));
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(8, 8, 8, 8);

    QHBoxLayout* pTitleLayout = new QHBoxLayout();
    pTitleLayout->setContentsMargins(18, 0, 20, 0);

    {
        custom_title_ = new CustomTitle(this);
        custom_title_->setVisible(false);
        custom_title_->setButtonVisible(CustomTitle::MinButton, false);

        custom_title_->setTitle(tr("Title"));
        custom_title_->move(8, 8);
        custom_title_->setStyleSheet(
            QLatin1String("QPushButton#windowCloseButton{ \
                                               background: transparent; \
                                               border: none; \
                                               min-height: 4px; \
                                               min-width: 20px; \
                               }\
                                QPushButton#windowCloseButton:hover{ \
                                    min-height: 4px; \
                                    min-width: 20px; \
                                }"));

        pTitleLayout->addWidget(custom_title_);
    }

    QHBoxLayout* pLabelLayout = new QHBoxLayout();
    pLabelLayout->setContentsMargins(20, 18, 0, 0);
    {
        label_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        label_->setOpenExternalLinks(true);
        label_->setWordWrap(true);
        label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label_->setObjectName(QLatin1String("customMessageBoxLabel"));

        pLabelLayout->addWidget(label_);
    }

    QHBoxLayout* pButtonBoxLayout = new QHBoxLayout();
    pButtonBoxLayout->setContentsMargins(179, 35, 20, 0);
    {
        pButtonBoxLayout->addWidget(button_box_);
        button_box_->layout()->setSpacing(10);
        pButtonBoxLayout->addStretch();
    }
    
    main_layout->addLayout(pTitleLayout);
    main_layout->addLayout(pLabelLayout);
    main_layout->addLayout(pButtonBoxLayout);
    main_layout->addStretch();

    resize(337 + 16, 136 + 16);

    QObject::connect(button_box_, &QDialogButtonBox::clicked,
                     this, &CustomMessgeBox::buttonClicked);
}

void CustomMessgeBox::buttonClicked(QAbstractButton *button)
{
    clicked_button_ = button;
    close();
}

void CustomMessgeBox::translateButtonText(QPushButton *button, QMessageBox::StandardButton type)
{
    QString button_text;
    switch (type) {
    case QMessageBox::Ok :
        button_text = tr("Ok");
        break;
    case QMessageBox::Open :
        button_text = tr("Open");
        break;
    case QMessageBox::Save :
        button_text = tr("Save");
        break;
    case QMessageBox::Cancel :
        button_text = tr("Cancel");
        break;
    case QMessageBox::Close :
        button_text = tr("Close");
        break;
    case QMessageBox::Discard :
        button_text = tr("Discard");
        break;
    case QMessageBox::Apply :
        button_text = tr("Apply");
        break;
    case QMessageBox::Reset :
        button_text = tr("Reset");
        break;
    case QMessageBox::RestoreDefaults :
        button_text = tr("RestoreDefaults");
        break;
    case QMessageBox::Help :
        button_text = tr("Help");
        break;
    case QMessageBox::SaveAll :
        button_text = tr("SaveAll");
        break;
    case QMessageBox::Yes :
        button_text = tr("Yes");
        break;
    case QMessageBox::YesToAll :
        button_text = tr("YesToAll");
        break;
    case QMessageBox::No :
        button_text = tr("No");
        break;
    case QMessageBox::NoToAll :
        button_text = tr("NoToAll");
        break;
    case QMessageBox::Abort :
        button_text = tr("Abort");
        break;
    case QMessageBox::Retry :
        button_text = tr("Retry");
        break;
    case QMessageBox::Ignore :
        button_text = tr("Ignore");
        break;
    }
    if(!button_text.isEmpty())
        button->setText(button_text);
    button->setIcon(QIcon());
}

void CustomMessgeBox::setButtonStyleSheet()
{
    QList<QAbstractButton *> all_button = button_box_->buttons();
    if(default_button_ != nullptr)
        default_button_->setStyleSheet(
            QLatin1String("QPushButton{ \
                    background-color: rgb(255, 103, 29);\
                    border-radius: 4px;\
                    opacity: 1;\
                    border: 1px solid rgb(255, 103, 29);\
                    font-size: 12px;\
                    font-family: Microsoft YaHei UI-Regular, Microsoft YaHei UI;\
                    font-weight: 400;\
                    color: rgb(255, 255, 255);\
                    line-height: 16px;\
                    min-height: 34px;\
                    min-width: 64px;\
                }\
                QPushButton:hover{\
                     background-color: rgb(255, 118, 52);}\
                QPushButton:pressed {\
                    background-color: rgb(240, 88, 14);}"));

    for(int i = 0; i < all_button.length(); i++) {
        if(all_button[i] != default_button_)
            all_button[i]->setStyleSheet(
            QLatin1String("QPushButton{ \
                    background-color: rgba(255, 251, 250, 1);\
                    border-radius: 4px;\
                    opacity: 1;\
                    border: 1px solid rgba(255, 103, 29, 1);\
                    font-size: 12px;\
                    font-family: Microsoft YaHei UI-Regular, Microsoft YaHei UI;\
                    font-weight: 400;\
                    color: rgb(255, 103, 29);\
                    line-height: 16px;\
                    min-height: 34px;\
                    min-width: 64px;\
                }\
                QPushButton:hover{\
                     background-color: rgb(255, 239, 231);}\
                QPushButton:pressed {\
                    background-color: rgb(255, 216, 196);}"));
    }
}

void CustomMessgeBox::showEvent(QShowEvent *event)
{
    //custom_title_->resize(width(), 40);
    setButtonStyleSheet();

    QDialog::showEvent(event);
}

void CustomMessgeBox::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QBrush(Qt::white));
    painter.setPen(Qt::transparent);

    int radius = 8;
    int kShadowLen = 8;
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

QMessageBox::StandardButton CustomMessgeBox::standardButton(QAbstractButton *button) const
{
    return static_cast<QMessageBox::StandardButton>(button_box_->standardButton(button));
}

void CustomMessgeBox::setDefaultButton(QAbstractButton *button)
{
    default_button_ = button;
    setButtonStyleSheet();
}

QAbstractButton* CustomMessgeBox::defaultButton() const
{
    return default_button_;
}
