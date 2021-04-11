// SPDX-License-Identifier: GPL-2.0
#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>

#include "pref.h"
#include "qthelper.h"
#include "git-access.h"
#include "errorhelper.h"

#include "checkcloudconnection.h"

CheckCloudConnection::CheckCloudConnection(QObject *parent) :
	QObject(parent),
	reply(0)
{

}

// our own madeup API to make sure we are talking to a Subsurface cloud server
#define TEAPOT "/make-latte?number-of-shots=3"
#define HTTP_I_AM_A_TEAPOT 418
#define MILK "Linus does not like non-fat milk"
bool CheckCloudConnection::checkServer()
{
	if (verbose)
		fprintf(stderr, "Checking cloud connection...\n");

	QTimer timer;
	timer.setSingleShot(true);
	QEventLoop loop;
	QNetworkRequest request;
	request.setRawHeader("Accept", "text/plain");
	request.setRawHeader("User-Agent", getUserAgent().toUtf8());
	request.setRawHeader("Client-Id", getUUID().toUtf8());
	request.setUrl(QString(prefs.cloud_base_url) + TEAPOT);
	QNetworkAccessManager *mgr = new QNetworkAccessManager();
	reply = mgr->get(request);
	connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	connect(reply, &QNetworkReply::sslErrors, this, &CheckCloudConnection::sslErrors);
	for (int seconds = 1; seconds <= prefs.cloud_timeout; seconds++) {
		timer.start(1000); // wait the given number of seconds (default 5)
		loop.exec();
		if (timer.isActive()) {
			// didn't time out, did we get the right response?
			timer.stop();
			if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == HTTP_I_AM_A_TEAPOT &&
			    reply->readAll() == QByteArray(MILK)) {
				reply->deleteLater();
				mgr->deleteLater();
				if (verbose > 1)
					qWarning() << "Cloud storage: successfully checked connection to cloud server";
				return true;
			}
		} else if (seconds < prefs.cloud_timeout) {
			QString text = tr("Waiting for cloud connection (%n second(s) passed)", "", seconds);
			git_storage_update_progress(qPrintable(text));
		} else {
			disconnect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
			reply->abort();
		}
	}
	git_storage_update_progress(qPrintable(tr("Cloud connection failed")));
	git_local_only = true;
	if (verbose)
		qDebug() << "connection test to cloud server failed" <<
			    reply->error() << reply->errorString() <<
			    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() <<
			    reply->readAll();
	reply->deleteLater();
	mgr->deleteLater();
	if (verbose)
		qWarning() << "Cloud storage: unable to connect to cloud server";
	return false;
}

void CheckCloudConnection::sslErrors(const QList<QSslError> &errorList)
{
	qDebug() << "Received error response trying to set up https connection with cloud storage backend:";
	for (QSslError err: errorList)
		qDebug() << err.errorString();
}

// helper to be used from C code
extern "C" bool canReachCloudServer()
{
	if (verbose)
		qWarning() << "Cloud storage: checking connection to cloud server";
	return CheckCloudConnection().checkServer();
}
