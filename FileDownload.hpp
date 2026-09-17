#ifndef JTDX_FILEDOWNLOAD_HPP__
#define JTDX_FILEDOWNLOAD_HPP__

#include <QObject>
#include <QString>
#include <QPointer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

//
// Downloads a single file to a given path, following redirects.
// Ported from WSJT-X (Network/FileDownload) with the upstream Logger
// dependency removed; failures are reported through the error() signal.
//
class FileDownload : public QObject
{
  Q_OBJECT

public:
  explicit FileDownload ();
  ~FileDownload ();

  void configure (QNetworkAccessManager * network_manager, QString const& source_url
                  , QString const& destination_filename, QString const& user_agent);

private:
  QNetworkAccessManager * manager_;
  QString source_url_;
  QString destination_filename_;
  QString user_agent_;
  QPointer<QNetworkReply> reply_;
  QNetworkRequest request_;
  QSaveFile destfile_;
  bool url_valid_;
  int redirect_count_;

signals:
  void complete (QString filename);
  void progress (QString message);
  void load_finished () const;
  void download_error (QString const& reason) const;
  void error (QString const& reason) const;

public slots:
  void start_download ();
  void download (QUrl url);
  void store ();
  void abort ();
  void downloadComplete (QNetworkReply * data);
  void downloadProgress (qint64 received, qint64 total);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  void errorOccurred (QNetworkReply::NetworkError code);
#else
  void obsoleteError ();
#endif
  void replyComplete ();
};

#endif
