#include "models.h"

#include <QDateTime>

QString EntryModel::statusName(EntryStatus status) {
    switch (status) {
    case EntryStatus::Same: return "same";
    case EntryStatus::Changed: return "changed";
    case EntryStatus::Deleted: return "deleted";
    case EntryStatus::NotBackedUp: return "new";
    case EntryStatus::Unknown: break;
    }
    return {};
}

QVariant EntryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case NameRole: return entry.name;
    case PathRole: return entry.path;
    case IsDirRole: return entry.isDir;
    case IsLinkRole: return entry.isLink;
    case SizeRole: return entry.size;
    case ModifiedRole: return entry.modified > 0 ? QDateTime::fromSecsSinceEpoch(entry.modified) : QDateTime();
    case StatusRole: return statusName(entry.status);
    }
    return {};
}

QHash<int, QByteArray> EntryModel::roleNames() const {
    return {{NameRole, "name"},       {PathRole, "path"},         {IsDirRole, "isDir"},   {IsLinkRole, "isLink"},
            {SizeRole, "size"},       {ModifiedRole, "modified"}, {StatusRole, "status"}};
}

int EntryModel::find(const QString &name) const {
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).name == name)
            return i;
    return -1;
}

void EntryModel::reset(const QVector<Entry> &entries) {
    const bool countChanges = entries.size() != m_entries.size();
    beginResetModel();
    m_entries = entries;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

QVariant DiffModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_lines.size())
        return {};
    const DiffLine &line = m_lines.at(index.row());
    switch (role) {
    case KindRole:
        switch (line.kind) {
        case DiffLine::Added: return "added";
        case DiffLine::Removed: return "removed";
        case DiffLine::Hunk: return "hunk";
        case DiffLine::Context: return "context";
        }
        break;
    case OldLineRole: return line.oldLine;
    case NewLineRole: return line.newLine;
    case TextRole: return line.text;
    }
    return {};
}

QHash<int, QByteArray> DiffModel::roleNames() const {
    return {{KindRole, "kind"}, {OldLineRole, "oldLine"}, {NewLineRole, "newLine"}, {TextRole, "text"}};
}

void DiffModel::reset(const QVector<DiffLine> &lines) {
    const bool countChanges = lines.size() != m_lines.size();
    beginResetModel();
    m_lines = lines;
    endResetModel();
    if (countChanges)
        emit countChanged();
}
