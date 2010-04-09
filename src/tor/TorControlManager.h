#ifndef TORCONTROLMANAGER_H
#define TORCONTROLMANAGER_H

#include <QObject>
#include <QHostAddress>

class QNetworkProxy;

namespace Tor
{

class HiddenService;

class TorControlManager : public QObject
{
	Q_OBJECT
	friend class ProtocolInfoCommand;

public:
	enum AuthMethod
	{
		AuthUnknown = 0,
		AuthNull = 0x1,
		AuthHashedPassword = 0x2,
		AuthCookie = 0x4
	};

	enum Status
	{
		Error = -1,
		NotConnected,
		Connecting,
		Authenticating,
		Connected
	};

    explicit TorControlManager(QObject *parent = 0);

	/* Information */
	Status status() const { return pStatus; }
	QString torVersion() const { return pTorVersion; }
	QString statusText() const;

	bool isSocksReady() const { return !pSocksAddress.isNull(); }
	QHostAddress socksAddress() const { return pSocksAddress; }
	quint16 socksPort() const { return pSocksPort; }
	QNetworkProxy connectionProxy();

	/* Authentication */
	QFlags<AuthMethod> authMethods() const { return pAuthMethods; }
	void setAuthPassword(const QByteArray &password);

	/* Connection */
	bool isConnected() const { return status() == Connected; }
	void connect(const QHostAddress &address, quint16 port);

	/* Hidden Services */
	const QList<HiddenService*> &hiddenServices() const { return pServices; }
	void addHiddenService(HiddenService *service);

signals:
	void statusChanged(Status newStatus, Status oldStatus);
	void connected();
	void disconnected();
	void socksReady();

private slots:
	void socketConnected();
	void socketDisconnected();

	void commandFinished(class TorControlCommand *command);

	void getSocksInfoReply();

private:
	class TorControlSocket *socket;
	QString pTorVersion;
	QByteArray pAuthPassword;
	QHostAddress pSocksAddress;
	QList<HiddenService*> pServices;
	quint16 pSocksPort;
	QFlags<AuthMethod> pAuthMethods;
	Status pStatus;

	void setStatus(Status status);

	void authenticate();
	void getSocksInfo();
	void publishServices();
};

}

extern Tor::TorControlManager *torManager;

#endif // TORCONTROLMANAGER_H
