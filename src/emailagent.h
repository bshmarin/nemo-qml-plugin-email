/*
 * Copyright 2011 Intel Corporation.
 *
 * This program is licensed under the terms and conditions of the
 * Apache License, version 2.0.  The full text of the Apache License is at 	
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef EMAILAGENT_H
#define EMAILAGENT_H

#include <QProcess>
#include <QTimer>
#include <QNetworkConfigurationManager>
#include <QNetworkSession>

#include <qmailaccount.h>
#include <qmailstore.h>
#include <qmailserviceaction.h>

#include "emailaction.h"

class Q_DECL_EXPORT EmailAgent : public QObject
{
    Q_OBJECT
    Q_ENUMS(Status)
    Q_ENUMS(AttachmentStatus)
    Q_ENUMS(SyncErrors)
public:
    static EmailAgent *instance();

    explicit EmailAgent(QObject *parent = 0);
    ~EmailAgent();

    Q_PROPERTY(bool synchronizing READ synchronizing NOTIFY synchronizingChanged)
    Q_PROPERTY(int currentSynchronizingAccountId READ currentSynchronizingAccountId NOTIFY currentSynchronizingAccountIdChanged)

    enum Status {
        Synchronizing = 0,
        Completed,
        Error
    };

    enum AttachmentStatus {
        NotDownloaded = 0,
        Queued,
        Downloaded,
        Downloading,
        Failed,
        FailedToSave
    };

    enum SyncErrors {
        SyncFailed = 0,
        LoginFailed,
        DiskFull
    };

    int currentSynchronizingAccountId() const;
    EmailAgent::AttachmentStatus attachmentDownloadStatus(const QString &attachmentLocation);
    int attachmentDownloadProgress(const QString &attachmentLocation);
    QString attachmentName(const QMailMessagePart &part) const;
    QString bodyPlainText(const QMailMessage &mailMsg) const;
    bool backgroundProcess() const;
    void setBackgroundProcess(const bool isBackgroundProcess);
    void downloadMessages(const QMailMessageIdList &messageIds, QMailRetrievalAction::RetrievalSpecification spec);
    void downloadMessagePart(const QMailMessagePartContainer::Location &location);
    void exportUpdates(const QMailAccountId accountId);
    bool hasMessagesInOutbox(const QMailAccountId accountId);
    void initMailServer();
    bool ipcConnected();
    bool synchronizing() const;
    void flagMessages(const QMailMessageIdList &ids, quint64 setMask, quint64 unsetMask);
    void moveMessages(const QMailMessageIdList &ids, const QMailFolderId &destinationId);
    quint64 newAction();
    void sendMessages(const QMailAccountId &accountId);
    void setupAccountFlags();
    int standardFolderId(int accountId, QMailFolder::StandardFolder folder) const;
    void syncAccounts(const QMailAccountIdList &accountIdList, const bool syncOnlyInbox = true, const uint minimum = 20);

    Q_INVOKABLE void accountsSync(const bool syncOnlyInbox = false, const uint minimum = 20);
    Q_INVOKABLE void cancelSync();
    Q_INVOKABLE void createFolder(const QString &name, int mailAccountId, int parentFolderId);
    Q_INVOKABLE void deleteFolder(int folderId);
    Q_INVOKABLE void deleteMessage(int messageId);
    Q_INVOKABLE void deleteMessages(const QMailMessageIdList &ids);
    Q_INVOKABLE void downloadAttachment(int messageId, const QString &attachmentlocation);
    Q_INVOKABLE void exportUpdates(int accountId);
    Q_INVOKABLE void getMoreMessages(int folderId, uint minimum = 20);
    Q_INVOKABLE QString signatureForAccount(int accountId);
    Q_INVOKABLE int inboxFolderId(int accountId);
    Q_INVOKABLE int outboxFolderId(int accountId);
    Q_INVOKABLE int draftsFolderId(int accountId);
    Q_INVOKABLE int sentFolderId(int accountId);
    Q_INVOKABLE int trashFolderId(int accountId);
    Q_INVOKABLE int junkFolderId(int accountId);
    Q_INVOKABLE bool isAccountValid(int accountId);
    Q_INVOKABLE bool isMessageValid(int messageId);
    Q_INVOKABLE void markMessageAsRead(int messageId);
    Q_INVOKABLE void markMessageAsUnread(int messageId);
    Q_INVOKABLE void moveMessage(int messageId, int destinationId);
    Q_INVOKABLE void renameFolder(int folderId, const QString &name);
    Q_INVOKABLE void retrieveFolderList(int accountId, int folderId = 0, const bool descending = true);
    Q_INVOKABLE void retrieveMessageList(int accountId, int folderId, const uint minimum = 20);
    Q_INVOKABLE void retrieveMessageRange(int messageId, uint minimum);
    Q_INVOKABLE void purgeSendingQueue(int accountId);
    Q_INVOKABLE void synchronize(int accountId);
    Q_INVOKABLE void synchronizeInbox(int accountId, const uint minimum = 20);

signals:
    void currentSynchronizingAccountIdChanged();
    void attachmentDownloadProgressChanged(const QString &attachmentLocation, int progress);
    void attachmentDownloadStatusChanged(const QString &attachmentLocation, EmailAgent::AttachmentStatus status);
    void attachmentUrlChanged(const QString &attachmentLocation, const QString &url);
    void error(int accountId, EmailAgent::SyncErrors syncError);
    void folderRetrievalCompleted(const QMailAccountId &accountId);
    void ipcConnectionEstablished();
    void messagesDownloaded(const QMailMessageIdList &messageIds, bool success);
    void messagePartDownloaded(const QMailMessageId &messageId, const QString &partLocation, bool success);
    void progressUpdated(int percent);
    void sendCompleted();
    void standardFoldersCreated(const QMailAccountId &accountId);
    void synchronizingChanged(EmailAgent::Status status);
    void networkConnectionRequested();

private slots:
    void activityChanged(QMailServiceAction::Activity activity);
    void onIpcConnectionEstablished();
    void onMessageServerProcessError(QProcess::ProcessError error);
    void onOnlineStateChanged(bool isOnline);
    void onStandardFoldersCreated(const QMailAccountId &accountId);
    void progressChanged(uint value, uint total);

private:
    static EmailAgent *m_instance;

    uint m_actionCount;
    uint m_accountSynchronizing;
    bool m_transmitting;
    bool m_cancelling;
    bool m_synchronizing;
    bool m_enqueing;
    bool m_backgroundProcess;
    bool m_waitForIpc;

    QMailAccountIdList m_enabledAccounts;

    QScopedPointer<QMailRetrievalAction> const m_retrievalAction;
    QScopedPointer<QMailStorageAction> const m_storageAction;
    QScopedPointer<QMailTransmitAction> const m_transmitAction;
    QMailRetrievalAction *m_attachmentRetrievalAction;
    QMailMessageId m_messageId;

    QProcess* m_messageServerProcess;
    QNetworkConfigurationManager *m_nmanager;
    QNetworkSession *m_networkSession;

    QList<QSharedPointer<EmailAction> > m_actionQueue;
    QSharedPointer<EmailAction> m_currentAction;
    struct AttachmentInfo { AttachmentStatus status; int progress;};
    // Holds a list of the attachments currently dowloading or queued for download
    QHash<QString, AttachmentInfo> m_attachmentDownloadQueue;

    bool actionInQueue(QSharedPointer<EmailAction> action) const;
    void dequeue();
    void enqueue(EmailAction *action);
    void executeCurrent();
    QSharedPointer<EmailAction> getNext();
    bool isOnline();
    void reportError(const QMailAccountId &accountId, const QMailServiceAction::Status::ErrorCode &errorCode);
    void saveAttachmentToDownloads(const QMailMessageId messageId, const QString &attachmentLocation);
    void updateAttachmentDowloadStatus(const QString &attachmentLocation, AttachmentStatus status);
    void updateAttachmentDowloadProgress(const QString &attachmentLocation, int progress);
};

#endif
