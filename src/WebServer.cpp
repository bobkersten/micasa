// mongoose
// http://www.vinaysahni.com/best-practices-for-a-pragmatic-restful-api
// https://docs.cesanta.com/mongoose/master/
// https://github.com/Gregwar/mongoose-cpp/blob/master/mongoose/Server.cpp
// http://stackoverflow.com/questions/7698488/turn-a-simple-socket-into-an-ssl-socket
// https://github.com/cesanta/mongoose/blob/master/examples/simplest_web_server/simplest_web_server.c

// json
// https://github.com/nlohmann/json

#include <cstdlib>

#include "WebServer.h"
#include "Utils.h"

#include "json.hpp"

namespace micasa {

	extern std::shared_ptr<Logger> g_logger;
	extern std::shared_ptr<Network> g_network;
	
	using namespace nlohmann;

	WebServer::WebServer() {
#ifdef _DEBUG
		assert( g_logger && "Global Logger instance should be created before global WebServer instance." );
#endif // _DEBUG
		srand ( time( NULL ) );
	};

	WebServer::~WebServer() {
#ifdef _DEBUG
		assert( g_logger && "Global Logger instance should be destroyed after global WebServer instance." );
		assert( this->m_resources.size() == 0 && "All resources should be removed before the global WebServer instance is destroyed." );
#endif // _DEBUG
	};

	void WebServer::start() {
#ifdef _DEBUG
		assert( g_network->isRunning() && "Network should be running before WebServer is started." );
#endif // _DEBUG
		g_logger->log( Logger::LogLevel::VERBOSE, this, "Starting..." );

		g_network->bind( "8081", Network::t_callback( [this]( mg_connection* connection_, int event_, void* data_ ) {
			if ( event_ == MG_EV_HTTP_REQUEST ) {
				this->_processHttpRequest( connection_, (http_message*)data_ );
			}
		} ) );
		
		this->_begin();
		g_logger->log( Logger::LogLevel::NORMAL, this, "Started." );
	};
	
	void WebServer::stop() {
#ifdef _DEBUG
		assert( g_network->isRunning() && "Network should be running before WebServer is stopped." );
#endif // _DEBUG
		g_logger->log( Logger::LogLevel::VERBOSE, this, "Stopping..." );

		this->_retire();
		g_logger->log( Logger::LogLevel::NORMAL, this, "Stopped." );
	};
	
	std::chrono::milliseconds WebServer::_work( const unsigned long int iteration_ ) {
		// TODO if it turns out that the webserver instance doesn't need to do stuff periodically, remove the
		// extend from Worker.
		return std::chrono::milliseconds( 5000 );
	}

	void WebServer::addResource( Resource resource_ ) {
		std::lock_guard<std::mutex> lock( this->m_resourcesMutex );
		std::string etag = std::to_string( rand() );
		this->m_resources[resource_.uri] = std::pair<Resource, std::string>( resource_, etag );
		g_logger->logr( Logger::LogLevel::VERBOSE, this, "Resource no. %d at " + resource_.uri + " installed.", this->m_resources.size() );
	};
	
	void WebServer::removeResourceAt( std::string uri_ ) {
		std::lock_guard<std::mutex> lock( this->m_resourcesMutex );
#ifdef _DEBUG
		assert( this->m_resources.find( uri_ ) != this->m_resources.end() && "Resource should exist before trying to remove it." );
#endif // _DEBUG
		this->m_resources.erase( uri_ );
		g_logger->logr( Logger::LogLevel::VERBOSE, this, "Resource " + uri_ + " removed, %d resources left.", this->m_resources.size() );
	};
	
	void WebServer::touchResourceAt( std::string uri_ ) {
#ifdef _DEBUG
		assert( this->m_resources.find( uri_ ) != this->m_resources.end() && "Resource should exist before trying to touch it." );
#endif // _DEBUG
		std::string etag = std::to_string( rand() );
		this->m_resources[uri_].second = etag;
	};

	void WebServer::_processHttpRequest( mg_connection* connection_, http_message* message_ ) {

		std::string methodStr = "";
		methodStr.assign( message_->method.p, message_->method.len );
		std::string queryStr = "";
		queryStr.assign( message_->query_string.p, message_->query_string.len );
		stringIsolate( queryStr, "_method=", "&", false, methodStr );
		
		// Determine method.
		ResourceMethod method = ResourceMethod::GET;
		if ( methodStr == "HEAD" ) {
			method = ResourceMethod::HEAD;
		} else if ( methodStr == "POST" ) {
			method = ResourceMethod::POST;
		} else if ( methodStr == "PUT" ) {
			method = ResourceMethod::PUT;
		} else if ( methodStr == "PATCH" ) {
			method = ResourceMethod::PATCH;
		} else if ( methodStr == "DELETE" ) {
			method = ResourceMethod::DELETE;
		} else if ( methodStr == "OPTIONS" ) {
			method = ResourceMethod::OPTIONS;
		}

		// Determine resource (the leading / is stripped, our resources start without it) and see if
		// a resource handler has been added for it.
		std::lock_guard<std::mutex> lock( this->m_resourcesMutex );
		std::string uriStr = "";
		if ( message_->uri.len >= 1 ) {
			uriStr.assign( message_->uri.p + 1, message_->uri.len - 1 );
		}
		auto resource = this->m_resources.find( uriStr );
		if (
			resource != this->m_resources.end()
			&& ( resource->second.first.methods & method ) == method
		) {
			std::stringstream headers;
			headers << "Content-Type: Content-type: application/json\r\n";
			headers << "Access-Control-Allow-Origin: *\r\n";
			
			std::string etag;
			int i = 0;
			for ( const mg_str &headerName : message_->header_names ) {
				std::string header;
				header.assign( headerName.p, headerName.len );
				if ( header == "If-None-Match" ) {
					etag.assign( message_->header_values[i].p, message_->header_values[i].len );
				}
				i++;
			}
			if ( etag == resource->second.second ) {
				// Resource here is the same as client, send not-changed header (=304).
				mg_send_head( connection_, 304, 0, headers.str().c_str() );
			} else {
				// We've got a newer version of the resource here, send it to the client.
				json outputJson;
				int code = 200;
				resource->second.first.handler->handleResource( resource->second.first, code, outputJson );

				headers << "ETag: " << resource->second.second;
				std::string output = outputJson.dump( 4 );
				mg_send_head( connection_, 200, output.length(), headers.str().c_str() );
				mg_send( connection_, output.c_str(), output.length() );
			}

			connection_->flags |= MG_F_SEND_AND_CLOSE;
		
		} else if (
			uriStr == "api"
			&& method == ResourceMethod::GET
		) {
			
			// Build a standard documentation page if the api uri is requested directly using get.
			std::stringstream outputStream;
			outputStream << "<!doctype html>" << "<html><body style=\"font-family:sans-serif;\">";
			for ( auto resourceIt = this->m_resources.begin(); resourceIt != this->m_resources.end(); resourceIt++ ) {
				outputStream << "<strong>Title:</strong> " << resourceIt->second.first.title << "<br>";
				outputStream << "<strong>Uri:</strong> <a href=\"/" << resourceIt->first << "\">" << resourceIt->first << "</a><br>";
				outputStream << "<strong>Methods:</strong>";
				if ( ( resourceIt->second.first.methods & WebServer::ResourceMethod::GET ) == WebServer::ResourceMethod::GET ) {
					outputStream << " GET";
				}
				if ( ( resourceIt->second.first.methods & WebServer::ResourceMethod::HEAD ) == WebServer::ResourceMethod::HEAD ) {
					outputStream << " HEAD";
				}
				if ( ( resourceIt->second.first.methods & WebServer::ResourceMethod::POST ) == WebServer::ResourceMethod::POST ) {
					outputStream << " POST";
				}
				if ( ( resourceIt->second.first.methods & WebServer::ResourceMethod::PUT ) == WebServer::ResourceMethod::PUT ) {
					outputStream << " PUT";
				}
				if ( ( resourceIt->second.first.methods & WebServer::ResourceMethod::PATCH ) == WebServer::ResourceMethod::PATCH ) {
					outputStream << " PATCH";
				}
				if ( ( resourceIt->second.first.methods & WebServer::ResourceMethod::DELETE ) == WebServer::ResourceMethod::DELETE ) {
					outputStream << " DELETE";
				}
				if ( ( resourceIt->second.first.methods & WebServer::ResourceMethod::OPTIONS ) == WebServer::ResourceMethod::OPTIONS ) {
					outputStream << " OPTIONS";
				}
				outputStream << "<br><br>";
			}
			
			std::string output = outputStream.str();
			mg_send_head( connection_, 200, output.length(), "Content-Type: text/html" );
			mg_send( connection_, output.c_str(), output.length() );
			connection_->flags |= MG_F_SEND_AND_CLOSE;
			
		} else {
			
			// If some other resource was accessed, serve static files instead.
			// TODO caching implementation
			// TODO gzip too?
			// TODO better 404 handling?
			mg_serve_http_opts options;
			memset( &options, 0, sizeof( options ) );
			options.document_root = "www";
			options.index_files = "index.html";
			options.enable_directory_listing = "no";
			mg_serve_http( connection_, message_, options );
		}
	}

}; // namespace micasa
