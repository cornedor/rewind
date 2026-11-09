#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "files.h"

class EntryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, IsDirRole, IsLinkRole, SizeRole, ModifiedRole, StatusRole };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_entries.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.size(); }
    const Entry &at(int row) const { return m_entries.at(row); }
    int find(const QString &name) const;
    void reset(const QVector<Entry> &entries);

    static QString statusName(EntryStatus status);

signals:
    void countChanged();

private:
    QVector<Entry> m_entries;
};

class DiffModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { KindRole = Qt::UserRole + 1, OldLineRole, NewLineRole, TextRole };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_lines.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_lines.size(); }
    void reset(const QVector<DiffLine> &lines);

signals:
    void countChanged();

private:
    QVector<DiffLine> m_lines;
};
