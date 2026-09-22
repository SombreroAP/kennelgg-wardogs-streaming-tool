#pragma once
#include <QObject>
#include <QString>
#include <QByteArray>
#include <functional>

/// One HTTPS GET. On Windows it goes through WinHTTP, the system's own stack, because the Qt that
/// OBS ships has no TLS backend on some PCs and every QNetworkAccessManager request then fails with
/// "TLS initialization failed". Elsewhere it is a plain Qt request.
namespace Http {
struct Result {
	bool ok = false;
	int status = 0; // HTTP status, 0 when the request never got an answer
	QByteArray body;
	QString error; // "" when ok
};
/// Blocking: call it off the UI thread.
Result get(const QString &url, int timeoutMs, const QString &userAgent);
/// Any method, with a body and extra headers ("Name: value\r\n" lines). Blocking.
Result request(const QString &method, const QString &url, const QByteArray &body, const QString &headers, int timeoutMs,
	       const QString &userAgent);
/// request() on its own thread; `done` runs on ctx's thread afterwards (not at all if ctx is gone).
void requestAsync(QObject *ctx, const QString &method, const QString &url, const QByteArray &body,
		  const QString &headers, int timeoutMs, const QString &userAgent, std::function<void(Result)> done);
/// get() on its own thread; `done` runs on ctx's thread afterwards (not at all if ctx is gone).
void getAsync(QObject *ctx, const QString &url, int timeoutMs, const QString &userAgent,
	      std::function<void(Result)> done);
/// At OBS exit: refuses new requests, cancels the ones in flight and waits briefly for their threads,
/// so no thread of ours is still inside the network stack when the process goes down.
void shutdown();
} // namespace Http
