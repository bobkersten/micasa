#include <memory>

#include "Database.h"
#include "Structs.h"

namespace micasa {

	extern std::shared_ptr<Logger> g_logger;

	Database::Database( std::string filename_ ) : m_filename( filename_ ) {
#ifdef _DEBUG
		assert( g_logger && "Global Logger instance should be created before global Database instances." );
#endif // _DEBUG

		int result = sqlite3_open_v2( filename_.c_str(), &this->m_connection, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_WAL, NULL );
		if ( result == SQLITE_OK ) {
			g_logger->logr( Logger::LogLevel::VERBOSE, this, "Database %s opened.", filename_.c_str() );
			this->_init();
		} else {
			g_logger->logr( Logger::LogLevel::ERROR, this, "Unable to open database %s.", filename_.c_str() );
		}
	}

	Database::~Database() {
#ifdef _DEBUG
		assert( g_logger && "Global Logger instance should be destroyed after global Database instances." );
#endif // _DEBUG

		this->putQuery( "VACUUM" );

		int result = sqlite3_close( this->m_connection );
		if ( SQLITE_OK == result ) {
			g_logger->logr( Logger::LogLevel::VERBOSE, this, "Database %s closed.", this->m_filename.c_str() );
		} else {
			g_logger->logr( Logger::LogLevel::ERROR, this, "Database %s was not closed properly.", this->m_filename.c_str() );
		}
	}

	std::vector<std::map<std::string, std::string> > Database::getQuery( const std::string& query_, ... ) const {
		std::vector<std::map<std::string, std::string> > result;

		va_list arguments;
		va_start( arguments, query_ );
		this->_wrapQuery( query_, arguments, [&result]( sqlite3_stmt *statement_ ) {
			int columns = sqlite3_column_count( statement_ );
			while ( true ) {
				if ( SQLITE_ROW == sqlite3_step( statement_ ) ) {
					std::map<std::string, std::string> row;
					for ( int column = 0; column < columns; column++ ) {
						std::string key = std::string( reinterpret_cast<const char*>( sqlite3_column_name( statement_, column ) ) );
						const unsigned char* valueC = sqlite3_column_text( statement_, column );
						if ( valueC != NULL ) {
							std::string value = std::string( reinterpret_cast<const char*>( sqlite3_column_text( statement_, column ) ) );
							row[key] = value;
						} else {
							row[key] = "";
						}
					}
					result.push_back( row );
				} else {
					break;
				}
			}
		} );
		va_end( arguments );

		return result;
	}

	std::map<std::string, std::string> Database::getQueryRow( const std::string& query_, ... ) const {
		std::map<std::string, std::string> result;

		va_list arguments;
		va_start( arguments, query_ );
		this->_wrapQuery( query_, arguments, [&result]( sqlite3_stmt *statement_ ) {
			if ( SQLITE_ROW == sqlite3_step( statement_ ) ) {
				int columns = sqlite3_column_count( statement_ );
				for ( int column = 0; column < columns; column++ ) {
					std::string key = std::string( reinterpret_cast<const char*>( sqlite3_column_name( statement_, column ) ) );
					const unsigned char* valueC = sqlite3_column_text( statement_, column );
					if ( valueC != NULL ) {
						std::string value = std::string( reinterpret_cast<const char*>( sqlite3_column_text( statement_, column ) ) );
						result[key] = value;
					} else {
						result[key] = "";
					}
				}
			}
		} );
		va_end( arguments );

		return result;
	}

	std::map<std::string, std::string> Database::getQueryMap( const std::string& query_, ... ) const {
		std::map<std::string, std::string> result;

		va_list arguments;
		va_start( arguments, query_ );
		this->_wrapQuery( query_, arguments, [this, &result]( sqlite3_stmt *statement_ ) {
#ifdef _DEBUG
			int columns = sqlite3_column_count( statement_ );
			assert( 2 == columns && "Query result should contain exactly two columns." );
#endif // _DEBUG
			while ( true ) {
				if ( SQLITE_ROW == sqlite3_step( statement_ ) ) {
					std::string key = std::string( reinterpret_cast<const char*>( sqlite3_column_text( statement_, 0 ) ) );
					const unsigned char* valueC = sqlite3_column_text( statement_, 1 );
					if ( valueC != NULL ) {
						std::string value = std::string( reinterpret_cast<const char*>( sqlite3_column_text( statement_, 1 ) ) );
						result[key] = value;
					} else {
						result[key] = "";
					}
				} else {
					break;
				}
			}
		} );
		va_end( arguments );

		return result;
	}

	template<typename T> T Database::getQueryValue( const std::string& query_, ... ) const {
		std::string result;

		va_list arguments;
		va_start( arguments, query_ );
		this->_wrapQuery( query_, arguments, [this, &result]( sqlite3_stmt *statement_ ) {
			if ( SQLITE_ROW == sqlite3_step( statement_ ) ) {
#ifdef _DEBUG
				assert( sqlite3_column_count( statement_ ) == 1 && "Query result should contain exactly 1 value." );
#endif // _DEBUG
				const unsigned char* valueC = sqlite3_column_text( statement_, 0 );
				if ( valueC != NULL ) {
					result = std::string( reinterpret_cast<const char*>( sqlite3_column_text( statement_, 0 ) ) );
				}
			}
		} );
		va_end( arguments );

		T value;
		std::istringstream( result ) >> value;
		return value;
	}
	
	// The above template is specialized for the types listed below.
	template int Database::getQueryValue( const std::string& query_, ... ) const;
	template unsigned int Database::getQueryValue( const std::string& query_, ... ) const;
	template double Database::getQueryValue( const std::string& query_, ... ) const;

	// The string variant of the above template doesn't require string streams and has it's own
	// specialized implementation.
	template<> std::string Database::getQueryValue<std::string>( const std::string& query_, ... ) const {
		std::string result;
		
		va_list arguments;
		va_start( arguments, query_ );
		this->_wrapQuery( query_, arguments, [this, &result]( sqlite3_stmt *statement_ ) {
			if ( SQLITE_ROW == sqlite3_step( statement_ ) ) {
#ifdef _DEBUG
				assert( sqlite3_column_count( statement_ ) == 1 && "Query result should contain exactly 1 value." );
#endif // _DEBUG
				const unsigned char* valueC = sqlite3_column_text( statement_, 0 );
				if ( valueC != NULL ) {
					result = std::string( reinterpret_cast<const char*>( sqlite3_column_text( statement_, 0 ) ) );
				}
			}
		} );
		va_end( arguments );
		
		return result;
	};
	
	long Database::putQuery( const std::string& query_, ... ) const {
		va_list arguments;
		va_start( arguments, query_ );
		long insertId = -1;
		this->_wrapQuery( query_, arguments, [this,&insertId]( sqlite3_stmt *statement_ ) {
			if ( SQLITE_DONE != sqlite3_step( statement_ ) ) {
				const char *error = sqlite3_errmsg( this->m_connection );
				g_logger->logr( Logger::LogLevel::ERROR, this, "Query rejected (%s).", error );
			} else {
				insertId = sqlite3_last_insert_rowid( this->m_connection );
			}
		} );
		va_end( arguments );
		return insertId;
	}

	void Database::_init() const {
		this->putQuery( "PRAGMA synchronous=NORMAL" );
		this->putQuery( "PRAGMA foreign_keys=ON" );
		
		this->putQuery( "VACUUM" );
		
		unsigned int version = this->getQueryValue<unsigned int>( "PRAGMA user_version" );
		if ( version < c_queries.size() ) {
			for ( auto queryIt = c_queries.begin() + version; queryIt != c_queries.end(); queryIt++ ) {
				this->putQuery( *queryIt );
			}
			this->putQuery( "PRAGMA user_version=%d", c_queries.size() );
		}
	}
	
	void Database::_wrapQuery( const std::string& query_, va_list arguments_, const std::function<void(sqlite3_stmt*)> process_ ) const {
		if ( ! this->m_connection ) {
			g_logger->logr( Logger::LogLevel::ERROR, this, "Database %s not open.", this->m_filename.c_str() );
			return;
		}
		
		char *query = sqlite3_vmprintf( query_.c_str(), arguments_ );
		if ( ! query ) {
			g_logger->log( Logger::LogLevel::ERROR, this, "Out of memory or invalid printf style query." );
			return;
		}
		
		std::lock_guard<std::mutex> lock( this->m_queryMutex );
		
		sqlite3_stmt *statement;
		if ( SQLITE_OK == sqlite3_prepare_v2( this->m_connection, query, -1, &statement, NULL ) ) {
			process_( statement );
			sqlite3_finalize( statement );
		} else {
			const char *error = sqlite3_errmsg( this->m_connection );
			g_logger->logr( Logger::LogLevel::ERROR, this, "Query rejected (%s).", error );
		}
		
#ifdef _DEBUG
		g_logger->log( Logger::LogLevel::DEBUG, this, std::string( query ) );
#endif // _DEBUG
		
		sqlite3_free( query );
	}
	
} // namespace micasa
