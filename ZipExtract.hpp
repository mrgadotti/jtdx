#ifndef JTDX_ZIPEXTRACT_HPP__
#define JTDX_ZIPEXTRACT_HPP__

#include <QString>

//
// Minimal read-only ZIP support: enough to pull one member out of the
// single-file archives published for the callsign database.  Only the stored
// and deflated methods are handled, which is all those archives use.
//
// Returns true on success.  On failure `error` describes what went wrong.
//
bool extract_from_zip (QString const& zip_path, QString const& member_suffix
                       , QString const& destination_path, QString * error);

#endif
