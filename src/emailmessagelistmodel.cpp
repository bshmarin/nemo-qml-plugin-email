/*
 * Copyright 2011 Intel Corporation.
 * Copyright (C) 2012 Jolla Ltd.
 *
 * This program is licensed under the terms and conditions of the
 * Apache License, version 2.0.  The full text of the Apache License is at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include <QDateTime>

#include <qmailmessage.h>
#include <qmailmessagekey.h>
#include <qmailstore.h>
#include <qmailserviceaction.h>

#include <qmailnamespace.h>

#include "emailmessagelistmodel.h"
#include "logging_p.h"

EmailMessageListModel::EmailMessageListModel(QObject *parent)
    : QMailMessageListModel(parent),
      m_combinedInbox(false),
      m_canFetchMore(false),
      m_searchLimit(100),
      m_searchOn(EmailMessageListModel::LocalAndRemote),
      m_searchFrom(true),
      m_searchRecipients(true),
      m_searchSubject(true),
      m_searchBody(true),
      m_searchRemainingOnRemote(0),
      m_searchCanceled(false)
{
    roles[QMailMessageModelBase::MessageAddressTextRole] = "sender";
    roles[QMailMessageModelBase::MessageSubjectTextRole] = "subject";
    roles[QMailMessageModelBase::MessageFilterTextRole] = "messageFilter";
    roles[QMailMessageModelBase::MessageTimeStampTextRole] = "timeStamp";
    roles[QMailMessageModelBase::MessageSizeTextRole] = "size";
    roles[QMailMessageModelBase::MessageBodyTextRole] = "body";
    roles[MessageAttachmentCountRole] = "numberOfAttachments";
    roles[MessageAttachmentsRole] = "listOfAttachments";
    roles[MessageRecipientsRole] = "recipients";
    roles[MessageRecipientsDisplayNameRole] = "recipientsDisplayName";
    roles[MessageReadStatusRole] = "readStatus";
    roles[MessageQuotedBodyRole] = "quotedBody";
    roles[MessageIdRole] = "messageId";
    roles[MessageSenderDisplayNameRole] = "senderDisplayName";
    roles[MessageSenderEmailAddressRole] = "senderEmailAddress";
    roles[MessageToRole] = "to";
    roles[MessageCcRole] = "cc";
    roles[MessageBccRole] = "bcc";
    roles[MessageTimeStampRole] = "qDateTime";
    roles[MessageSelectModeRole] = "selected";
    roles[MessagePreviewRole] = "preview";
    roles[MessageTimeSectionRole] = "timeSection";
    roles[MessagePriorityRole] = "priority";
    roles[MessageAccountIdRole] = "accountId";
    roles[MessageHasAttachmentsRole] = "hasAttachments";
    roles[MessageHasCalendarInvitationRole] = "hasCalendarInvitation";
    roles[MessageSizeSectionRole] = "sizeSection";
    roles[MessageFolderIdRole] = "folderId";
    roles[MessageParsedSubject] = "parsedSubject";

    m_key = key();
    m_sortKey = QMailMessageSortKey::timeStamp(Qt::DescendingOrder);
    m_sortBy = Time;
    QMailMessageListModel::setSortKey(m_sortKey);

    connect(this, SIGNAL(rowsInserted(QModelIndex,int,int)),
            this, SIGNAL(countChanged()));
    connect(this, SIGNAL(rowsRemoved(QModelIndex,int,int)),
            this, SIGNAL(countChanged()));
    connect(this, SIGNAL(modelReset()),
            this, SIGNAL(countChanged()));

    connect(QMailStore::instance(), SIGNAL(messagesAdded(QMailMessageIdList)),
            this, SLOT(messagesAdded(QMailMessageIdList)));

    connect(QMailStore::instance(), SIGNAL(messagesRemoved(QMailMessageIdList)),
            this, SLOT(messagesRemoved(QMailMessageIdList)));

    connect(QMailStore::instance(), SIGNAL(accountsUpdated(QMailAccountIdList)),
            this, SLOT(accountsChanged()));

    connect(EmailAgent::instance(), SIGNAL(searchCompleted(QString,const QMailMessageIdList&,bool,int,EmailAgent::SearchStatus)),
            this, SLOT(onSearchCompleted(QString,const QMailMessageIdList&,bool,int,EmailAgent::SearchStatus)));

    m_remoteSearchTimer.setSingleShot(true);
    connect(&m_remoteSearchTimer, SIGNAL(timeout()), this, SLOT(searchOnline()));
}

EmailMessageListModel::~EmailMessageListModel()
{
}

QHash<int, QByteArray> EmailMessageListModel::roleNames() const
{
    return roles;
}

int EmailMessageListModel::rowCount(const QModelIndex & parent) const
{
    return QMailMessageListModel::rowCount(parent);
}

QVariant EmailMessageListModel::data(const QModelIndex & index, int role) const
{
    if (!index.isValid() || index.row() > rowCount(parent(index))) {
        qCWarning(lcEmail) << Q_FUNC_INFO << "Invalid Index";
        return QVariant();
    }

    QMailMessageId msgId = idFromIndex(index);

    if (role == QMailMessageModelBase::MessageBodyTextRole) {
        QMailMessage message(msgId);
        return EmailAgent::instance()->bodyPlainText(message);
    } else if (role == MessageQuotedBodyRole) {
        QMailMessage message (msgId);
        QString body = EmailAgent::instance()->bodyPlainText(message);
        body.prepend('\n');
        body.replace('\n', "\n>");
        body.truncate(body.size() - 1);  // remove the extra ">" put there by QString.replace
        return body;
    } else if (role == MessageIdRole) {
        return msgId.toULongLong();
    } else if (role == MessageToRole) {
        QMailMessage message(msgId);
        return QMailAddress::toStringList(message.to());
    } else if (role == MessageCcRole) {
        QMailMessage message(msgId);
        return QMailAddress::toStringList(message.cc());
    } else if (role == MessageBccRole) {
        QMailMessage message(msgId);
        return QMailAddress::toStringList(message.bcc());
    } else if (role == MessageSelectModeRole) {
        return (m_selectedMsgIds.contains(index.row()));
    }

    QMailMessageMetaData messageMetaData(msgId);

    if (role == QMailMessageModelBase::MessageTimeStampTextRole) {
        QDateTime timeStamp = messageMetaData.date().toLocalTime();
        return (timeStamp.toString("hh:mm MM/dd/yyyy"));
    } else if (role == MessageAttachmentCountRole) {
        // return number of attachments
        if (!messageMetaData.status() & QMailMessageMetaData::HasAttachments)
            return 0;

        QMailMessage message(msgId);
        const QList<QMailMessagePart::Location> &attachmentLocations = message.findAttachmentLocations();
        return attachmentLocations.count();
    } else if (role == MessageAttachmentsRole) {
        // return a stringlist of attachments
        if (!messageMetaData.status() & QMailMessageMetaData::HasAttachments)
            return QStringList();

        QMailMessage message(msgId);
        QStringList attachments;
        for (const QMailMessagePart::Location &location : message.findAttachmentLocations()) {
            const QMailMessagePart &attachmentPart = message.partAt(location);
            attachments << attachmentPart.displayName();
        }
        return attachments;
    } else if (role == MessageRecipientsRole) {
        QStringList recipients;
        QList<QMailAddress> addresses = messageMetaData.recipients();
        for (const QMailAddress &address : addresses) {
            recipients << address.address();
        }
        return recipients;
    } else if (role == MessageRecipientsDisplayNameRole) {
        QStringList recipients;
        QList<QMailAddress> addresses = messageMetaData.recipients();
        for (const QMailAddress &address : addresses) {
            if (address.name().isEmpty()) {
                recipients << address.address();
            } else {
                recipients << address.name();
            }
        }
        return recipients;
    } else if (role == MessageReadStatusRole) {
        return (messageMetaData.status() & QMailMessage::Read) != 0;
    } else if (role == MessageSenderDisplayNameRole) {
        if (messageMetaData.from().name().isEmpty()) {
            return messageMetaData.from().address();
        } else {
            return messageMetaData.from().name();
        }
    } else if (role == MessageSenderEmailAddressRole) {
        return messageMetaData.from().address();
    } else if (role == MessageTimeStampRole) {
        return (messageMetaData.date().toLocalTime());
    } else if (role == MessagePreviewRole) {
        return messageMetaData.preview().simplified();
    } else if (role == MessageTimeSectionRole) {
        static QDate lastDate(QDate::currentDate());

        // The value of this property depends on the current date; if that changes, we need to notify the update
        QDate now(QDate::currentDate());
        if (now != lastDate) {
            lastDate = now;
            QMetaObject::invokeMethod(const_cast<EmailMessageListModel *>(this), "notifyDateChanged");
        }

        const int daysDiff = now.toJulianDay() - (messageMetaData.date().toLocalTime()).date().toJulianDay();
        if (daysDiff < 7) {
            return (messageMetaData.date().toLocalTime()).date();
        } else {
            //returns epoch time for items older than a week
            return QDateTime::fromTime_t(0);
        }
    } else if (role == MessagePriorityRole) {
        if (messageMetaData.status() & QMailMessage::HighPriority) {
            return HighPriority;
        } else if (messageMetaData.status() & QMailMessage::LowPriority) {
            return LowPriority;
        } else {
            return NormalPriority;
        }
    } else if (role == MessageAccountIdRole) {
        return messageMetaData.parentAccountId().toULongLong();
    } else if (role == MessageHasAttachmentsRole) {
        return (messageMetaData.status() & QMailMessageMetaData::HasAttachments) != 0;
    } else if (role == MessageHasCalendarInvitationRole) {
        return (messageMetaData.status() & QMailMessageMetaData::CalendarInvitation) != 0;
    } else if (role == MessageSizeSectionRole) {
        const uint size(messageMetaData.size());

        if (size < 100 * 1024) { // <100 KB
            return 0;
        } else if (size < 500 * 1024) { // <500 KB
            return 1;
        } else { // >500 KB
            return 2;
        }
    } else if (role == MessageFolderIdRole) {
        return messageMetaData.parentFolderId().toULongLong();
    } else if (role == MessageParsedSubject) {
        // Filter <img> and <ahref> html tags to make the text suitable to be displayed in a qml
        // label using StyledText(allows only small subset of html)
        QString subject = data(index, QMailMessageModelBase::MessageSubjectTextRole).toString();
        subject.replace(QRegExp("<\\s*img", Qt::CaseInsensitive), "<no-img");
        subject.replace(QRegExp("<\\s*a", Qt::CaseInsensitive), "<no-a");
        return subject;
    }

    return QMailMessageListModel::data(index, role);
}

int EmailMessageListModel::count() const
{
    return rowCount();
}

void EmailMessageListModel::setSearch(const QString &search)
{
    if (search.isEmpty()) {
        m_searchKey = QMailMessageKey::nonMatchingKey();
        setKey(m_searchKey);
        m_search = search;
        cancelSearch();
    } else {
        if (m_search == search)
            return;

        QMailMessageKey tempKey;
        if (m_searchFrom) {
            tempKey |= QMailMessageKey::sender(search, QMailDataComparator::Includes);
        }
        if (m_searchRecipients) {
            tempKey |= QMailMessageKey::recipients(search, QMailDataComparator::Includes);
        }
        if (m_searchSubject) {
            tempKey |= QMailMessageKey::subject(search, QMailDataComparator::Includes);
        }
        if (m_searchBody) {
            tempKey |= QMailMessageKey::preview(search, QMailDataComparator::Includes);
        }

        m_searchCanceled = false;
        // All options are disabled, nothing to search
        if (tempKey.isEmpty()) {
            return;
        }
        m_searchKey = QMailMessageKey(m_key & tempKey);
        m_search = search;
        setSearchRemainingOnRemote(0);

        if (m_searchOn == EmailMessageListModel::Remote) {
            setKey(QMailMessageKey::nonMatchingKey());
            EmailAgent::instance()->searchMessages(m_searchKey, m_search, QMailSearchAction::Remote, m_searchLimit, m_searchBody);
        } else {
            setKey(m_searchKey);
            // We have model filtering already via searchKey, so when doing body search we pass just the current model key plus body search,
            // otherwise results will be merged and just entries with both, fields and body matches will be returned.
            EmailAgent::instance()->searchMessages(m_searchBody ? m_key : m_searchKey, m_search, QMailSearchAction::Local, m_searchLimit, m_searchBody);
        }
    }
}

void EmailMessageListModel::cancelSearch()
{
    // Cancel also remote search since it can be trigger later by the timer
    m_searchCanceled = true;
    EmailAgent::instance()->cancelSearch();
}

void EmailMessageListModel::setFolderKey(int id, QMailMessageKey messageKey)
{
    m_currentFolderId = QMailFolderId(id);
    if (!m_currentFolderId.isValid())
        return;
    // Local folders (e.g outbox) can have messages from several accounts.
    QMailMessageKey accountKey = QMailMessageKey::parentAccountId(m_mailAccountIds);
    QMailMessageKey folderKey = accountKey & QMailMessageKey::parentFolderId(m_currentFolderId);
    QMailMessageListModel::setKey(folderKey & messageKey);
    m_key = key();
    QMailMessageListModel::setSortKey(m_sortKey);

    if (combinedInbox())
        setCombinedInbox(false);

    emit countChanged();
    checkFetchMoreChanged();
}

void EmailMessageListModel::setAccountKey(int id, bool defaultInbox)
{
    QMailAccountId accountId = QMailAccountId(id);
    if (!accountId.isValid()) {
        //If accountId is invalid, empty key will be set.
        QMailMessageListModel::setKey(QMailMessageKey::nonMatchingKey());
    } else {
        m_mailAccountIds.clear();
        m_mailAccountIds.append(accountId);

        QMailMessageKey accountKey = QMailMessageKey::parentAccountId(accountId);
        QMailMessageListModel::setKey(accountKey);
        if (defaultInbox) {
            QMailAccount account(accountId);
            QMailFolderId folderId = account.standardFolder(QMailFolder::InboxFolder);
            if (folderId.isValid()) {
                // default to INBOX
                QMailMessageKey folderKey = QMailMessageKey::parentFolderId(folderId);
                QMailMessageListModel::setKey(folderKey);
            } else {
                QMailMessageListModel::setKey(QMailMessageKey::nonMatchingKey());
                connect(QMailStore::instance(), SIGNAL(foldersAdded(const QMailFolderIdList &)),
                        this, SLOT(foldersAdded(const QMailFolderIdList &)));
            }
        }
    }
    QMailMessageListModel::setSortKey(m_sortKey);

    m_key = key();

    if (combinedInbox())
        setCombinedInbox(false);

    emit countChanged();
    checkFetchMoreChanged();
}

void EmailMessageListModel::foldersAdded(const QMailFolderIdList &folderIds)
{
    QMailFolderId folderId;
    for (const QMailFolderId &mailFolderId : folderIds) {
        QMailFolder folder(mailFolderId);
        if (m_mailAccountIds.contains(folder.parentAccountId())) {
            QMailAccount account(folder.parentAccountId());
            folderId = account.standardFolder(QMailFolder::InboxFolder);
            break;
        }
    }
    if (folderId.isValid()) {
        // default to INBOX
        QMailMessageKey folderKey = QMailMessageKey::parentFolderId(folderId);
        QMailMessageListModel::setKey(folderKey);
        disconnect(QMailStore::instance(), SIGNAL(foldersAdded(const QMailFolderIdList &)),
                   this, SLOT(foldersAdded(const QMailFolderIdList &)));
        m_key = key();
    }
}

EmailMessageListModel::Sort EmailMessageListModel::sortBy() const
{
    return m_sortBy;
}

bool EmailMessageListModel::unreadMailsSelected() const
{
    return !m_selectedUnreadIdx.isEmpty();
}

void EmailMessageListModel::notifyDateChanged()
{
    dataChanged(index(0), index(rowCount() - 1), QVector<int>() << MessageTimeSectionRole);
}

void EmailMessageListModel::setSortBy(EmailMessageListModel::Sort sort)
{
    Qt::SortOrder order = Qt::AscendingOrder;
    switch (sort) {
    case Time:
    case Attachments:
    case Priority:
    case Size:
        order = Qt::DescendingOrder;
    default:
        break;
    }

    sortByOrder(order, sort);
}

// Always sorts by Qt::DescendingOrder
void EmailMessageListModel::sortByOrder(Qt::SortOrder sortOrder, EmailMessageListModel::Sort sortBy)
{
    switch (sortBy) {
    case Attachments:
        m_sortKey = QMailMessageSortKey::status(QMailMessage::HasAttachments, sortOrder);
        break;
    case Priority:
        if (sortOrder == Qt::AscendingOrder) {
            m_sortKey = QMailMessageSortKey::status(QMailMessage::HighPriority, sortOrder) &
                      QMailMessageSortKey::status(QMailMessage::LowPriority, Qt::DescendingOrder);
        } else {
            m_sortKey = QMailMessageSortKey::status(QMailMessage::HighPriority, sortOrder) &
                    QMailMessageSortKey::status(QMailMessage::LowPriority, Qt::AscendingOrder);
        }
        break;
    case ReadStatus:
        m_sortKey = QMailMessageSortKey::status(QMailMessage::Read, sortOrder);
        break;
    case Recipients:
        m_sortKey = QMailMessageSortKey::recipients(sortOrder);
        break;
    case Sender:
        m_sortKey = QMailMessageSortKey::sender(sortOrder);
        break;
    case Size:
        m_sortKey = QMailMessageSortKey::size(sortOrder);
        break;
    case Subject:
        m_sortKey = QMailMessageSortKey::subject(sortOrder);
        break;
    case Time:
        m_sortKey = QMailMessageSortKey::timeStamp(sortOrder);
        break;
    default:
        qCWarning(lcEmail) << Q_FUNC_INFO << "Invalid sort type provided.";
        return;
    }

    m_sortBy = sortBy;

    if (sortBy != Time) {
        m_sortKey &= QMailMessageSortKey::timeStamp(Qt::DescendingOrder);
    }
    QMailMessageListModel::setSortKey(m_sortKey);
    emit sortByChanged();
}

int EmailMessageListModel::accountIdForMessage(int messageId)
{
    QMailMessageId msgId(messageId);
    QMailMessageMetaData metaData(msgId);
    return metaData.parentAccountId().toULongLong();
}

int EmailMessageListModel::folderIdForMessage(int messageId)
{
    QMailMessageId msgId(messageId);
    QMailMessageMetaData metaData(msgId);
    return metaData.parentFolderId().toULongLong();
}

int EmailMessageListModel::indexFromMessageId(int messageId)
{
    QMailMessageId msgId(messageId);
    for (int row = 0; row < rowCount(); row++) {
        QVariant vMsgId = data(index(row), QMailMessageModelBase::MessageIdRole);
        
        if (msgId == vMsgId.value<QMailMessageId>())
            return row;
    }
    return -1;
}

void EmailMessageListModel::selectAllMessages()
{
    for (int row = 0; row < rowCount(); row++) {
        selectMessage(row);
    }
}

void EmailMessageListModel::deSelectAllMessages()
{
    if (!m_selectedMsgIds.size())
        return;

    QMutableMapIterator<int, QMailMessageId> iter(m_selectedMsgIds);
    while (iter.hasNext()) {
        iter.next();
        int idx = iter.key();
        iter.remove();
        dataChanged(index(idx), index(idx), QVector<int>() << MessageSelectModeRole);
    }
    m_selectedUnreadIdx.clear();
    emit unreadMailsSelectedChanged();
}

void EmailMessageListModel::selectMessage(int idx)
{
    QMailMessageId msgId = idFromIndex(index(idx));

    if (!m_selectedMsgIds.contains(idx)) {
        m_selectedMsgIds.insert(idx, msgId);
        dataChanged(index(idx), index(idx), QVector<int>() << MessageSelectModeRole);
    }

    bool messageRead = data(index(idx), MessageReadStatusRole).toBool();
    if (m_selectedUnreadIdx.isEmpty() && !messageRead) {
        m_selectedUnreadIdx.append(idx);
        emit unreadMailsSelectedChanged();
    } else if (!messageRead) {
        m_selectedUnreadIdx.append(idx);
    }
}

void EmailMessageListModel::deSelectMessage(int idx)
{
    if (m_selectedMsgIds.contains(idx)) {
        m_selectedMsgIds.remove(idx);
        dataChanged(index(idx), index(idx), QVector<int>() << MessageSelectModeRole);
    }

    if (m_selectedUnreadIdx.contains(idx)) {
        m_selectedUnreadIdx.removeOne(idx);
        if (m_selectedUnreadIdx.isEmpty()) {
            emit unreadMailsSelectedChanged();
        }
    }
}

void EmailMessageListModel::moveSelectedMessageIds(int vFolderId)
{
    if (m_selectedMsgIds.empty())
        return;

    const QMailFolderId id(vFolderId);
    if (id.isValid()) {
        EmailAgent::instance()->moveMessages(m_selectedMsgIds.values(), id);
    }
    deSelectAllMessages();
}

void EmailMessageListModel::deleteSelectedMessageIds()
{
    if (m_selectedMsgIds.empty())
        return;

    EmailAgent::instance()->deleteMessages(m_selectedMsgIds.values());
    deSelectAllMessages();
}

void EmailMessageListModel::markAsReadSelectedMessagesIds()
{
    if (m_selectedMsgIds.empty())
        return;

    EmailAgent::instance()->setMessagesReadState(m_selectedMsgIds.values(), true);
    deSelectAllMessages();
}

void EmailMessageListModel::markAsUnReadSelectedMessagesIds()
{
    if (m_selectedMsgIds.empty())
        return;

    EmailAgent::instance()->setMessagesReadState(m_selectedMsgIds.values(), false);
    deSelectAllMessages();
}

void EmailMessageListModel::markAllMessagesAsRead()
{
    if (rowCount()) {
        QMailAccountIdList accountIdList;
        QMailMessageIdList msgIds;
        quint64 status(QMailMessage::Read);

        for (int row = 0; row < rowCount(); row++) {
            if (!data(index(row), MessageReadStatusRole).toBool()) {
                QMailMessageId id = (data(index(row), QMailMessageModelBase::MessageIdRole)).value<QMailMessageId>();
                msgIds.append(id);

                QMailAccountId accountId = (data(index(row), MessageAccountIdRole)).value<QMailAccountId>();
                if (!accountIdList.contains(accountId)) {
                    accountIdList.append(accountId);
                }
            }
        }
        if (msgIds.size()) {
            QMailStore::instance()->updateMessagesMetaData(QMailMessageKey::id(msgIds), status, true);
        }
        for (const QMailAccountId &accId : accountIdList) {
            EmailAgent::instance()->exportUpdates(QMailAccountIdList() << accId);
        }
    }
}

bool EmailMessageListModel::canFetchMore() const
{
    return m_canFetchMore;
}

bool EmailMessageListModel::combinedInbox() const
{
    return m_combinedInbox;
}

void EmailMessageListModel::setCombinedInbox(bool c, bool forceUpdate)
{
    if (!forceUpdate && c == m_combinedInbox) {
        return;
    }

    m_mailAccountIds = QMailStore::instance()->queryAccounts(QMailAccountKey::messageType(QMailMessage::Email)
                                                             & QMailAccountKey::status(QMailAccount::Enabled),
                                                             QMailAccountSortKey::name());
    QMailMessageKey excludeRemovedKey = QMailMessageKey::status(QMailMessage::Removed,  QMailDataComparator::Excludes);
    QMailMessageKey excludeReadKey = QMailMessageKey::status(QMailMessage::Read, QMailDataComparator::Excludes);

    if (c) {
        QMailFolderIdList folderIds;
        for (const QMailAccountId &accountId : m_mailAccountIds) {
            QMailAccount account(accountId);
            QMailFolderId foldId = account.standardFolder(QMailFolder::InboxFolder);
            if (foldId.isValid())
                folderIds << account.standardFolder(QMailFolder::InboxFolder);
        }

        QMailFolderKey inboxKey = QMailFolderKey::id(folderIds, QMailDataComparator::Includes);
        QMailMessageKey messageKey = QMailMessageKey::parentFolderId(inboxKey) & excludeRemovedKey;

        QMailMessageKey unreadKey = QMailMessageKey::parentFolderId(inboxKey)
                & excludeReadKey
                & excludeRemovedKey;
        QMailMessageListModel::setKey(unreadKey);

        m_combinedInbox = true;
        m_key = key();
    } else {
        QMailMessageKey accountKey;

        accountKey = QMailMessageKey::parentAccountId(m_mailAccountIds)
                & excludeReadKey
                & excludeRemovedKey;
        QMailMessageListModel::setKey(accountKey);
        m_key = key();
        QMailMessageListModel::setSortKey(m_sortKey);
        m_combinedInbox = false;
    }
    emit combinedInboxChanged();
}

uint EmailMessageListModel::limit() const
{
    return QMailMessageListModel::limit();
}

void EmailMessageListModel::setLimit(uint limit)
{
    if (limit != this->limit()) {
        QMailMessageListModel::setLimit(limit);
        emit limitChanged();
        checkFetchMoreChanged();
    }
}

uint EmailMessageListModel::searchLimit() const
{
    return m_searchLimit;
}

void EmailMessageListModel::setSearchLimit(uint limit)
{
    if (limit != m_searchLimit) {
        m_searchLimit = limit;
        emit searchLimitChanged();
    }
}

EmailMessageListModel::SearchOn EmailMessageListModel::searchOn() const
{
    return m_searchOn;
}

void EmailMessageListModel::setSearchOn(EmailMessageListModel::SearchOn value)
{
    if (value != m_searchOn) {
        m_searchOn = value;
        emit searchOnChanged();
    }
}

bool EmailMessageListModel::searchFrom() const
{
    return m_searchFrom;
}

void EmailMessageListModel::setSearchFrom(bool value)
{
    if (value != m_searchFrom) {
        m_searchFrom = value;
        emit searchFromChanged();
    }
}

bool EmailMessageListModel::searchRecipients() const
{
    return m_searchRecipients;
}

void EmailMessageListModel::setSearchRecipients(bool value)
{
    if (value != m_searchRecipients) {
        m_searchRecipients = value;
        emit searchRecipientsChanged();
    }
}

bool EmailMessageListModel::searchSubject() const
{
    return m_searchSubject;
}

void EmailMessageListModel::setSearchSubject(bool value)
{
    if (value != m_searchSubject) {
        m_searchSubject = value;
        emit searchSubjectChanged();
    }
}

bool EmailMessageListModel::searchBody() const
{
    return m_searchBody;
}

void EmailMessageListModel::setSearchBody(bool value)
{
    if (value != m_searchBody) {
        m_searchBody = value;
        emit searchBodyChanged();
    }
}

int EmailMessageListModel::searchRemainingOnRemote() const
{
    return m_searchRemainingOnRemote;
}

void EmailMessageListModel::setSearchRemainingOnRemote(int count)
{
    if (count != m_searchRemainingOnRemote) {
        m_searchRemainingOnRemote = count;
        emit searchRemainingOnRemoteChanged();
    }
}

void EmailMessageListModel::checkFetchMoreChanged()
{
    if (limit()) {
        bool canFetchMore = QMailMessageListModel::totalCount() > rowCount();
        if (canFetchMore != m_canFetchMore) {
            m_canFetchMore = canFetchMore;
            emit canFetchMoreChanged();
        }
    } else if (m_canFetchMore) {
        m_canFetchMore = false;
        emit canFetchMoreChanged();
    }
}

void EmailMessageListModel::messagesAdded(const QMailMessageIdList &ids)
{
    Q_UNUSED(ids);

    if (limit() > 0 && !m_canFetchMore) {
        checkFetchMoreChanged();
    }
}

void EmailMessageListModel::messagesRemoved(const QMailMessageIdList &ids)
{
    Q_UNUSED(ids);

    if (limit() > 0 && m_canFetchMore) {
        checkFetchMoreChanged();
    }
}

void EmailMessageListModel::searchOnline()
{
    // Check if the search term did not change yet,
    // if changed we skip online search until local search returns again
    if (!m_searchCanceled && (m_remoteSearch == m_search)) {
        qCDebug(lcEmail) << "Starting remote search for" << m_search;
        EmailAgent::instance()->searchMessages(m_searchKey, m_search, QMailSearchAction::Remote, m_searchLimit, m_searchBody);
    }
}

void EmailMessageListModel::onSearchCompleted(const QString &search, const QMailMessageIdList &matchedIds,
                                              bool isRemote, int remainingMessagesOnRemote, EmailAgent::SearchStatus status)
{
    if (m_search.isEmpty()) {
        return;
    }

    if (search != m_search) {
        qCDebug(lcEmail) << "Search terms are different, skipping. Received:" << search << "Have:" << m_search;
        return;
    }
    switch (status) {
    case EmailAgent::SearchDone:
        if (isRemote) {
            // Append online search results to local ones
            setKey(key() | QMailMessageKey::id(matchedIds));
            setSearchRemainingOnRemote(remainingMessagesOnRemote);
            qCDebug(lcEmail) << "We have more messages on remote, remaining count:" << remainingMessagesOnRemote;
        } else {
            setKey(m_searchKey | QMailMessageKey::id(matchedIds));
            if ((m_searchOn == EmailMessageListModel::LocalAndRemote) && EmailAgent::instance()->isOnline() && !m_searchCanceled) {
                m_remoteSearch = search;
                // start online search after 2 seconds to avoid flooding the server with incomplete queries
                m_remoteSearchTimer.start(2000);
            } else if (!EmailAgent::instance()->isOnline()) {
                qCDebug(lcEmail) << "Device is offline, not performing online search";
            }
        }
        break;
    case EmailAgent::SearchCanceled:
        break;
    case EmailAgent::SearchFailed:
        break;
    default:
        break;
    }
}

void EmailMessageListModel::accountsChanged()
{
    if (!m_combinedInbox) {
        return;
    }

    setCombinedInbox(true, true);
}
