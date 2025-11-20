#include <qt/chattablemodel.h>

ChatTableModel::ChatTableModel(QObject *parent) : QAbstractTableModel(parent)
{
}

int ChatTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return messages.size();
}

int ChatTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 3;
}

QVariant ChatTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= messages.size())
        return QVariant();

    const ChatMessage &msg = messages[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Timestamp:
            return msg.timestamp.toString(Qt::SystemLocaleShortDate);
        case Sender:
            return msg.sender;
        case Message:
            return msg.message;
        }
    } else if (role == Qt::TextAlignmentRole) {
        return Qt::AlignLeft + Qt::AlignVCenter;
    }

    return QVariant();
}

QVariant ChatTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
        case Timestamp:
            return tr("Time");
        case Sender:
            return tr("Sender");
        case Message:
            return tr("Message");
        }
    }
    return QVariant();
}

void ChatTableModel::addMessage(const QString &sender, const QString &message, bool isIncoming, const QDateTime &timestamp)
{
    beginInsertRows(QModelIndex(), messages.size(), messages.size());
    ChatMessage msg;
    msg.sender = sender;
    msg.message = message;
    msg.timestamp = timestamp;
    msg.isIncoming = isIncoming;
    messages.append(msg);
    endInsertRows();
}
