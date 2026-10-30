#include "hikari/app/local_only_network.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QUrl>

namespace hikari::app {

namespace {

bool isLocal(const QUrl &url)
{
    const QString scheme = url.scheme();
    return scheme == QLatin1String("file") || scheme == QLatin1String("qrc") || scheme == QLatin1String("data");
}

class LocalOnlyAccessManager : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override
    {
        if (isLocal(request.url()))
            return QNetworkAccessManager::createRequest(op, request, outgoingData);
        QNetworkRequest refused(request);
        refused.setUrl(QUrl());
        return QNetworkAccessManager::createRequest(op, refused, outgoingData);
    }
};

class LocalOnlyFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override { return new LocalOnlyAccessManager(parent); }
};

}

void refuseRemoteUrls(QQmlEngine &engine)
{
    static LocalOnlyFactory factory;
    engine.setNetworkAccessManagerFactory(&factory);
}

}
