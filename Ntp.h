#ifndef NTP_H__
#define NTP_H__

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHostInfo>
#include <QHostAddress>
#include <QList>

//
// Minimal SNTP (RFC 4330) client.
//
// Resolves the given server name (which may yield both IPv4 and IPv6
// addresses) and queries it for the current time, emitting the clock
// offset between the server and this computer on success.
//
class Ntp final
  : public QObject
{
  Q_OBJECT

public:
  explicit Ntp (QObject * parent = nullptr);

  // Resolve host and send an SNTP request. offsetComputed() or
  // error() is emitted asynchronously with the result. Ignored while
  // a query is already in progress.
  void query (QString const& host, quint16 port = 123);

  bool isBusy () const { return busy_; }

Q_SIGNALS:
  // offsetSeconds: server time minus local time, in seconds
  // (positive means the local clock is behind the server).
  void offsetComputed (double offsetSeconds, double roundTripSeconds);
  void error (QString const& message);

private:
  void hostResolved (QHostInfo const&);
  void tryNextAddress ();
  void finishWithError (QString const& message);

  Q_SLOT void readyRead ();
  Q_SLOT void timeout ();

  QUdpSocket socket_;
  QTimer timeout_timer_;
  quint16 port_;
  QList<QHostAddress> pending_addresses_;
  qint64 t1_ms_;                // client send time, ms since Unix epoch
  bool busy_;
};

#endif
