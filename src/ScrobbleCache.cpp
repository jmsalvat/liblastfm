/*
   Copyright 2009 Last.fm Ltd. 
      - Primarily authored by Max Howell, Jono Cole and Doug Mansell

   This file is part of liblastfm.

   liblastfm is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   liblastfm is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with liblastfm.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "ScrobbleCache.h"
#include "ScrobblePoint.h"
#include "misc.h"
#include <QCoreApplication>
#include <QFile>
#include <QDomElement>
#include <QDomDocument>

using lastfm::ScrobbleCache;


class lastfm::ScrobbleCachePrivate
{
    public:
    enum Invalidity
    {
        TooShort,
        ArtistNameMissing,
        TrackNameMissing,
        ArtistInvalid,
        NoTimestamp,
        FromTheFuture,
        FromTheDistantPast
    };

    QString m_username;
    QString m_path;
    QList<Track> m_tracks;

    bool isValid( const Track& track, Invalidity* = 0 );
    void write(); /// writes m_tracks to m_path
    void read( QDomDocument& xml );  /// reads from m_path into m_tracks   
    
};


bool
lastfm::ScrobbleCachePrivate::isValid( const lastfm::Track& track, Invalidity* v )
{
    #define TEST( test, x ) \
        if (test) { \
            if (v) *v = x; \
            return false; \
        }

    TEST( track.duration() < ScrobblePoint::kScrobbleMinLength, TooShort );

    TEST( !track.timestamp().isValid(), NoTimestamp );

    // actual spam prevention is something like 12 hours, but we are only
    // trying to weed out obviously bad data, server side criteria for
    // "the future" may change, so we should let the server decide, not us
    TEST( track.timestamp() > QDateTime::currentDateTime().addMonths( 1 ), FromTheFuture );

    TEST( track.timestamp() < QDateTime::fromString( "2003-01-01", Qt::ISODate ), FromTheDistantPast );

    // Check if any required fields are empty
    TEST( track.artist().isNull(), ArtistNameMissing );
    TEST( track.title().isEmpty(), TrackNameMissing );

    TEST( (QStringList() << "unknown artist"
                         << "unknown"
                         << "[unknown]"
                         << "[unknown artist]").contains( track.artist().name().toLower() ),
           ArtistInvalid );

    return true;
}


ScrobbleCache::ScrobbleCache( const QString& username )
    : d( new ScrobbleCachePrivate )
{
    Q_ASSERT( username.length() );

    d->m_path = lastfm::dir::runtimeData().filePath( username + "_subs_cache.xml" );
    d->m_username = username;

    QDomDocument xml;
    d->read( xml );
}


ScrobbleCache::ScrobbleCache( const ScrobbleCache& that )
    : d ( new ScrobbleCachePrivate( *that.d ) )
{
}


ScrobbleCache&
ScrobbleCache::operator=( const ScrobbleCache& that )
{
    d->m_username = that.d->m_username;
    d->m_path = that.d->m_path;
    d->m_tracks = that.d->m_tracks;
    return *this;
}


ScrobbleCache::~ScrobbleCache()
{
    delete d;
}


void
lastfm::ScrobbleCachePrivate::read( QDomDocument& xml )
{
    m_tracks.clear();

    QFile file( m_path );
    file.open( QFile::Text | QFile::ReadOnly );
    QTextStream stream( &file );
    stream.setCodec( "UTF-8" );

    xml.setContent( stream.readAll() );

    for (QDomNode n = xml.documentElement().firstChild(); !n.isNull(); n = n.nextSibling())
        if (n.nodeName() == "track")
            m_tracks += Track( n.toElement() );
}


void
lastfm::ScrobbleCachePrivate::write()
{
    if (m_tracks.isEmpty())
    {
        QFile::remove( m_path );
    }
    else {
        QDomDocument xml;
        QDomElement e = xml.createElement( "submissions" );
        e.setAttribute( "product", QCoreApplication::applicationName() );
        e.setAttribute( "version", "2" );

        foreach (Track i, m_tracks)
            e.appendChild( i.toDomElement( xml ) );

        xml.appendChild( e );

        QFile file( m_path );
        file.open( QIODevice::WriteOnly | QIODevice::Text );

        QTextStream stream( &file );
        stream.setCodec( "UTF-8" );
        stream << "<?xml version='1.0' encoding='utf-8'?>\n";
        stream << xml.toString( 2 );
        file.close();
    }
}


void
ScrobbleCache::add( const QList<lastfm::Track>& tracks )
{
    foreach (const Track& track, tracks)
    {
        ScrobbleCachePrivate::Invalidity invalidity;
        
        if ( !d->isValid( track, &invalidity ) )
        {
            qWarning() << invalidity;
        }
        else if (track.isNull()) 
            qDebug() << "Will not cache an empty track";
        else 
            d->m_tracks += track;
    }

    d->write();
}


int
ScrobbleCache::remove( const QList<lastfm::Track>& toremove )
{
    QMutableListIterator<Track> i( d->m_tracks );
    while (i.hasNext()) {
        Track t = i.next();
        for (int x = 0; x < toremove.count(); ++x)
            if (toremove[x] == t)
                i.remove();
    }

    d->write();

    // yes we return # remaining, rather # removed, but this is an internal 
    // function and the behaviour is documented so it's alright imo --mxcl
    return d->m_tracks.count();
}


QList<lastfm::Track>
ScrobbleCache::tracks() const
{
    return d->m_tracks;
}


QString
ScrobbleCache::path() const
{
    return d->m_path;
}


QString
ScrobbleCache::username() const
{
    return d->m_username;
}
