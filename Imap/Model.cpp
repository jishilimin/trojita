/* Copyright (C) 2007 - 2008 Jan Kundrát <jkt@gentoo.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "Imap/Model.h"
#include "Imap/MailboxTree.h"
#include <QDebug>

namespace Imap {
namespace Mailbox {

Model::Model( QObject* parent, CachePtr cache, AuthenticatorPtr authenticator,
        ParserPtr parser ):
    // parent
    QAbstractItemModel( parent ),
    // our tools
    _cache(cache), _authenticator(authenticator), _parser(parser),
    _state( CONN_STATE_ESTABLISHED ), _capabilitiesFresh(false), _mailboxes(0)
{
    connect( _parser.get(), SIGNAL( responseReceived() ), this, SLOT( responseReceived() ) );
    _mailboxes = new TreeItemMailbox( 0 );
}

void Model::responseReceived()
{
    while ( _parser->hasResponse() ) {
        std::tr1::shared_ptr<Imap::Responses::AbstractResponse> resp = _parser->getResponse();
        Q_ASSERT( resp );

        QTextStream s(stderr);
        s << "<<< " << *resp << "\r\n";
        s.flush();
        resp->plug( _parser, this );
    }
}

void Model::handleState( Imap::ParserPtr ptr, const Imap::Responses::State* const resp )
{
    // OK/NO/BAD/PREAUTH/BYE
    using namespace Imap::Responses;

    const QString& tag = resp->tag;

    // Check for common stuff like ALERT and CAPABILITIES update
    switch ( resp->respCode ) {
        case ALERT:
            {
                const RespData<QString>* const msg = dynamic_cast<const RespData<QString>* const>(
                        resp->respCodeData.get() );
                //alert( resp, msg ? msg->data : QString() );
                throw 42; // FIXME
            }
            break;
        case CAPABILITIES:
            {
                const RespData<QStringList>* const caps = dynamic_cast<const RespData<QStringList>* const>(
                        resp->respCodeData.get() );
                if ( caps ) {
                    _capabilities = caps->data;
                    _capabilitiesFresh = true;
                }
            }
            break;
        default:
            // do nothing here, it must be handled later
            break;
    }

    // FIXME: we shouldn't mix tag-based and state-based stuff here

    if ( ! tag.isEmpty() ) {
        QMap<CommandHandle, Task>::const_iterator command = _commandMap.find( tag );
        if ( command == _commandMap.end() )
            throw UnexpectedResponseReceived( "Unknown tag in tagged response", *resp );

        switch ( command->kind ) {
            case Task::NONE:
                throw 42; // FIXME internal error
                break;
            case Task::LIST:
                _finalizeList( command );
                return;
                break;
        }
    }

    switch ( _state ) {
        case CONN_STATE_ESTABLISHED:
            if ( ! tag.isEmpty() )
                throw UnexpectedResponseReceived( "Received a tagged response when expecting server greeting", *resp );
            else
                handleStateInitial( resp );
            break;
        case CONN_STATE_NOT_AUTH:
            throw UnexpectedResponseReceived(
                    "Somehow we managed to get back to the "
                    "IMAP_STATE_NOT_AUTH, which is rather confusing",
                    *resp );
            break;
        case CONN_STATE_AUTH:
            handleStateAuthenticated( resp );
            break;
        case CONN_STATE_SELECTING:
            handleStateSelecting( resp );
            break;
        case CONN_STATE_SELECTED:
            handleStateSelected( resp );
            break;
        case CONN_STATE_LOGOUT:
            // hey, we're supposed to be logged out, how come that
            // *anything* made it here?
            throw UnexpectedResponseReceived(
                    "WTF, we're logged out, yet I just got this message", 
                    *resp );
            break;
    }
}

void Model::_finalizeList( const QMap<CommandHandle, Task>::const_iterator command )
{
    emit layoutAboutToBeChanged();
    QList<TreeItem*> mailboxes;
    TreeItemMailbox* mailboxPtr = static_cast<TreeItemMailbox*>( command->what );
    for ( QList<Responses::List>::const_iterator it = _listResponses.begin();
            it != _listResponses.end(); ++it ) {
        if ( it->mailbox != mailboxPtr->mailbox() + mailboxPtr->separator() )
            mailboxes << new TreeItemMailbox( command->what, *it );
    }
    _listResponses.clear();
    qSort( mailboxes.begin(), mailboxes.end(), SortMailboxes );
    command->what->setChildren( mailboxes );
    emit layoutChanged();

    qDebug() << "_finalizeList" << mailboxPtr->mailbox();
}

bool SortMailboxes( const TreeItem* const a, const TreeItem* const b )
{
    return dynamic_cast<const TreeItemMailbox* const>(a)->mailbox().compare( 
            dynamic_cast<const TreeItemMailbox* const>(b)->mailbox(), Qt::CaseInsensitive 
            ) < 1;
}

void Model::_updateState( const ConnectionState state )
{
    _state = state;
}

void Model::handleStateInitial( const Imap::Responses::State* const state )
{
    using namespace Imap::Responses;

    switch ( state->kind ) {
        case PREAUTH:
            _updateState( CONN_STATE_AUTH );
            break;
        case OK:
            _updateState( CONN_STATE_NOT_AUTH );
        case BYE:
            _updateState( CONN_STATE_LOGOUT );
        default:
            throw Imap::UnexpectedResponseReceived(
                    "Waiting for initial OK/BYE/PREAUTH, but got this instead",
                    *state );
    }

    /*switch ( state->respCode() ) {
        case ALERT:
        case CAPABILITIES:
            // already handled in handleState()
            break;
        default:
            _unknownResponseCode( state );
    }*/
    
    // FIXME
}

void Model::handleStateAuthenticated( const Imap::Responses::State* const state )
{
    const QString& tag = state->tag;
}

void Model::handleStateSelecting( const Imap::Responses::State* const state )
{
}

void Model::handleStateSelected( const Imap::Responses::State* const state )
{
}


void Model::handleCapability( Imap::ParserPtr ptr, const Imap::Responses::Capability* const resp )
{
}

void Model::handleNumberResponse( Imap::ParserPtr ptr, const Imap::Responses::NumberResponse* const resp )
{
}

void Model::handleList( Imap::ParserPtr ptr, const Imap::Responses::List* const resp )
{
    _listResponses << *resp;
}

void Model::handleFlags( Imap::ParserPtr ptr, const Imap::Responses::Flags* const resp )
{
}

void Model::handleSearch( Imap::ParserPtr ptr, const Imap::Responses::Search* const resp )
{
    throw UnexpectedResponseReceived( "SEARCH reply, wtf?", *resp );
}

void Model::handleStatus( Imap::ParserPtr ptr, const Imap::Responses::Status* const resp )
{
    throw UnexpectedResponseReceived( "STATUS reply, wtf?", *resp );
}

void Model::handleFetch( Imap::ParserPtr ptr, const Imap::Responses::Fetch* const resp )
{
    throw UnexpectedResponseReceived( "FETCH reply, wtf?", *resp );
}

void Model::handleNamespace( Imap::ParserPtr ptr, const Imap::Responses::Namespace* const resp )
{
    throw UnexpectedResponseReceived( "NAMESPACE reply, wtf?", *resp );
}





TreeItem* Model::translatePtr( const QModelIndex& index ) const
{
    return index.internalPointer() ? static_cast<TreeItem*>( index.internalPointer() ) : _mailboxes;
}

QVariant Model::data(const QModelIndex& index, int role ) const
{
    return translatePtr( index )->data( this, role );
}

QModelIndex Model::index(int row, int column, const QModelIndex& parent ) const
{
    TreeItem* parentItem = parent.internalPointer() ? 
        static_cast<TreeItem*>( parent.internalPointer() ) : _mailboxes;

    TreeItem* child = parentItem->child( row, this );

    return child ? QAbstractItemModel::createIndex( row, column, child ) : QModelIndex();
}

QModelIndex Model::parent(const QModelIndex& index ) const
{
    if ( !index.isValid() )
        return QModelIndex();

    TreeItem *childItem = static_cast<TreeItem*>(index.internalPointer());
    TreeItem *parentItem = childItem->parent();

    if ( parentItem == _mailboxes )
        return QModelIndex();

    return QAbstractItemModel::createIndex( parentItem->row(), 0, parentItem );
}

int Model::rowCount(const QModelIndex& index ) const
{
    TreeItem* node = static_cast<TreeItem*>( index.internalPointer() );
    if ( !node ) {
        node = _mailboxes;
    }
    Q_ASSERT(node);
    return node->rowCount( this );
}

int Model::columnCount(const QModelIndex& index ) const
{
    TreeItem* node = static_cast<TreeItem*>( index.internalPointer() );
    if ( !node ) {
        node = _mailboxes;
    }
    Q_ASSERT(node);
    return node->columnCount( this );
}


void Model::_askForChildrenOfMailbox( TreeItem* item ) const
{
    QString mailbox = dynamic_cast<TreeItemMailbox*>( item )->mailbox();

    if ( mailbox.isNull() )
        mailbox = "%";
    else
        mailbox = QString::fromLatin1("%1.%").arg( mailbox ); // FIXME: separator

    qDebug() << "_askForChildrenOfMailbox()" << mailbox;
    CommandHandle cmd = _parser->list( "", mailbox );
    _commandMap[ cmd ] = Task( Task::LIST, item );
}

}
}

#include "Model.moc"
