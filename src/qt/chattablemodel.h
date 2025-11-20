#ifndef PALLADIUM_QT_CHATTABLEMODEL_H
#define PALLADIUM_QT_CHATTABLEMODEL_H

#include <QAbstractTableModel>
#include <QString>
#include <QList>
#include <QDateTime>

class ChatTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ChatTableModel(QObject *parent = nullptr);

    enum ColumnIndex {
        Timestamp = 0,
        Sender = 1,
        Message = 2
    };

    struct ChatMessage {
        QDateTime timestamp;
        QString sender;
        QString message;
        bool isIncoming;
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

public Q_SLOTS:
    void addMessage(const QString &sender, const QString &message, bool isIncoming, const QDateTime &timestamp = QDateTime::currentDateTime());

private:
    QList<ChatMessage> messages;
};

#endif // PALLADIUM_QT_CHATTABLEMODEL_H
