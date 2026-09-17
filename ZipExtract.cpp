#include "ZipExtract.hpp"

#include <QFile>
#include <QSaveFile>
#include <QByteArray>
#include <QtEndian>
#include <QObject>

#include <zlib.h>

namespace
{
  quint16 u16 (QByteArray const& b, int off)
  {
    return qFromLittleEndian<quint16> (reinterpret_cast<uchar const *> (b.constData () + off));
  }

  quint32 u32 (QByteArray const& b, int off)
  {
    return qFromLittleEndian<quint32> (reinterpret_cast<uchar const *> (b.constData () + off));
  }

  // Raw deflate stream (no zlib header), as stored inside a ZIP member.
  bool inflate_raw (QByteArray const& in, QByteArray * out, quint32 expected_size)
  {
    out->resize (expected_size);
    if (!expected_size) return true;

    z_stream zs {};
    if (inflateInit2 (&zs, -MAX_WBITS) != Z_OK) return false;

    zs.next_in = reinterpret_cast<Bytef *> (const_cast<char *> (in.constData ()));
    zs.avail_in = static_cast<uInt> (in.size ());
    zs.next_out = reinterpret_cast<Bytef *> (out->data ());
    zs.avail_out = static_cast<uInt> (expected_size);

    auto const rc = inflate (&zs, Z_FINISH);
    auto const produced = zs.total_out;
    inflateEnd (&zs);

    return (rc == Z_STREAM_END || rc == Z_OK) && produced == expected_size;
  }
}

bool extract_from_zip (QString const& zip_path, QString const& member_suffix
                       , QString const& destination_path, QString * error)
{
  auto fail = [error] (QString const& why) { if (error) *error = why; return false; };

  QFile zip {zip_path};
  if (!zip.open (QIODevice::ReadOnly)) return fail (QObject::tr ("cannot open %1").arg (zip_path));
  auto const data = zip.readAll ();
  zip.close ();

  // End of central directory: signature PK\5\6, within the last 64 KiB
  int eocd = -1;
  for (int i = data.size () - 22; i >= 0 && i >= data.size () - 65557; --i)
    {
      if (u32 (data, i) == 0x06054b50) { eocd = i; break; }
    }
  if (eocd < 0) return fail (QObject::tr ("not a ZIP archive"));

  auto const entries = u16 (data, eocd + 10);
  auto cd = static_cast<int> (u32 (data, eocd + 16));

  for (int n = 0; n < entries; ++n)
    {
      if (cd + 46 > data.size () || u32 (data, cd) != 0x02014b50)
        return fail (QObject::tr ("damaged central directory"));

      auto const method = u16 (data, cd + 10);
      auto const compressed = u32 (data, cd + 20);
      auto const uncompressed = u32 (data, cd + 24);
      auto const name_len = u16 (data, cd + 28);
      auto const extra_len = u16 (data, cd + 30);
      auto const comment_len = u16 (data, cd + 32);
      auto const local_off = static_cast<int> (u32 (data, cd + 42));
      auto const name = QString::fromUtf8 (data.mid (cd + 46, name_len));

      if (name.endsWith (member_suffix, Qt::CaseInsensitive))
        {
          // the local header repeats the name and extra fields, with its own lengths
          if (local_off + 30 > data.size () || u32 (data, local_off) != 0x04034b50)
            return fail (QObject::tr ("damaged local header for %1").arg (name));
          auto const body = local_off + 30 + u16 (data, local_off + 26) + u16 (data, local_off + 28);
          if (body + static_cast<int> (compressed) > data.size ())
            return fail (QObject::tr ("truncated archive"));

          QByteArray payload;
          if (method == 0)                     // stored
            {
              payload = data.mid (body, compressed);
            }
          else if (method == 8)                // deflated
            {
              if (!inflate_raw (data.mid (body, compressed), &payload, uncompressed))
                return fail (QObject::tr ("cannot inflate %1").arg (name));
            }
          else
            {
              return fail (QObject::tr ("unsupported compression method %1").arg (method));
            }

          QSaveFile out {destination_path};
          if (!out.open (QIODevice::WriteOnly))
            return fail (QObject::tr ("cannot write %1").arg (destination_path));
          out.write (payload);
          if (!out.commit ()) return fail (QObject::tr ("cannot commit %1").arg (destination_path));
          return true;
        }

      cd += 46 + name_len + extra_len + comment_len;
    }

  return fail (QObject::tr ("no member ending in %1").arg (member_suffix));
}
