#include <qt/chatpage.h>
#include <qt/forms/ui_chatpage.h>
#include <qt/walletmodel.h>
#include <qt/chattablemodel.h>
#include <qt/platformstyle.h>
#include <qt/guiutil.h>
#include <wallet/rpcwallet.h> // For sendchatmessage if calling directly, or better via WalletModel

#include <QMessageBox>
#include <QDateTime>

// Note: You need to implement sendChatMessage in WalletModel or call the RPC logic here.
// For this example, we assume a helper function or direct RPC call wrapper exists.

ChatPage::ChatPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ChatPage),
    platformStyle(platformStyle),
    walletModel(nullptr),
    chatModel(nullptr)
{
    ui->setupUi(this);
    
    // Connect signals if not using auto-connect slots
    connect(ui->sendButton, &QPushButton::clicked, this, &ChatPage::on_sendButton_clicked);
}

ChatPage::~ChatPage()
{
    delete ui;
}

void ChatPage::setModel(WalletModel *model)
{
    this->walletModel = model;
}

void ChatPage::setChatModel(ChatTableModel *model)
{
    this->chatModel = model;
    if(model) {
        ui->messageListView->setModel(model);
    }
}

void ChatPage::on_sendButton_clicked()
{
    if(!walletModel || !chatModel) return;

    QString address = ui->addressEdit->text();
    QString message = ui->messageEdit->text();

    if(address.isEmpty() || message.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please enter address and message."));
        return;
    }

    // TODO: Implement the actual sending logic.
    // Ideally, WalletModel should expose a method: walletModel->sendChatMessage(address, message);
    // Since we implemented it as an RPC command, we can technically invoke it via the RPC interface 
    // or move the logic to WalletModel.
    
    // Mock implementation for UI testing:
    /*
    try {
        // walletModel->sendChatMessage(address, message); // Needs to be implemented in WalletModel
        chatModel->addMessage("Me", message, false, QDateTime::currentDateTime());
        ui->messageEdit->clear();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Error"), QString::fromStdString(e.what()));
    }
    */
   
   // For now, let's just show a message that it's not fully wired up in this snippet
   QMessageBox::information(this, tr("Info"), tr("Send logic needs to be connected to WalletModel."));
}
