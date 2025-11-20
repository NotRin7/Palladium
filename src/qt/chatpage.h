#ifndef PALLADIUM_QT_CHATPAGE_H
#define PALLADIUM_QT_CHATPAGE_H

#include <QWidget>
#include <memory>

class WalletModel;
class ChatTableModel;
class PlatformStyle;

namespace Ui {
class ChatPage;
}

class ChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPage(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~ChatPage();

    void setModel(WalletModel *model);
    void setChatModel(ChatTableModel *model);

private Q_SLOTS:
    void on_sendButton_clicked();

private:
    Ui::ChatPage *ui;
    WalletModel *walletModel;
    ChatTableModel *chatModel;
    const PlatformStyle *platformStyle;
};

#endif // PALLADIUM_QT_CHATPAGE_H
