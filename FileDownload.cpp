#include "FileDownload.hpp"

#include <QCoreApplication>
#include <QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QSslSocket>
#include <QFileInfo>
#include <QDir>
#include <QIODevice>

#include "moc_FileDownload.cpp"

FileDownload::FileDownload ()
  : QObject (nullptr)
  , manager_ {nullptr}
  , url_valid_ {false}
  , redirect_count_ {0}
{
}

FileDownload::~FileDownload ()
{
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
void FileDownload::errorOccurred (QNetworkReply::NetworkError)
{
  Q_EMIT error (reply_->errorString ());
  destfile_.cancelWriting ();
  destfile_.commit ();
}
#else
void FileDownload::obsoleteError ()
{
  Q_EMIT error (reply_->errorString ());
  destfile_.cancelWriting ();
  destfile_.commit ();
}
#endif

void FileDownload::configure (QNetworkAccessManager * network_manager, QString const& source_url
                              , QString const& destination_path, QString const& user_agent)
{
  manager_ = network_manager;
  source_url_ = source_url;
  destination_filename_ = destination_path;
  user_agent_ = user_agent;
}

void FileDownload::store ()
{
  if (destfile_.isOpen ())
    {
      destfile_.write (reply_->read (reply_->bytesAvailable ()));
    }
}

void FileDownload::replyComplete ()
{
  if (!reply_)
    {
      Q_EMIT load_finished ();
      return;           // we probably deleted it in an earlier call
    }

  QUrl redirect_url {reply_->attribute (QNetworkRequest::RedirectionTargetAttribute).toUrl ()};

  if (reply_->error () == QNetworkReply::NoError && !redirect_url.isEmpty ())
    {
      if ("https" == redirect_url.scheme () && !QSslSocket::supportsSsl ())
        {
          Q_EMIT download_error (tr ("Network Error - SSL/TLS support not installed, cannot fetch:\n\'%1\'")
                                 .arg (redirect_url.toDisplayString ()));
          url_valid_ = false; // reset
          Q_EMIT load_finished ();
        }
      else if (++redirect_count_ < 10) // maintain sanity
        {
          // follow redirect
          download (reply_->url ().resolved (redirect_url));
        }
      else
        {
          Q_EMIT download_error (tr ("Network Error - Too many redirects:\n\'%1\'")
                                 .arg (redirect_url.toDisplayString ()));
          url_valid_ = false; // reset
          Q_EMIT load_finished ();
        }
    }
  else if (reply_->error () != QNetworkReply::NoError)
    {
      destfile_.cancelWriting ();
      destfile_.commit ();
      url_valid_ = false;     // reset
      // report errors that are not due to abort
      if (QNetworkReply::OperationCanceledError != reply_->error ())
        {
          Q_EMIT download_error (tr ("Network Error:\n%1").arg (reply_->errorString ()));
        }
      Q_EMIT load_finished ();
    }
  else
    {
      if (!url_valid_)
        {
          // the HEAD succeeded, now get the body content
          url_valid_ = true;
          download (reply_->url ().resolved (redirect_url));
        }
      else // the body has completed, save it
        {
          url_valid_ = false; // reset
          destfile_.commit ();
          Q_EMIT complete (destination_filename_);
        }
    }

  if (reply_ && reply_->isFinished ())
    {
      reply_->deleteLater ();
    }
}

void FileDownload::downloadComplete (QNetworkReply * data)
{
  data->deleteLater ();
}

void FileDownload::start_download ()
{
  url_valid_ = false;
  redirect_count_ = 0;
  download (QUrl (source_url_));
}

void FileDownload::download (QUrl qurl)
{
  request_.setUrl (qurl);

  request_.setAttribute (QNetworkRequest::FollowRedirectsAttribute, true);
  request_.setRawHeader ("Accept", "*/*");
  // some sites, country-files among them, reject requests without a user agent
  request_.setRawHeader ("User-Agent", user_agent_.toLocal8Bit ());

  if (!url_valid_)
    {
      reply_ = manager_->head (request_);
    }
  else
    {
      reply_ = manager_->get (request_);
    }

  connect (manager_, &QNetworkAccessManager::finished, this, &FileDownload::downloadComplete, Qt::UniqueConnection);
  connect (reply_, &QNetworkReply::downloadProgress, this, &FileDownload::downloadProgress, Qt::UniqueConnection);
  connect (reply_, &QNetworkReply::finished, this, &FileDownload::replyComplete, Qt::UniqueConnection);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  connect (reply_, &QNetworkReply::errorOccurred, this, &FileDownload::errorOccurred, Qt::UniqueConnection);
#else
  connect (reply_, QOverload<QNetworkReply::NetworkError>::of (&QNetworkReply::error), this
           , &FileDownload::obsoleteError, Qt::UniqueConnection);
#endif
  connect (reply_, &QNetworkReply::readyRead, this, &FileDownload::store, Qt::UniqueConnection);

  QFileInfo destination_file {destination_filename_};
  QDir {}.mkpath (destination_file.absolutePath ());

  if (url_valid_)
    {
      destfile_.setFileName (destination_file.absoluteFilePath ());
      if (!destfile_.open (QSaveFile::WriteOnly | QIODevice::WriteOnly))
        {
          Q_EMIT error (tr ("Unable to open %1: %2").arg (destfile_.fileName ()).arg (destfile_.errorString ()));
          return;
        }
    }
}

void FileDownload::downloadProgress (qint64 received, qint64)
{
  Q_EMIT progress (tr ("%1 bytes downloaded").arg (received));
}

void FileDownload::abort ()
{
  if (reply_ && reply_->isRunning ())
    {
      reply_->abort ();
    }
}
