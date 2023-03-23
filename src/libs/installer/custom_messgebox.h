#ifndef CUSTOM_MESSGEBOX_H
#define CUSTOM_MESSGEBOX_H

#include <QMessageBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QDialogButtonBox>

class CustomTitle : public QWidget {
    Q_OBJECT
public:
    enum ButtonType{
        MinButton,
        CloseButton,
        ButtonNums
    };

    class CustomButton : public QPushButton
    {
    public:
        explicit CustomButton(ButtonType type, QWidget *parent = nullptr)
            : QPushButton(parent)
            , type_(type)
        {
            setButtonIcon(false);
            setVisible(true);
            setFlat(true);
        }
        void enterEvent(QEvent *e) override
        {
            setButtonIcon(true);
            QPushButton::enterEvent(e);
        }
        void leaveEvent(QEvent *e) override
        {
            setButtonIcon(false);
            QPushButton::leaveEvent(e);
        }
    private:
        void setButtonIcon(bool focus)
        {
            if (focus) {
                if (type_ == MinButton) {
                    QPixmap pixmap(QLatin1String(":/min_window@2x.png"));
                    setIcon(pixmap.scaled(14, 14));
                }
                else if (type_ == CloseButton) {
                    QPixmap pixmap(QLatin1String(":/close_window@2x.png"));
                    setIcon(pixmap.scaled(14, 14));
                }
            }
            else {
                if (type_ == MinButton) {
                    QPixmap pixmap(QLatin1String(":/min_window_gray@2x.png"));
                    setIcon(pixmap.scaled(14, 14));
                }
                else if (type_ == CloseButton) {
                    QPixmap pixmap(QLatin1String(":/close_window_gray@2x.png"));
                    setIcon(pixmap.scaled(14, 14));
                }
            }
        };
        ButtonType type_;
    };

    explicit CustomTitle(QWidget *parent);

    void setButtonVisible(ButtonType type, bool visible);

    void setTitle(const QString& name);

private:
    void closeButtonClicked()
    {
        this->window()->close();
    };
    void minimizeButtonClicked()
    {
        this->window()->showMinimized();
    };
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseReleaseEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);

    bool is_pressed_;
    QPoint start_move_pos_;
    CustomButton* buttons_[ButtonNums];
    QLabel* title_;
    QHBoxLayout* layout_;
};

class CustomMessgeBox : public QDialog
{
    Q_OBJECT
public:
    explicit CustomMessgeBox(QWidget* parent = nullptr);
    CustomMessgeBox(const QString &title, const QString& text,
                    QMessageBox::StandardButtons buttons = QMessageBox::NoButton,
                    QWidget* parent = nullptr);

    void setWindowTitle(const QString& title);
    void setText(const QString& text);
    void setStandardButtons(QMessageBox::StandardButtons buttons);
    QMessageBox::StandardButton standardButton(QAbstractButton* button) const;

    QAbstractButton* clickedButton() const;
    QPushButton* addButton(QMessageBox::StandardButton button);    

    void setDefaultButton(QAbstractButton * button);
    QAbstractButton* defaultButton() const;
protected:
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent* event) override;
private:
    void init();
    void buttonClicked(QAbstractButton* button);

    void translateButtonText(QPushButton* button, QMessageBox::StandardButton);
    void setButtonStyleSheet();

    CustomTitle* custom_title_;
    QLabel* label_;
    QDialogButtonBox* button_box_;
    QAbstractButton *clicked_button_;
    QAbstractButton *default_button_;
};

#endif // CUSTOM_MESSGEBOX_H
