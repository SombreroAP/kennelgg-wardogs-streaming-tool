#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QByteArray>
#include <QRectF>
#include <vector>

/// The local control channel: a tiny WebSocket server on 127.0.0.1 that a companion app (ClipHound) talks to.
/// Text frames carry JSON {"type": ...}; binary frames carry JPEG crops of the game source with a 16-byte header.
/// Deliberately dependency-free: the RFC 6455 handshake and framing are a few dozen lines with Qt's SHA-1.
class Bridge : public QObject {
	Q_OBJECT
public:
	struct Client {
		QTcpSocket *sock = nullptr;
		bool upgraded = false;
		bool wantsFrames = false; // subscribed to frames (ClipHound); a controller does not
		bool controller = false;  // said so with a frames:false subscribe (the Stream Deck plugin)
		bool announced = false;   // clientConnected has been emitted (or withheld, for a controller)
		QByteArray buf;
	};
	explicit Bridge(QObject *parent = nullptr);
	bool listen(quint16 port);
	void close();
	/// Pushes what is queued on every socket out, waiting up to ms per client (no event loop needed).
	void flush(int ms);
	bool listening() const { return server_.isListening(); }
	/// Companion-app clients (ClipHound): a controller such as the Stream Deck plugin is not counted,
	/// so "is ClipHound here" keeps meaning that. Everyone still gets sendJson.
	int clients() const
	{
		int n = 0;
		for (auto *c : clients_)
			if (c->upgraded && !c->controller)
				n++;
		return n;
	}
	int allClients() const { return (int)clients_.size(); }
	quint16 port() const { return server_.serverPort(); }

	/// A client asked for frames of this region (fractions of the game source) at this rate. 0 = nobody.
	double wantedFps() const { return frameFps_; }
	QRectF wantedRoi() const { return roi_; }
	int wantedWidth() const { return roiWidth_; }
	/// Several regions at their own rates: the kill feed ten times a second, the whole frame once
	/// a second. Each has an id the frames carry back. Empty = the single legacy stream above.
	struct Stream {
		int id = 0;
		double fps = 4;
		QRectF roi{0, 0, 1, 1};
		int width = 0;
		qint64 nextMs = 0; // when it is next due
	};
	std::vector<Stream> &streams() { return streams_; }

	void sendJson(const QJsonObject &o);
	void sendFrame(const QByteArray &jpeg, int w, int h, qint64 tsMs);
	/// "KWF2": uint8 stream id, uint16 w, uint16 h, uint64 ts ms, then JPEG.
	void sendFrame2(int id, const QByteArray &jpeg, int w, int h, qint64 tsMs);
	/// Microphone audio for ClipHound: "KWA1" then 16 kHz mono int16 PCM.
	void sendAudio(const QByteArray &pcm);

signals:
	void clientConnected();
	void clientDisconnected();
	void message(const QJsonObject &o); // JSON from the app

private:
	QTcpServer server_;
	std::vector<Client *> clients_;
	double frameFps_ = 0;
	QRectF roi_{0, 0, 1, 1};
	int roiWidth_ = 0;
	std::vector<Stream> streams_;

	void onNewConnection();
	void onReadyRead(Client *c);
	void onDisconnected(Client *c);
	bool handshake(Client *c);
	void parseFrames(Client *c);
	void sendRaw(Client *c, int opcode, const QByteArray &payload);
	void handle(Client *c, const QJsonObject &o);
	void recomputeWants();
};
