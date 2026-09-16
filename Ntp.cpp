#include "Ntp.h"

#include <QDateTime>
#include <QtEndian>

namespace
{
  quint32 constexpr NTP_UNIX_EPOCH_DELTA = 2208988800u; // seconds between 1900-01-01 and 1970-01-01
  int constexpr NTP_PACKET_SIZE = 48;
  int constexpr NTP_TIMEOUT_MS = 4000;
}

Ntp::Ntp (QObject * parent)
  : QObject {parent}
  , port_ {123}
  , t1_ms_ {0}
  , busy_ {false}
{
  connect (&socket_, &QUdpSocket::readyRead, this, &Ntp::readyRead);
  timeout_timer_.setSingleShot (true);
  connect (&timeout_timer_, &QTimer::timeout, this, &Ntp::timeout);
}

void Ntp::query (QString const& host, quint16 port)
{
  if (busy_)
    {
      return;
    }
  busy_ = true;
  port_ = port;
  pending_addresses_.clear ();

  QHostAddress direct;
  if (direct.setAddress (host))
    {
      pending_addresses_ << direct;
      tryNextAddress ();
    }
  else
    {
      QHostInfo::lookupHost (host, this, &Ntp::hostResolved);
    }
}

void Ntp::hostResolved (QHostInfo const& info)
{
  if (info.error () != QHostInfo::NoError || info.addresses ().isEmpty ())
    {
      finishWithError (tr ("NTP: could not resolve server address"));
      return;
    }
  pending_addresses_ = info.addresses (); // may contain both IPv4 and IPv6 entries
  tryNextAddress ();
}

void Ntp::tryNextAddress ()
{
  timeout_timer_.stop ();
  socket_.close ();

  if (pending_addresses_.isEmpty ())
    {
      finishWithError (tr ("NTP: no response from server"));
      return;
    }
  auto const address = pending_addresses_.takeFirst ();

  // Bind with a family matching the target address so both IPv4 and
  // IPv6 servers can be reached.
  auto const any = address.protocol () == QAbstractSocket::IPv6Protocol
    ? QHostAddress {QHostAddress::AnyIPv6} : QHostAddress {QHostAddress::AnyIPv4};
  if (!socket_.bind (any, 0))
    {
      tryNextAddress ();
      return;
    }

  quint8 packet[NTP_PACKET_SIZE] = {0};
  packet[0] = 0x23;             // LI = 0, VN = 4, Mode = 3 (client)

  t1_ms_ = QDateTime::currentMSecsSinceEpoch ();
  auto const written = socket_.writeDatagram (reinterpret_cast<char const *> (packet), NTP_PACKET_SIZE, address, port_);
  if (written != NTP_PACKET_SIZE)
    {
      tryNextAddress ();
      return;
    }
  t1_ms_ = QDateTime::currentMSecsSinceEpoch (); // refine T1 to just after the write
  timeout_timer_.start (NTP_TIMEOUT_MS);
}

void Ntp::readyRead ()
{
  while (socket_.hasPendingDatagrams ())
    {
      QByteArray datagram;
      datagram.resize (int (socket_.pendingDatagramSize ()));
      socket_.readDatagram (datagram.data (), datagram.size ());

      qint64 const t4_ms = QDateTime::currentMSecsSinceEpoch ();

      if (datagram.size () < NTP_PACKET_SIZE)
        {
          continue;             // malformed, ignore and keep waiting
        }

      auto const * p = reinterpret_cast<quint8 const *> (datagram.constData ());
      int const mode = p[0] & 0x07;
      quint8 const stratum = p[1];
      if (mode != 4 /* server */ || stratum == 0 /* kiss-o'-death */)
        {
          continue;
        }

      quint32 const rx_sec = qFromBigEndian<quint32> (p + 32);
      quint32 const rx_frac = qFromBigEndian<quint32> (p + 36);
      quint32 const tx_sec = qFromBigEndian<quint32> (p + 40);
      quint32 const tx_frac = qFromBigEndian<quint32> (p + 44);

      double const t1 = double (t1_ms_) / 1000.0;
      double const t2 = double (rx_sec) - NTP_UNIX_EPOCH_DELTA + double (rx_frac) / 4294967296.0;
      double const t3 = double (tx_sec) - NTP_UNIX_EPOCH_DELTA + double (tx_frac) / 4294967296.0;
      double const t4 = double (t4_ms) / 1000.0;

      double const offset = ((t2 - t1) + (t3 - t4)) / 2.0;
      double const roundtrip = (t4 - t1) - (t3 - t2);

      timeout_timer_.stop ();
      socket_.close ();
      busy_ = false;
      Q_EMIT offsetComputed (offset, roundtrip);
      return;
    }
}

void Ntp::timeout ()
{
  tryNextAddress ();            // fall back to any other resolved address
}

void Ntp::finishWithError (QString const& message)
{
  socket_.close ();
  busy_ = false;
  Q_EMIT error (message);
}
