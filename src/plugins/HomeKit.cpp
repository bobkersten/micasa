#ifndef _DARWIN
	#include <avahi-common/alternative.h>
	#include <avahi-common/malloc.h>
	#include <avahi-common/error.h>
#endif // _DARWIN

#include "tlv8.h"

extern "C" {
	#include "hkdf.h"
	#include "poly1305.h"
	#include "chacha20_simple.h"
	#include "ed25519.h"
	#include "curve25519-donna.h"
}

#include "../Controller.h"
#include "../Database.h"
#include "../Logger.h"
#include "../Network.h"
#include "../User.h"
#include "../Utils.h"
#include "../device/Switch.h"

#include "HomeKit.h"

#ifndef _DARWIN
	void micasa_avahi_client_callback( AvahiClient* client_, AvahiClientState state_, void* userdata_ ) {
		auto plugin = (micasa::HomeKit*)userdata_;
		switch( state_ ) {
			case AVAHI_CLIENT_S_RUNNING:
				plugin->_createService( client_ );
				break;

			case AVAHI_CLIENT_FAILURE:
				plugin->setState( micasa::Plugin::State::FAILED );
				micasa::Logger::logr( micasa::Logger::LogLevel::ERROR, plugin, "Error in avahi client: %s.", avahi_strerror( avahi_client_errno( client_ ) ) );
				avahi_simple_poll_quit( plugin->m_poll );
				break;

			case AVAHI_CLIENT_S_COLLISION:
			case AVAHI_CLIENT_S_REGISTERING:
				if ( plugin->m_group ) {
					avahi_entry_group_reset( plugin->m_group );
				}
				break;

			case AVAHI_CLIENT_CONNECTING:
				break;

			default:
				break;
		}
	};

	void micasa_avahi_group_callback( AvahiEntryGroup* group_, AvahiEntryGroupState state_, void* userdata_ ) {
		auto plugin = (micasa::HomeKit*)userdata_;
		switch( state_ ) {
			case AVAHI_ENTRY_GROUP_ESTABLISHED:
				micasa::Logger::log( micasa::Logger::LogLevel::VERBOSE, plugin, "Avahi group established." );
				break;

			case AVAHI_ENTRY_GROUP_COLLISION: {
				char* altname = avahi_alternative_service_name( plugin->m_name );
				micasa::Logger::logr( micasa::Logger::LogLevel::WARNING, plugin, "Avahi name collision, renaming from %s to %s.", plugin->m_name, altname );
				avahi_free( plugin->m_name );
				plugin->m_name = altname;
				plugin->_increaseConfig( false );
				plugin->_createService( avahi_entry_group_get_client( group_ ) );
				break;
			}

			case AVAHI_ENTRY_GROUP_FAILURE:
				plugin->setState( micasa::Plugin::State::FAILED );
				micasa::Logger::logr( micasa::Logger::LogLevel::ERROR, plugin, "Avahi group entry failure: %s.", avahi_strerror( avahi_client_errno( avahi_entry_group_get_client( group_ ) ) ) );
				avahi_simple_poll_quit( plugin->m_poll );
				break;

			case AVAHI_ENTRY_GROUP_UNCOMMITED:
			case AVAHI_ENTRY_GROUP_REGISTERING:
				break;
		}
	};
#endif // _DARWIN

namespace micasa {

	using namespace nlohmann;

	const char* HomeKit::label = "HomeKit";

	extern std::unique_ptr<Controller> g_controller;
	extern std::unique_ptr<Database> g_database;

	const unsigned char modulus[]              = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1, 0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD, 0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45, 0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B, 0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED, 0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5, 0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6, 0x49, 0x28, 0x66, 0x51, 0xEC, 0xE4, 0x5B, 0x3D, 0xC2, 0x00, 0x7C, 0xB8, 0xA1, 0x63, 0xBF, 0x05, 0x98, 0xDA, 0x48, 0x36, 0x1C, 0x55, 0xD3, 0x9A, 0x69, 0x16, 0x3F, 0xA8, 0xFD, 0x24, 0xCF, 0x5F, 0x83, 0x65, 0x5D, 0x23, 0xDC, 0xA3, 0xAD, 0x96, 0x1C, 0x62, 0xF3, 0x56, 0x20, 0x85, 0x52, 0xBB, 0x9E, 0xD5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6D, 0x67, 0x0C, 0x35, 0x4E, 0x4A, 0xBC, 0x98, 0x04, 0xF1, 0x74, 0x6C, 0x08, 0xCA, 0x18, 0x21, 0x7C, 0x32, 0x90, 0x5E, 0x46, 0x2E, 0x36, 0xCE, 0x3B, 0xE3, 0x9E, 0x77, 0x2C, 0x18, 0x0E, 0x86, 0x03, 0x9B, 0x27, 0x83, 0xA2, 0xEC, 0x07, 0xA2, 0x8F, 0xB5, 0xC5, 0x5D, 0xF0, 0x6F, 0x4C, 0x52, 0xC9, 0xDE, 0x2B, 0xCB, 0xF6, 0x95, 0x58, 0x17, 0x18, 0x39, 0x95, 0x49, 0x7C, 0xEA, 0x95, 0x6A, 0xE5, 0x15, 0xD2, 0x26, 0x18, 0x98, 0xFA, 0x05, 0x10, 0x15, 0x72, 0x8E, 0x5A, 0x8A, 0xAA, 0xC4, 0x2D, 0xAD, 0x33, 0x17, 0x0D, 0x04, 0x50, 0x7A, 0x33, 0xA8, 0x55, 0x21, 0xAB, 0xDF, 0x1C, 0xBA, 0x64, 0xEC, 0xFB, 0x85, 0x04, 0x58, 0xDB, 0xEF, 0x0A, 0x8A, 0xEA, 0x71, 0x57, 0x5D, 0x06, 0x0C, 0x7D, 0xB3, 0x97, 0x0F, 0x85, 0xA6, 0xE1, 0xE4, 0xC7, 0xAB, 0xF5, 0xAE, 0x8C, 0xDB, 0x09, 0x33, 0xD7, 0x1E, 0x8C, 0x94, 0xE0, 0x4A, 0x25, 0x61, 0x9D, 0xCE, 0xE3, 0xD2, 0x26, 0x1A, 0xD2, 0xEE, 0x6B, 0xF1, 0x2F, 0xFA, 0x06, 0xD9, 0x8A, 0x08, 0x64, 0xD8, 0x76, 0x02, 0x73, 0x3E, 0xC8, 0x6A, 0x64, 0x52, 0x1F, 0x2B, 0x18, 0x17, 0x7B, 0x20, 0x0C, 0xBB, 0xE1, 0x17, 0x57, 0x7A, 0x61, 0x5D, 0x6C, 0x77, 0x09, 0x88, 0xC0, 0xBA, 0xD9, 0x46, 0xE2, 0x08, 0xE2, 0x4F, 0xA0, 0x74, 0xE5, 0xAB, 0x31, 0x43, 0xDB, 0x5B, 0xFC, 0xE0, 0xFD, 0x10, 0x8E, 0x4B, 0x82, 0xD1, 0x20, 0xA9, 0x3A, 0xD2, 0xCA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	const unsigned char generator[]            = { 0x05 };
	const unsigned char accessorySecretKey[32] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1, 0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74 };
	const unsigned char curveBasePoint[]       = { 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	enum class TLVType: uint8_t {
		REQUEST_TYPE   = 0x00,
		USERNAME       = 0x01,
		SALT           = 0x02,
		PUBLIC_KEY     = 0x03,
		PASSWORD_PROOF = 0x04,
		ENCRYPTED_DATA = 0x05,
		SEQUENCE_NUM   = 0x06,
		ERROR_CODE     = 0x07,
		PROOF          = 0x0a,
	}; // enum class TLVType

	enum class TLVCode: uint8_t {
		INVALID_REQUEST   = 0x02,
		INVALID_SIGNATURE = 0x04,
	}; // enum class TLVCode

	enum class HAPCharacteristic: unsigned int {
		NAME          = 10,
		MANUFACTURER  = 11,
		SERIAL        = 12,
		MODEL         = 13,
		IDENTIFY      = 14,
		S_NAME        = 15,

		BATTERY_LEVEL = 20,
		BATTERY_STATE = 21,
		BATTERY_LOW   = 22,
	}; // enum class HAPCharacteristic

	const unsigned short LOW_BATTERY_THRESHOLD = 30;

	const unsigned short HAP_PERM_PR = (1 << 0);
	const unsigned short HAP_PERM_PW = (1 << 1);
	const unsigned short HAP_PERM_EV = (1 << 2);

	// NOTE custom/additional characteristics should be added in three separate places, updateDevice, _handleAccessories
	// and _handleCharacteristic (GET and optionally PUT).
	typedef struct { std::string service_uuid; std::string characteristic_uuid; std::string format; unsigned short iid; unsigned short permissions; } HAPDefenition;
	const std::map<Device::Type, std::map<std::string, HAPDefenition>> HAPMappings {
		{ Device::Type::SWITCH, {
			{ Switch::resolveTextSubType( Switch::SubType::GENERIC ),         { "49", "25", "bool",  100,  HAP_PERM_PR | HAP_PERM_PW | HAP_PERM_EV } },
			{ Switch::resolveTextSubType( Switch::SubType::TOGGLE ),          { "49", "25", "bool",  100,  HAP_PERM_PR | HAP_PERM_PW | HAP_PERM_EV } },
			{ Switch::resolveTextSubType( Switch::SubType::LIGHT ),           { "43", "25", "bool",  101, HAP_PERM_PR | HAP_PERM_PW | HAP_PERM_EV } },
			{ Switch::resolveTextSubType( Switch::SubType::MOTION_DETECTOR ), { "85", "22", "bool",  102, HAP_PERM_PR | HAP_PERM_EV } },
			{ Switch::resolveTextSubType( Switch::SubType::FAN ),             { "B7", "B0", "uint8", 103, HAP_PERM_PR | HAP_PERM_PW | HAP_PERM_EV } },
			{ Switch::resolveTextSubType( Switch::SubType::OCCUPANCY ),       { "86", "71", "uint8", 104, HAP_PERM_PR | HAP_PERM_EV } },
			{ Switch::resolveTextSubType( Switch::SubType::CONTACT ),         { "80", "6A", "uint8", 105, HAP_PERM_PR | HAP_PERM_EV } },
			{ Switch::resolveTextSubType( Switch::SubType::SMOKE_DETECTOR ),  { "87", "76", "uint8", 106, HAP_PERM_PR | HAP_PERM_EV } },
			{ Switch::resolveTextSubType( Switch::SubType::CO_DETECTOR ),     { "7F", "69", "uint8", 107, HAP_PERM_PR | HAP_PERM_EV } },
			// NOTE The security system service requires additional code and a second characteristic_uuid, leave a gap.
			{ Switch::resolveTextSubType( Switch::SubType::ALARM ),           { "7E", "66", "uint8", 109, HAP_PERM_PR | HAP_PERM_EV } },
			// NOTE the blinds service requires two additional codes and two additional charateristics, leave gaps.
			{ Switch::resolveTextSubType( Switch::SubType::BLINDS ),          { "8C", "6D", "uint8", 112, HAP_PERM_PR | HAP_PERM_EV } },
		} },
		{ Device::Type::LEVEL, {
			{ Level::resolveTextSubType( Level::SubType::TEMPERATURE ),       { "8A", "11", "float", 200, HAP_PERM_PR | HAP_PERM_EV } },
			{ Level::resolveTextSubType( Level::SubType::HUMIDITY ),          { "82", "10", "float", 201, HAP_PERM_PR | HAP_PERM_EV } },
			{ Level::resolveTextSubType( Level::SubType::LUMINANCE ),         { "84", "6B", "float", 202, HAP_PERM_PR | HAP_PERM_EV } },
			// NOTE an additional on/off characteristic is added for the dimmer, leave a gap in iid for it.
			{ Level::resolveTextSubType( Level::SubType::DIMMER ),            { "43", "8",  "int",   204, HAP_PERM_PR | HAP_PERM_PW | HAP_PERM_EV } },
		} },
	};

	HomeKit::HomeKit( const unsigned int id_, const Plugin::Type type_, const std::string reference_, const std::shared_ptr<Plugin> parent_ ) :
		Plugin( id_, type_, reference_, parent_ ),
#ifdef _DARWIN
		m_dnssd( NULL ),
#else
		m_client( NULL ),
		m_poll( NULL ),
		m_group( NULL ),
#endif // _DARWIN
		m_srp( NULL ),
		m_publicKey( NULL ),
		m_secretKey( NULL )
	{
		SRP_initialize_library();
	};

	HomeKit::~HomeKit() {
		if ( this->m_srp ) {
			SRP_free( this->m_srp );
		}
		SRP_finalize_library();
	}

	void HomeKit::start() {
		Logger::log( Logger::LogLevel::VERBOSE, this, "Starting..." );
		Plugin::start();

		auto handler = [this]( std::shared_ptr<Network::Connection> connection_, Network::Connection::Event event_ ) -> void {
			switch( event_ ) {
				case Network::Connection::Event::CONNECT: {
					std::lock_guard<std::mutex> lock( this->m_sessionsMutex );
					this->m_sessions.emplace(
						std::piecewise_construct,
						std::forward_as_tuple( connection_ ),
						std::forward_as_tuple( connection_ )
					);
					break;
				}

				case Network::Connection::Event::DATA: {
					// Once raw data packages are received instead of parsed http messages, an (encrypted) session
					// should be present.
					std::unique_lock<std::mutex> sessionsLock( this->m_sessionsMutex );
					if ( this->m_sessions.find( connection_ ) != this->m_sessions.end() ) {
						Session& session = this->m_sessions.at( connection_ );
						std::lock_guard<std::mutex> lock( session.m_sessionMutex );
						sessionsLock.unlock();

						std::string data = connection_->popData();
						auto packet = data.c_str();
						uint16_t len = data.size();
						uint16_t length = (uint8_t)packet[1] * 256 + (uint8_t)*packet;

						chacha20_ctx chacha20;
						bzero( &chacha20, sizeof( chacha20 ) );
						chacha20_setup( &chacha20, (const uint8_t*)session.m_controllerToAccessoryKey, 32, (uint8_t*)&session.m_controllerToAccessoryCount );
						session.m_controllerToAccessoryCount++;

						char temp[64];
						bzero(temp, 64);
						char temp2[64];
						bzero(temp2, 64);
						chacha20_encrypt( &chacha20, (const uint8_t*)temp, (uint8_t *)temp2, 64 );

						char verify[16];
						bzero( verify, 16 );
						poly1305_genkey( (const unsigned char*)temp2, (uint8_t*)packet, length, Type_Data_With_Length, verify );

						char decrypted[2048];
						bzero( decrypted, 2048 );
						chacha20_encrypt( &chacha20, (const uint8_t*)&packet[2], (uint8_t*)decrypted, length );

						if (
							len >= (2 + length + 16 )
							&& memcmp( (void*)verify, (void*)&packet[2 + length], 16 ) == 0
						) {
							if ( mg_parse_http( decrypted, length, &connection_->m_http, true ) > 0 ) {
								this->_processRequest( session );
							} else {
								Logger::log( Logger::LogLevel::ERROR, this, "Unable to parse session http message." );
							}
						} else {
							Logger::log( Logger::LogLevel::ERROR, this, "Session packet verification failed." );
						}
					} else {
						Logger::log( Logger::LogLevel::ERROR, this, "Session is not open." );
					}
					break;
				}

				case Network::Connection::Event::HTTP: {
					// During the pairing phase the connection behaves like a standard unencrypted http connection that
					// can be processed by mongoose.
					std::unique_lock<std::mutex> sessionsLock( this->m_sessionsMutex );
					if ( this->m_sessions.find( connection_ ) != this->m_sessions.end() ) {
						Session& session = this->m_sessions.at( connection_ );
						std::lock_guard<std::mutex> lock( session.m_sessionMutex );
						sessionsLock.unlock();
						this->_processRequest( session );
					} else {
						Logger::log( Logger::LogLevel::ERROR, this, "Session is not open." );
					}
					break;
				}

				case Network::Connection::Event::DROPPED:
				case Network::Connection::Event::CLOSE: {
					Logger::log( Logger::LogLevel::VERBOSE, this, "Controller disconnected." );
					std::lock_guard<std::mutex> lock( this->m_sessionsMutex );
					auto find = this->m_sessions.find( connection_ );
					if ( find != this->m_sessions.end() ) {
						this->m_sessions.erase( find );
					}
					if ( this->m_sessions.size() == 0 ) {
						this->setState( Plugin::State::DISCONNECTED );
					}
					break;
				}

				default: { break; }
			}
		};

		// The HAP service uses http-like requests on an arbitrary port, so we're going to listen on a port selected
		// by the operating system (hence the :0 part of the uri).
#ifdef _IPV6_ENABLED
		this->m_bind = Network::bind( "[::]:0", handler );
#else
		this->m_bind = Network::bind( "0.0.0.0:0", handler );
#endif // _IPV6_ENABLED
		if ( ! this->m_bind ) {
			Logger::log( Logger::LogLevel::ERROR, this, "Unable to bind." );
			this->setState( Plugin::State::FAILED );
			return;
		}

		// Everytime the plugin is restarted the config number should increase, which makes sure the controller
		// reinitializes it's config and connects to the right port.
		this->_increaseConfig( false );
#ifdef _DARWIN
		this->_createService();
#else
		// Pick a default bonjour name. This name is updated with a suffix if a collision is detected.
		this->m_name = avahi_strdup( this->getName().c_str() );

		// Allocate main avahi loop object.
		if ( ! ( this->m_poll = avahi_simple_poll_new() ) ) {
			Logger::log( Logger::LogLevel::ERROR, this, "Unable to initialize avahi poller." );
			this->setState( Plugin::State::FAILED );
			return;
		}

		// Allocate new avahi client.
		int error;
		this->m_client = avahi_client_new( avahi_simple_poll_get( this->m_poll ), static_cast<AvahiClientFlags>( 0 ), micasa_avahi_client_callback, this, &error );
		if ( ! this->m_client ) {
			Logger::logr( Logger::LogLevel::ERROR, this, "Unable to create avahi client: %s", avahi_strerror( error ) );
			this->setState( Plugin::State::FAILED );
			return;
		}

		// Run the main avahi loop in a separate thread.
		this->m_worker = std::thread( [this]() -> void {
			if ( 1 != avahi_simple_poll_loop( this->m_poll ) ) {
				Logger::log( Logger::LogLevel::ERROR, this, "The avahi poller stopped unexpectedly." );
			}
			if ( this->m_group ) {
				avahi_entry_group_free( this->m_group );
				this->m_group = NULL;
			}
			if ( this->m_client ) {
				avahi_client_free( this->m_client );
			}
			if ( this->m_poll ) {
				avahi_simple_poll_free( this->m_poll );
			}
		} );
#endif // _DARWIN

		// The keep-alive task will send periodic empty events to the iOS controller to make sure the connection is
		// still open.
		this->m_scheduler.schedule( 0, SCHEDULER_INTERVAL_5MIN, SCHEDULER_REPEAT_INFINITE, this, [this]( std::shared_ptr<Scheduler::Task<>> ) {
			std::lock_guard<std::mutex> lock( this->m_sessionsMutex );
			for ( auto& sessionIt : this->m_sessions ) {
				auto& session = sessionIt.second;
				std::lock_guard<std::mutex> lock( session.m_sessionMutex );
				if ( session.m_encrypted ) {
					json output = {
						{ "characteristics", json::array() }
					};
					session.send( "EVENT/1.0 200 OK", "Content-Type: application/hap+json", output.dump() );
				}
			}
		} );

		// As long as there's no connection from the controller (an iOS device) we remain in disconnected state.
		this->setState( Plugin::State::DISCONNECTED );
	};

	void HomeKit::stop() {
		Logger::log( Logger::LogLevel::VERBOSE, this, "Stopping..." );

		this->m_scheduler.erase( [this]( const Scheduler::BaseTask& task_ ) {
			return task_.data == this;
		} );

#ifdef _DARWIN
		DNSServiceRefDeallocate( this->m_dnssd );
		this->m_dnssd = NULL;
#else
		if ( this->m_poll ) {
			avahi_simple_poll_quit( this->m_poll );
		}
		avahi_free( this->m_name );
		if ( this->m_worker.joinable() ) {
			this->m_worker.join();
		}
#endif // _DARWIN

		if ( this->m_bind ) {
			this->m_bind->terminate();
		}
		std::lock_guard<std::mutex> lock( this->m_sessionsMutex );
		for ( auto& sessionIt : this->m_sessions ) {
			sessionIt.second.m_connection->terminate();
		}

		Plugin::stop();
	};

	bool HomeKit::updateDevice( const Device::UpdateSource& source_, std::shared_ptr<Device> device_, bool owned_, bool& apply_ ) {
		if ( device_->getSettings()->get<bool>( "enable_homekit_" + this->getReference(), false ) ) {

			// Events for this device are only sent to sessions that have specificially asked for this device to be
			// monitored by a characteristic PUT request with an "ev" property.
			std::unique_lock<std::mutex> sessionsLock( this->m_sessionsMutex );
			for ( auto& sessionIt : this->m_sessions ) {
				for ( auto& devicePtr : sessionIt.second.m_devices ) {
					if ( devicePtr.lock() == device_ ) {
						auto& session = sessionIt.second;

						// Sending of events is done in a separate thread that locks on the session. This prevents an
						// event being sent in the middle of some other request or response. Events are also sent twice
						// because they sometimes get lost somewhere within the Apple ecosystem :)
						this->m_scheduler.schedule( 10, 250, 2, this, [this,device_,&session]( std::shared_ptr<Scheduler::Task<>> ) {
							std::lock_guard<std::mutex> lock( session.m_sessionMutex );

							try {
								std::string subtype = device_->getSettings()->get( "subtype", device_->getSettings()->get( DEVICE_SETTING_DEFAULT_SUBTYPE, "generic" ) );
								const HAPDefenition& defenition = HAPMappings.at( device_->getType() ).at( subtype );
								if ( ( defenition.permissions & HAP_PERM_EV ) != HAP_PERM_EV ) {
									throw std::runtime_error( "Device " + device_->getName() + " doesn't support events." );
								}

								json characteristic = {
									{ "aid", device_->getId() + 1 },
									{ "iid", defenition.iid }
								};
								this->_addHAPValue( device_, defenition.format, characteristic );
								json output = {
									{ "characteristics", json::array( { characteristic } ) }
								};

								// Services with additional required characteristics are added here.
								if ( subtype == Switch::resolveTextSubType( Switch::SubType::ALARM ) ) {
									unsigned int state = 3;
									if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::HOME ) {
										state = 0;
									} else if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::AWAY ) {
										state = 1;
									}
									output["characteristics"] += {
										{ "aid", device_->getId() + 1 },
										{ "iid", defenition.iid - 1 }, // should be left a gap in HAPMappings
										{ "value", state }
									};
								} else if ( subtype == Switch::resolveTextSubType( Switch::SubType::BLINDS ) ) {
									unsigned int state = 2; // stopped
									if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::OPEN ) {
										state = 1; // going to the maximum value
									} else if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::CLOSE ) {
										state = 0; // going to the minimum value
									}
									output["characteristics"] += {
										{ "aid", device_->getId() + 1 },
										{ "iid", defenition.iid - 2 }, // should be left a gap in HAPMappings
										{ "value", state }
									};
									state = 50;
									if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::OPEN ) {
										state = 100;
									} else if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::CLOSE ) {
										state = 0;
									}
									output["characteristics"] += {
										{ "aid", device_->getId() + 1 },
										{ "iid", defenition.iid - 1 }, // should be left a gap in HAPMappings
										{ "value", state }
									};
								} else if ( subtype == Level::resolveTextSubType( Level::SubType::DIMMER ) ) {
									output["characteristics"] += {
										{ "aid", device_->getId() + 1 },
										{ "iid", defenition.iid - 1 }, // should be left a gap in HAPMappings
										{ "value", ( std::static_pointer_cast<Level>( device_ )->getValue() > 0 ) }
									};
								}
								session.send( "EVENT/1.0 200 OK", "Content-Type: application/hap+json", output.dump() );

							} catch( std::out_of_range exception_ ) {
								Logger::logr( Logger::LogLevel::ERROR, this, "Device %s is not supported.", device_->getName().c_str() );
								device_->getSettings()->remove( "enable_homekit_" + this->getReference() );
								device_->getSettings()->commit();
							} catch( std::runtime_error exception_ ) {
								Logger::log( Logger::LogLevel::ERROR, this, exception_.what() );
							}
						} );
					}
				}
			}
		}
		return true;
	};

	json HomeKit::getJson() const {
		json result = Plugin::getJson();
		result["setup_code"] = this->_getSetupCode();
		return result;
	};

	json HomeKit::getSettingsJson() const {
		json result = Plugin::getSettingsJson();
		for ( auto &&setting : HomeKit::getEmptySettingsJson( true ) ) {
			result.push_back( setting );
		}
		result += {
			{ "name", "setup_code" },
			{ "label", "Setup Code" },
			{ "type", "display" },
			{ "mandatory", true },
			{ "sort", 99 }
		};
		return result;
	};

	json HomeKit::getEmptySettingsJson( bool advanced_ ) {
		json result = json::array();
		return result;
	};

	void HomeKit::updateDeviceJson( std::shared_ptr<const Device> device_, nlohmann::json& json_, bool owned_ ) const {
		json_["enable_homekit_" + this->getReference()] = device_->getSettings()->get<bool>( "enable_homekit_" + this->getReference(), false );
	};

	void HomeKit::updateDeviceSettingsJson( std::shared_ptr<const Device> device_, nlohmann::json& json_, bool owned_ ) const {
		auto getSetting = [this]() -> json {
			return {
				{ "name", "enable_homekit_" + this->getReference() },
				{ "label", "HomeKit" },
				{ "badge", "Enable HomeKit" },
				{ "description", "Enable this setting to make this device accessible by HomeKit controllers." },
				{ "type", "boolean" },
				{ "sort", 110 },
				{ "class", "advanced" }
			};
		};

		// If the subtype of the device can be changed by the user, the HomeKit setting should be shown only when a
		// supported subtype is selected.
		if ( __unlikely( device_->getSettings()->get<bool>( DEVICE_SETTING_ALLOW_SUBTYPE_CHANGE, false ) ) ) {
			for ( auto& setting : json_ ) {
				if ( setting["name"] == "subtype" ) {
					for ( auto& subtype : setting["options"] ) {
						try {
							HAPMappings.at( device_->getType() ).at( subtype["value"] );
							subtype["settings"] = json::array();
							subtype["settings"].push_back( getSetting() );
						} catch( std::out_of_range exception_ ) { }
					}
				}
			}

		// If the subtype is fixed, the HomeKit setting should only be shown when the subtype is supported.
		} else {
			std::string subtype = device_->getSettings()->get( "subtype", device_->getSettings()->get( DEVICE_SETTING_DEFAULT_SUBTYPE, "generic" ) );
			try {
				HAPMappings.at( device_->getType() ).at( subtype );
				json_.push_back( getSetting() );
			} catch( std::out_of_range exception_ ) { }
		}
	};

	void HomeKit::putDeviceSettingsJson( std::shared_ptr<Device> device_, const nlohmann::json& json_, bool owned_ ) {

		// If the HomeKit setting of a device is changed, the configuration number needs to be increasted for the
		// HomeKit controller to pick up the changes.
		bool increaseConfig = false;
		std::string setting = "enable_homekit_" + this->getReference();
		if ( json_.find( setting ) != json_.end() ) {
			if (
				! device_->getSettings()->contains( setting )
				&& jsonGet<bool>( json_[setting] )
			) {
				increaseConfig = true;
			} else if (
				device_->getSettings()->get<bool>( setting, false ) != jsonGet<bool>( json_[setting] )
				|| jsonGet<bool>( json_[setting] ) // also increase config on name changes
			) {
				increaseConfig = true;
			}
		} else {
			if ( device_->getSettings()->contains( setting ) ) {
				device_->getSettings()->remove( setting );
				increaseConfig = true;
			}
		}
		if ( increaseConfig ) {
			this->_increaseConfig( true );
		}
	};

	void HomeKit::beforeRemoveDevice( const std::shared_ptr<Device> device_ ) {

		// If a device is removed from the system and it had HomeKit enabled, the configuration number needs to be
		// increased for the HomeKit controller to be aware if this removal.
		std::string setting = "enable_homekit_" + this->getReference();
		if ( device_->getSettings()->contains( setting ) ) {
			if ( device_->getSettings()->get<bool>( setting ) ) {
				this->_increaseConfig( true );
			}
			device_->getSettings()->remove( setting );
		}
	};

	void HomeKit::_increaseConfig( bool createService_ ) {
		Logger::log( Logger::LogLevel::VERBOSE, this, "Increasing HAP configuration index." );
		int config = this->m_settings->get( "_configuration_number", 1 );
		this->m_settings->put( "_configuration_number", ++config );
		this->m_settings->commit();

		if ( createService_ ) {
#ifdef _DARWIN
			this->_createService();
#else
			if ( this->m_group ) {
				avahi_entry_group_reset( this->m_group );
			}
			if ( this->m_client ) {
				this->_createService( this->m_client );
			}
#endif // _DARWIN
		}
	};

#ifdef _DARWIN
	void HomeKit::_createService() {
		if ( this->m_dnssd != NULL ) {
			DNSServiceRefDeallocate( this->m_dnssd );
			this->m_dnssd = NULL;
		}

		std::string config = "c#=" + this->m_settings->get( "_configuration_number", "1" );
		std::string id = "id=" + this->_getAccessoryId();
		std::string status;
		if ( this->m_settings->get( "paired", false ) ) {
			status = "sf=0";
		} else {
			status = "sf=1"; // not paired
		}
		auto txtparts = {
			config.c_str(),
			"ff=0",
			id.c_str(),
			"md=HomeKitPlugin1,1",
			"pv=1.0",
			"s#=1",
			status.c_str(),
			"ci=2",
		};
		std::stringstream txt;
		for ( const auto& txtpart : txtparts ) {
			txt << char( strlen( txtpart ) ) << txtpart;
		}

		Logger::logr( Logger::LogLevel::VERBOSE, this, "Using configuration number %s.", config.c_str() );
		DNSServiceErrorType error = DNSServiceRegister(
			&this->m_dnssd,
			0, // no flags
			0, // all network interfaces
			this->getName().c_str(),
			"_hap._tcp",
			NULL, // default domain
			NULL, // use default host name
			htons( this->m_bind->getPort() ), // port number
			txt.str().size(), // length of TXT record
			txt.str().c_str(),
			NULL, // callback function
			this
		);
		if ( error != kDNSServiceErr_NoError ) {
			Logger::log( Logger::LogLevel::ERROR, this, "Unable to register bonjour service." );
			this->setState( Plugin::State::FAILED );
		}
	};
#else
	void HomeKit::_createService( AvahiClient* client_ ) {

		// If this is the first time we're called, let's create a new entry group if necessary.
		if ( ! this->m_group ) {
			if ( ! ( this->m_group = avahi_entry_group_new( client_, micasa_avahi_group_callback, this ) ) ) {
				this->setState( Plugin::State::FAILED );
				Logger::logr( Logger::LogLevel::ERROR, this, "Unable to create avahi group: %s.", avahi_strerror( avahi_client_errno( client_ ) ) );
				avahi_simple_poll_quit( this->m_poll );
				return;
			}
		}

		// If the group is empty (because it was just created, or because it was reset previously, add our entries.
		if ( avahi_entry_group_is_empty( this->m_group ) ) {
			std::string config = "c#=" + this->m_settings->get( "_configuration_number", "1" );
			std::string id = "id=" + this->_getAccessoryId();
			std::string status;
			if ( this->m_settings->get( "paired", false ) ) {
				status = "sf=0";
			} else {
				status = "sf=1"; // not paired
			}
			Logger::logr( Logger::LogLevel::VERBOSE, this, "Using configuration number %s.", config.c_str() );
			int ret = avahi_entry_group_add_service( this->m_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, static_cast<AvahiPublishFlags>( 0 ), this->m_name, "_hap._tcp", NULL, NULL, this->m_bind->getPort(),
				config.c_str(),
				// Feature flags (e.g. "0x3" for bits 0 and 1). Required if non-zero. See Table 5-8.
				"ff=0",
				id.c_str(),
				"md=HomeKitPlugin1,1",
				"pv=1.0",
				"s#=1",
				status.c_str(),
				"ci=2",
				NULL
			);
			if ( ret < 0 ) {
				switch( ret ) {
					case AVAHI_ERR_COLLISION: {
						char* altname = avahi_alternative_service_name( this->m_name );
						Logger::logr( Logger::LogLevel::NOTICE, this, "Avahi name collision, renaming from %s to %s.", this->m_name, altname );
						avahi_free( this->m_name );
						this->m_name = altname;
						avahi_entry_group_reset( this->m_group );
						this->_increaseConfig( false );
						this->_createService( client_ );
						break;
					}
					default:
						this->setState( Plugin::State::FAILED );
						Logger::logr( Logger::LogLevel::ERROR, this, "Failed to add avahi service: %s.", avahi_strerror( ret ) );
						avahi_simple_poll_quit( this->m_poll );
						break;
				}
				return;
			}

			// Tell the server to register the service.
			if ( ( ret = avahi_entry_group_commit( this->m_group ) ) < 0 ) {
				this->setState( Plugin::State::FAILED );
				Logger::logr( Logger::LogLevel::ERROR, this, "Failed to commit avahi group: %s.", avahi_strerror( ret ) );
				avahi_simple_poll_quit( this->m_poll );
			}
		}
	};
#endif // _DARWIN

	void HomeKit::_processRequest( Session& session_ ) {
		std::string uri = session_.m_connection->getUri();
		if ( uri.substr( 0, 9 ) == "/identify" ) {
			this->_handleIdentify( session_ );
		} else if ( uri.substr( 0, 11 ) == "/pair-setup" ) {
			this->_handlePair( session_ );
		} else if ( uri.substr( 0, 12 ) == "/pair-verify" ) {
			this->_handlePairVerify( session_ );
		} else if ( uri.substr( 0, 9 ) == "/pairings" ) {
			this->_handlePairings( session_ );
		} else if ( uri.substr( 0, 12 ) == "/accessories" ) {
			this->_handleAccessories( session_ );
		} else if ( uri.substr( 0, 16 ) == "/characteristics" ) {
			this->_handleCharacteristics( session_ );
		} else {
			Logger::logr( Logger::LogLevel::WARNING, this, "Unsupported request uri %s.", uri.c_str() );
		}
	};

	void HomeKit::_handleIdentify( Session& session_ ) {
		std::string method = session_.m_connection->getMethod();
		Logger::logr( Logger::LogLevel::VERBOSE, this, "Identify %s request received.", method.c_str() );

		// HAP par 5.7.6 - This call is only valid if we're unpaired. If we're paired the identify routine should
		// be initiated with a call to /characteristics with the proper iid.
		if ( method == "POST" ) {
			if ( false == this->m_settings->get( "paired", false ) ) {
				Logger::log( Logger::LogLevel::NOTICE, this, "Identify routine initiated." );
				session_.m_connection->reply( "", 204 /* no content */, { } );
			} else {
				Logger::log( Logger::LogLevel::WARNING, this, "Identify called while already paired with controller." );
				json result = json::object();
				result["status"] = -70401;
				session_.m_connection->reply( result.dump(), 400 /* bad request */, {
					{ "Content-Type", "application/hap+json" }
				} );
			}
		} else {
			Logger::logr( Logger::LogLevel::ERROR, this, "Invalid identify %s request.", method.c_str() );
		}
	};

	void HomeKit::_handlePair( Session& session_ ) {
		std::string method = session_.m_connection->getMethod();
		Logger::logr( Logger::LogLevel::VERBOSE, this, "Pair %s request received.", method.c_str() );

		try {

			// Parse received tlv data and determine if it's valid and contains the required sequence number.
			std::string body = session_.m_connection->getBody();
			TLV8Class TLV8 = TLV8Class();
			struct tlv_map tlvmap_in;
			memset( &tlvmap_in, 0, sizeof( tlv_map ) );
			if ( TLV_SUCESS != TLV8.decode( reinterpret_cast<uint8_t*>( &body[0] ), body.length(), &tlvmap_in ) ) {
				throw std::runtime_error( "Invalid pair request." );
			}
			tlv seqnum = TLV8.getTLVByType( tlvmap_in, (uint8_t)TLVType::SEQUENCE_NUM );
			if ( seqnum.type != (uint8_t)TLVType::SEQUENCE_NUM ) {
				throw std::runtime_error( "Missing sequence number in pair request." );
			}

			// Pairing step 1, M1 + M2.
			if ( seqnum.data[0] == 0x01 ) {

				Logger::log( Logger::LogLevel::VERBOSE, this, "Pairing step 1/3." );
#ifdef _DEBUG
				Logger::logr( Logger::LogLevel::NOTICE, this, "Use setup code %s.", this->_getSetupCode().c_str() );
#endif // _DEBUG

				// Create a new SRP (Secure Remote Password) instance, freeing an existing one if this is not the first
				// pair attempt being made by the plugin.
				if ( this->m_srp ) {
					SRP_free( this->m_srp );
				}
				this->m_srp = SRP_new( SRP6a_server_method() );

				const std::string salt = randomString( 16 );
				SRP_set_username( this->m_srp, "Pair-Setup" );
				SRP_set_params( this->m_srp, (const unsigned char*)modulus, sizeof( modulus ) / sizeof( modulus[0] ), (const unsigned char*)generator, 1, (const unsigned char*)salt.c_str(), 16 );
				SRP_set_auth_password( this->m_srp, this->_getSetupCode().c_str() );
				SRP_gen_pub( this->m_srp, &this->m_publicKey );

				// Create output tlv data.
				struct tlv_map tlvmap_out;
				memset( &tlvmap_out, 0, sizeof( tlv_map ) );
				uint8_t seqnum = 0x02;
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( &seqnum, (int8_t)TLVType::SEQUENCE_NUM, 1 ) );
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( (unsigned char*)salt.c_str(), (int8_t)TLVType::SALT, 16 ) );
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( (unsigned char*)this->m_publicKey->data, (int8_t)TLVType::PUBLIC_KEY, this->m_publicKey->length ) );
				uint8_t* output;
				uint32_t length;
				TLV8.encode( &tlvmap_out, &output, &length );

				session_.m_connection->reply( std::string( (char*)output, length ), 200, {
					{ "Content-Type", "application/pairing+tlv8" }
				} );

				TLV8.TLVFree( &tlvmap_out );
				free( output );

			// Pairing step 2, M3 + M4.
			} else if ( seqnum.data[0] == 0x03 ) {

				Logger::log( Logger::LogLevel::VERBOSE, this, "Pairing step 2/3." );

				// In addition to a sequence number, this step of the verification requires additional types to be
				// present in the tlv data.
				tlv pubkey = TLV8.getTLVByType( tlvmap_in, (uint8_t)TLVType::PUBLIC_KEY );
				if ( pubkey.type != (uint8_t)TLVType::PUBLIC_KEY ) {
					throw std::runtime_error( "Missing public key in pair request." );
				}
				tlv proof = TLV8.getTLVByType( tlvmap_in, (uint8_t)TLVType::PASSWORD_PROOF );
				if ( proof.type != (uint8_t)TLVType::PASSWORD_PROOF ) {
					throw std::runtime_error( "Missing password proof in pair request." );
				}

				SRP_compute_key( this->m_srp, &this->m_secretKey, pubkey.data, pubkey.size );
				SRP_RESULT result = SRP_verify( this->m_srp, proof.data, proof.size );

				// Create output tlv data.
				struct tlv_map tlvmap_out;
				memset( &tlvmap_out, 0, sizeof( tlv_map ) );
				uint8_t seqnum = 0x04;
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( &seqnum, (int8_t)TLVType::SEQUENCE_NUM, 1 ) );
				if ( ! SRP_OK( result ) ) {
					Logger::log( Logger::LogLevel::ERROR, this, "Invalid setup code provided." );
					uint8_t code = (int8_t)TLVCode::INVALID_REQUEST;
					TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( &code, (int8_t)TLVType::ERROR_CODE, 1 ) );
				} else {
					cstr* response = NULL;
					SRP_respond( this->m_srp, &response );
					TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( (unsigned char*)response->data, (int8_t)TLVType::PASSWORD_PROOF, response->length ) );
				}
				uint8_t* output;
				uint32_t length;
				TLV8.encode( &tlvmap_out, &output, &length );

				session_.m_connection->reply( std::string( (char*)output, length ), 200, {
					{ "Content-Type", "application/pairing+tlv8" }
				} );

				TLV8.TLVFree( &tlvmap_out );
				free( output );

			// Pairing step 3, M5 + M6.
			} else if ( seqnum.data[0] == 0x05 ) {

				Logger::log( Logger::LogLevel::VERBOSE, this, "Pairing step 3/3." );

				// In addition to a sequence number, this step of the verification requires additional types to be
				// present in the tlv data.
				tlv encrypted = TLV8.getTLVByType( tlvmap_in, (uint8_t)TLVType::ENCRYPTED_DATA );
				if ( encrypted.type != (uint8_t)TLVType::ENCRYPTED_DATA ) {
					throw std::runtime_error( "Missing encrypted data in pair request." );
				}

				{
					const char salt[] = "Pair-Setup-Encrypt-Salt";
					const char info[] = "Pair-Setup-Encrypt-Info";
					if ( 0 != hkdf( (const unsigned char*)salt, strlen( salt ), (const unsigned char*)this->m_secretKey->data, this->m_secretKey->length, (const unsigned char*)info, strlen( info ), (uint8_t*)this->m_sessionKey, 32 ) ) {
						throw std::runtime_error( "Pairing encryption failure." );
					}
				}

				chacha20_ctx chacha20;
				bzero( &chacha20, sizeof( chacha20 ) );
				chacha20_setup( &chacha20, (const uint8_t *)this->m_sessionKey, 32, (uint8_t*)"PS-Msg05" );

				char temp[64];
				bzero( temp, 64 );
				char temp2[64];
				bzero( temp2, 64 );
				chacha20_encrypt( &chacha20, (const uint8_t*)temp, (uint8_t *)temp2, 64 );

				char verify[16];
				bzero( verify, 16 );
				poly1305_genkey( (const unsigned char*)temp2, (unsigned char*)encrypted.data, encrypted.size - 16, Type_Data_Without_Length, verify );

				uint8_t size = encrypted.size - 16;
				char decrypted[size];
				chacha20_decrypt( &chacha20, encrypted.data, (uint8_t*)decrypted, size );

				if ( memcmp( (void*)verify, (void*)&encrypted.data[encrypted.size - 16], 16 ) != 0 ) {
					throw std::runtime_error( "Session packet verification failed in pair request." );
				}

				struct tlv_map tlvmap;
				memset( &tlvmap, 0, sizeof( tlv_map ) );
				if ( TLV_SUCESS != TLV8.decode( (uint8_t*)decrypted, size, &tlvmap ) ) {
					throw std::runtime_error( "Invalid encryped data in pair request." );
				}

				// Check if all the required data is present in the decrypted tlv.
				tlv username = TLV8.getTLVByType( tlvmap, (uint8_t)TLVType::USERNAME );
				if ( username.type != (uint8_t)TLVType::USERNAME ) {
					throw std::runtime_error( "Missing username in pair request." );
				}
				tlv pubkey = TLV8.getTLVByType( tlvmap, (uint8_t)TLVType::PUBLIC_KEY );
				if ( pubkey.type != (uint8_t)TLVType::PUBLIC_KEY ) {
					throw std::runtime_error( "Missing public key in pair request." );
				}
				tlv proof = TLV8.getTLVByType( tlvmap, (uint8_t)TLVType::PROOF );
				if ( proof.type != (uint8_t)TLVType::PROOF ) {
					throw std::runtime_error( "Missing proof in pair request." );
				}

				uint8_t controllerHash[100];
				{
					const char salt[] = "Pair-Setup-Controller-Sign-Salt";
					const char info[] = "Pair-Setup-Controller-Sign-Info";
					if ( 0 != hkdf( (const unsigned char*)salt, strlen( salt ), (const unsigned char*)this->m_secretKey->data, this->m_secretKey->length, (const unsigned char*)info, strlen( info ), controllerHash, 32 ) ) {
						throw std::runtime_error( "Pairing encryption failure (1)." );
					}
				}

				bcopy( username.data, &controllerHash[32], 36 );
				bcopy( pubkey.data, &controllerHash[68], 32 );
				if ( 0 != ed25519_sign_open( (const unsigned char*)controllerHash, 100, pubkey.data, proof.data ) ) {
					throw std::runtime_error( "Pairing encryption failure (2)." );
				}

				uint8_t outputkey[150];
				{
					const char salt[] = "Pair-Setup-Accessory-Sign-Salt";
					const char info[] = "Pair-Setup-Accessory-Sign-Info";
					if ( 0 != hkdf( (const unsigned char*)salt, strlen( salt ), (const unsigned char*)this->m_secretKey->data, this->m_secretKey->length, (const unsigned char*)info, strlen( info ), outputkey, 32 ) ) {
						throw std::runtime_error( "Pairing encryption failure (3)." );
					}
				}

				ed25519_secret_key edSecret;
				bcopy( accessorySecretKey, edSecret, sizeof( edSecret ) );
				ed25519_public_key edPubKey;
				ed25519_publickey( edSecret, edPubKey );

				bcopy( this->_getAccessoryId().c_str(), &outputkey[32], 17 );
				bcopy( edPubKey, &outputkey[32 + 17], 32 );
				char signature[64];
				ed25519_sign( outputkey, 64 + 17, (const unsigned char*)edSecret, (const unsigned char*)edPubKey, (unsigned char *)signature );

				// Create inner output tlv data.
				struct tlv_map tlvmap_inner;
				memset( &tlvmap_inner, 0, sizeof( tlv_map ) );
				TLV8.insert( &tlvmap_inner, TLV8.TLVFromBuffer( (uint8_t*)this->_getAccessoryId().c_str(), (int8_t)TLVType::USERNAME, 17 ) );
				TLV8.insert( &tlvmap_inner, TLV8.TLVFromBuffer( (uint8_t*)edPubKey, (int8_t)TLVType::PUBLIC_KEY, 32 ) );
				TLV8.insert( &tlvmap_inner, TLV8.TLVFromBuffer( (uint8_t*)signature, (int8_t)TLVType::PROOF, 64 ) );
				uint8_t* output_inner;
				uint32_t length_inner;
				TLV8.encode( &tlvmap_inner, &output_inner, &length_inner );

				// Encode inner output tlv data.
				chacha20_ctx ctx;
				bzero( &ctx, sizeof( ctx ) );
				chacha20_setup( &ctx, (const uint8_t*)this->m_sessionKey, 32, (uint8_t*)"PS-Msg06" );
				char buffer[64];
				char key[64];
				bzero( buffer, 64 );
				chacha20_encrypt( &ctx, (const uint8_t*)buffer, (uint8_t*)key, 64 );
				// Make room for encrypted + verification (16 chars).
				char encrypted_out[length_inner + 16];
				bzero( encrypted_out, length_inner + 16 );
				chacha20_encrypt( &ctx, (const uint8_t*)output_inner, (uint8_t*)encrypted_out, length_inner );

				TLV8.TLVFree( &tlvmap_inner );
				free( output_inner );

				// We need to add the 16 characters of verification here.
				memset( verify, 0, 16 );
				poly1305_genkey( (const unsigned char*)key, (unsigned char*)encrypted_out, length_inner, Type_Data_Without_Length, verify );
				memcpy( (unsigned char*)&encrypted_out[length_inner], verify, 16 );

				// Create output tlv data.
				struct tlv_map tlvmap_out;
				memset( &tlvmap_out, 0, sizeof( tlv_map ) );
				uint8_t seqnum = 0x06;
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( &seqnum, (int8_t)TLVType::SEQUENCE_NUM, 1 ) );
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( (uint8_t*)encrypted_out, (int8_t)TLVType::ENCRYPTED_DATA, length_inner + 16 ) );
				uint8_t* output;
				uint32_t length;
				TLV8.encode( &tlvmap_out, &output, &length );

				session_.m_connection->reply( std::string( (char*)output, length ), 200, {
					{ "Content-Type", "application/pairing+tlv8" }
				} );

				TLV8.TLVFree( &tlvmap_out );
				free( output );

				// At this point the plugin has been paired with the controller, and it should remain that way.
				Logger::log( Logger::LogLevel::NOTICE, this, "Paired with controller." );
				this->m_settings->put( "paired", true );
				this->m_settings->commit();
			}

			TLV8.TLVFree( &tlvmap_in );

		} catch( std::runtime_error exception_ ) {
			Logger::logr( Logger::LogLevel::ERROR, this, exception_.what() );
		}
	};

	void HomeKit::_handlePairVerify( Session& session_ ) {
		std::string method = session_.m_connection->getMethod();
		Logger::logr( Logger::LogLevel::VERBOSE, this, "Pair verify %s request received.", method.c_str() );

		// A pair verify call can only be handled if the plugin is paired with a controller first.
		if ( ! this->m_settings->get( "paired", false ) ) {
			Logger::log( Logger::LogLevel::WARNING, this, "Pair verify called without being paired." );
			session_.m_connection->reply( "", 403 /* forbidden */, { } );
		}

		try {
			// First the tlv data is parsed.
			std::string body = session_.m_connection->getBody();
			TLV8Class TLV8 = TLV8Class();
			struct tlv_map tlvmap_in;
			memset( &tlvmap_in, 0, sizeof( tlv_map ) );
			if ( TLV_SUCESS != TLV8.decode( reinterpret_cast<uint8_t*>( &body[0] ), body.length(), &tlvmap_in ) ) {
				throw std::runtime_error( "Invalid pair request." );
			}

			// There should be at least a sequence number present in the data.
			tlv seqnum = TLV8.getTLVByType( tlvmap_in, (uint8_t)TLVType::SEQUENCE_NUM );
			if ( seqnum.type != (uint8_t)TLVType::SEQUENCE_NUM ) {
				throw std::runtime_error( "Missing sequence number in pair verification request." );
			}

			// Pair verification step 1.
			if ( seqnum.data[0] == 0x01 ) {

				Logger::log( Logger::LogLevel::VERBOSE, this, "Pair verification step 1/2." );

				// Check if all the required data is present in the decrypted tlv.
				tlv pubkey = TLV8.getTLVByType( tlvmap_in, (uint8_t)TLVType::PUBLIC_KEY );
				if ( pubkey.type != (uint8_t)TLVType::PUBLIC_KEY ) {
					throw std::runtime_error( "Missing client public key in pair verification request." );
				}

				curved25519_key secretKey;
				curved25519_key publicKey;
				curved25519_key controllerPublicKey;
				curved25519_key sharedKey;

				bcopy( pubkey.data, controllerPublicKey, 32 );
				for ( unsigned short i = 0; i < sizeof( secretKey ); i++ ) {
					secretKey[i] = rand();
				}

				curve25519_donna( (u8*)publicKey, (const u8*)secretKey, (const u8*)curveBasePoint );
				curve25519_donna( sharedKey, secretKey, controllerPublicKey );

				char temp[100];
				char proof[64];
				bcopy( publicKey, temp, 32 );
				bcopy( this->_getAccessoryId().c_str(), &temp[32], 17 );
				bcopy( controllerPublicKey, &temp[32 + 17], 32 );

				ed25519_secret_key edSecret;
				bcopy( accessorySecretKey, edSecret, sizeof( edSecret ) );
				ed25519_public_key edPubKey;
				ed25519_publickey( edSecret, edPubKey );
				ed25519_sign( (const unsigned char *)temp, 64 + 17, edSecret, edPubKey, (unsigned char*)proof );

				// Create inner output tlv data.
				struct tlv_map tlvmap_inner;
				memset( &tlvmap_inner, 0, sizeof( tlv_map ) );
				TLV8.insert( &tlvmap_inner, TLV8.TLVFromBuffer( (uint8_t*)this->_getAccessoryId().c_str(), (int8_t)TLVType::USERNAME, 17 ) );
				TLV8.insert( &tlvmap_inner, TLV8.TLVFromBuffer( (uint8_t*)proof, (int8_t)TLVType::PROOF, 64 ) );
				uint8_t* output_inner;
				uint32_t length_inner;
				TLV8.encode( &tlvmap_inner, &output_inner, &length_inner );

				{
					if (
						0 != hkdf( (uint8_t*)"Pair-Verify-Encrypt-Salt", 24, sharedKey, 32, (uint8_t*)"Pair-Verify-Encrypt-Info", 24, (uint8_t*)session_.m_sessionKey, 32 )
						|| 0 != hkdf( (uint8_t*)"Control-Salt", 12, sharedKey, 32, (uint8_t*)"Control-Read-Encryption-Key", 27, (uint8_t*)session_.m_accessoryToControllerKey, 32 )
						|| 0 != hkdf( (uint8_t*)"Control-Salt", 12, sharedKey, 32, (uint8_t*)"Control-Write-Encryption-Key", 28, (uint8_t*)session_.m_controllerToAccessoryKey, 32 )
					) {
						throw std::runtime_error( "Pair verification encryption failure." );
					}
				}

				// Encode inner output tlv data.
				chacha20_ctx ctx;
				bzero( &ctx, sizeof( ctx ) );
				chacha20_setup( &ctx, (const uint8_t*)session_.m_sessionKey, 32, (uint8_t*)"PV-Msg02" );
				char buffer[64];
				char key[64];
				bzero( buffer, 64 );
				chacha20_encrypt( &ctx, (const uint8_t*)buffer, (uint8_t*)key, 64 );
				// Make room for encrypted + verification (16 chars).
				char encrypted_out[length_inner + 16];
				bzero( encrypted_out, length_inner + 16 );
				chacha20_encrypt( &ctx, (const uint8_t*)output_inner, (uint8_t*)encrypted_out, length_inner );

				TLV8.TLVFree( &tlvmap_inner );
				free( output_inner );

				// We need to add the 16 characters of verification here.
				char verify[16];
				memset( verify, 0, 16 );
				poly1305_genkey( (const unsigned char*)key, (unsigned char*)encrypted_out, length_inner, Type_Data_Without_Length, verify );
				memcpy( (unsigned char*)&encrypted_out[length_inner], verify, 16 );

				// Create output tlv data.
				struct tlv_map tlvmap_out;
				memset( &tlvmap_out, 0, sizeof( tlv_map ) );
				uint8_t seqnum = 0x02;
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( &seqnum, (int8_t)TLVType::SEQUENCE_NUM, 1 ) );
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( (uint8_t*)encrypted_out, (int8_t)TLVType::ENCRYPTED_DATA, length_inner + 16 ) );
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( (uint8_t*)publicKey, (int8_t)TLVType::PUBLIC_KEY, 32 ) );
				uint8_t* output;
				uint32_t length;
				TLV8.encode( &tlvmap_out, &output, &length );

				session_.m_connection->reply( std::string( (char*)output, length ), 200, {
					{ "Content-Type", "application/pairing+tlv8" }
				} );

				TLV8.TLVFree( &tlvmap_out );
				free( output );

			// Pair verification step 2.
			} else if ( seqnum.data[0] == 0x03 ) {

				Logger::log( Logger::LogLevel::VERBOSE, this, "Pair verification 2/2." );

				// Check if all the required data is present in the decrypted tlv.
				tlv encrypted = TLV8.getTLVByType( tlvmap_in, (uint8_t)TLVType::ENCRYPTED_DATA );
				if ( encrypted.type != (uint8_t)TLVType::ENCRYPTED_DATA ) {
					throw std::runtime_error( "Missing encrypted data in pair verification request." );
				}

				chacha20_ctx chacha20;
				bzero( &chacha20, sizeof( chacha20 ) );
				chacha20_setup( &chacha20, (const uint8_t *)session_.m_sessionKey, 32, (uint8_t*)"PV-Msg03" );

				char temp[64];
				bzero( temp, 64 );
				char temp2[64];
				bzero( temp2, 64 );
				chacha20_encrypt( &chacha20, (const uint8_t*)temp, (uint8_t *)temp2, 64 );

				char verify[16];
				bzero( verify, 16 );
				poly1305_genkey( (const unsigned char*)temp2, (unsigned char*)encrypted.data, encrypted.size - 16, Type_Data_Without_Length, verify );

				if ( memcmp( (void*)verify, (void*)&encrypted.data[encrypted.size - 16], 16 ) != 0 ) {
					throw std::runtime_error( "Session packet verification failed in pair verify request." );
				}

				uint8_t size = encrypted.size - 16;
				char decrypted[size];
				chacha20_decrypt( &chacha20, encrypted.data, (uint8_t*)decrypted, size );

				// Create output tlv data.
				struct tlv_map tlvmap_out;
				memset( &tlvmap_out, 0, sizeof( tlv_map ) );
				uint8_t seqnum = 0x04;
				TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( &seqnum, (int8_t)TLVType::SEQUENCE_NUM, 1 ) );
				uint8_t* output;
				uint32_t length;
				TLV8.encode( &tlvmap_out, &output, &length );

				session_.m_connection->reply( std::string( (char*)output, length ), 200, {
					{ "Content-Type", "application/pairing+tlv8" }
				} );

				TLV8.TLVFree( &tlvmap_out );
				free( output );

				// After he pairing process is complete, the connection encrypts all data at the packet level. These
				// packets are not recognized as http packets by mongoose, so we're removing the http protocol handler
				// and process these packets manually.
				session_.m_connection->m_mg_conn->proto_handler = NULL;
				session_.m_connection->m_flags &= ~NETWORK_CONNECTION_FLAG_HTTP;
				session_.m_encrypted = true;
				this->setState( Plugin::State::READY );
			}

			TLV8.TLVFree( &tlvmap_in );

		} catch( std::out_of_range exception_ ) {
			Logger::log( Logger::LogLevel::ERROR, this, "Session is not valid." );
		} catch( std::runtime_error exception_ ) {
			Logger::logr( Logger::LogLevel::ERROR, this, exception_.what() );
		}
	};

	void HomeKit::_handlePairings( Session& session_ ) {
		std::string method = session_.m_connection->getMethod();
		Logger::logr( Logger::LogLevel::VERBOSE, this, "Pairings %s request received.", method.c_str() );

		try {
			// First the tlv data is parsed.
			std::string body = session_.m_connection->getBody();
			TLV8Class TLV8 = TLV8Class();
			struct tlv_map tlvmap_in;
			memset( &tlvmap_in, 0, sizeof( tlv_map ) );
			if ( TLV_SUCESS != TLV8.decode( reinterpret_cast<uint8_t*>( &body[0] ), body.length(), &tlvmap_in ) ) {
				throw std::runtime_error( "Invalid pairings request." );
			}

			// There should be at least a request type present in the data.
			tlv reqtype = TLV8.getTLVByType( tlvmap_in, (uint8_t)TLVType::REQUEST_TYPE );
			if ( reqtype.type != (uint8_t)TLVType::REQUEST_TYPE ) {
				throw std::runtime_error( "Missing request type in pairings request." );
			}

			if ( reqtype.data[0] == 3 ) {
				// Not implemented.
			} else if ( reqtype.data[0] == 4 ) {
				Logger::log( Logger::LogLevel::NORMAL, this, "Pairing removed." );
				this->m_settings->put( "paired", false );
				this->m_settings->commit();
				this->setState( Plugin::State::DISCONNECTED );

				std::lock_guard<std::mutex> sessionsLock( this->m_sessionsMutex );
				for ( auto& sessionIt : this->m_sessions ) {
					sessionIt.second.m_connection->close();
				}
			}

			// Create output tlv data.
			struct tlv_map tlvmap_out;
			memset( &tlvmap_out, 0, sizeof( tlv_map ) );
			uint8_t seqnum = 0x02;
			TLV8.insert( &tlvmap_out, TLV8.TLVFromBuffer( &seqnum, (int8_t)TLVType::SEQUENCE_NUM, 1 ) );
			uint8_t* output = NULL;
			uint32_t length = 0;
			TLV8.encode( &tlvmap_out, &output, &length );

			session_.send( "HTTP/1.1 200 OK", "Content-Type: application/pairing+tlv8", std::string( (char*)output, length ) );

			TLV8.TLVFree( &tlvmap_out );
			free( output );

			TLV8.TLVFree( &tlvmap_in );

		} catch( std::out_of_range exception_ ) {
			Logger::log( Logger::LogLevel::ERROR, this, "Session is not valid." );
		} catch( std::runtime_error exception_ ) {
			Logger::logr( Logger::LogLevel::ERROR, this, exception_.what() );
		}
	};

	void HomeKit::_handleAccessories( Session& session_ ) {
		std::string method = session_.m_connection->getMethod();
		Logger::logr( Logger::LogLevel::VERBOSE, this, "Accessoires %s request received.", method.c_str() );

		try {
			json accessories = json::array();
			accessories += {
				{ "aid", 1 }, // first accessory is the bridge and should use aid 1
				{ "services", {
					{
						{ "type", "3E" }, // accessory information service
						{ "iid", 1 },
						{ "characteristics", {
							{
								{ "type", "23" },
								{ "iid", HAPCharacteristic::NAME },
								{ "value", "Micasa" },
								{ "perms", { "pr" } },
								{ "format", "string" },
							},
							{
								{ "type", "20" },
								{ "iid", HAPCharacteristic::MANUFACTURER },
								{ "value", "Fellownet" },
								{ "perms", { "pr" } },
								{ "format", "string" },
							},
							{
								{ "type", "30" },
								{ "iid", HAPCharacteristic::SERIAL },
								{ "value", this->_getAccessoryId() },
								{ "perms", { "pr" } },
								{ "format", "string" },
							},
							{
								{ "type", "21" },
								{ "iid", HAPCharacteristic::MODEL },
								{ "value", "HomeKitPlugin1,1" },
								{ "perms", { "pr" } },
								{ "format", "string" },
							},
							{
								{ "type", "14" },
								{ "iid", HAPCharacteristic::IDENTIFY },
								{ "perms", { "pw" } },
								{ "format", "bool" },
							},
						} }
					}
				} }
			};

			for ( auto const& device : g_controller->getAllDevices() ) {
				if ( device->getSettings()->get( "enable_homekit_" + this->getReference(), false ) ) {
					try {
						std::string subtype = device->getSettings()->get( "subtype", device->getSettings()->get( DEVICE_SETTING_DEFAULT_SUBTYPE, "generic" ) );
						const HAPDefenition& defenition = HAPMappings.at( device->getType() ).at( subtype );

						// Prepare the services array for this accessory / device. The first service should always be
						// the information service, which is mandatory.
						json services = json::array();
						services += {
							{ "type", "3E" }, // accessory information service
							{ "iid", 1 },
							{ "characteristics", {
								{
									{ "type", "23" },
									{ "iid", HAPCharacteristic::NAME },
									{ "value", device->getName() },
									{ "perms", { "pr" } },
									{ "format", "string" },
								},
								{
									{ "type", "20" },
									{ "iid", HAPCharacteristic::MANUFACTURER },
									{ "value", "Fellownet" },
									{ "perms", { "pr" } },
									{ "format", "string" },
								},
								{
									{ "type", "30" },
									{ "iid", HAPCharacteristic::SERIAL },
									{ "value", this->_getAccessoryId() },
									{ "perms", { "pr" } },
									{ "format", "string" },
								},
								{
									{ "type", "21" },
									{ "iid", HAPCharacteristic::MODEL },
									{ "value", "MicasaDevice1,1" },
									{ "perms", { "pr" } },
									{ "format", "string" },
								},
								{
									{ "type", "14" },
									{ "iid", HAPCharacteristic::IDENTIFY },
									{ "perms", { "pw" } },
									{ "format", "bool" },
								},
							} }
						};

						// If the device is battery powered an additional battery service is added.
						if ( device->getSettings()->contains( DEVICE_SETTING_BATTERY_LEVEL ) ) {
							unsigned int level = device->getSettings()->get<unsigned int>( DEVICE_SETTING_BATTERY_LEVEL );
							level = level > 100 ? 100 : level;
							services += {
								{ "type", "96" }, // battery service
								{ "iid", 2 },
								{ "characteristics", {
									{
										{ "type", "68" },
										{ "iid", HAPCharacteristic::BATTERY_LEVEL },
										{ "value", level },
										{ "perms", { "pr" } },
										{ "format", "uint8" },
									},
									{
										{ "type", "8F" },
										{ "iid", HAPCharacteristic::BATTERY_STATE },
										{ "value", 2 }, // not chargeable
										{ "perms", { "pr" } },
										{ "format", "uint8" },
									},
									{
										{ "type", "79" },
										{ "iid", HAPCharacteristic::BATTERY_LOW },
										{ "value", level < LOW_BATTERY_THRESHOLD ? 1 : 0 },
										{ "perms", { "pr" } },
										{ "format", "uint8" },
									}
								} }
							};
						}

						unsigned short permission_bits = HAP_PERM_PR | HAP_PERM_EV;
						auto updateSources = device->getSettings()->get<Device::UpdateSource>( DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::UpdateSource::ANY );
						if ( Device::resolveUpdateSource( updateSources & Device::UpdateSource::USER ) != 0 ) {
							permission_bits |= HAP_PERM_PW;
						}
						permission_bits &= defenition.permissions;
						json permissions = json::array();
						if ( ( permission_bits & HAP_PERM_PR ) == HAP_PERM_PR ) { permissions += "pr"; };
						if ( ( permission_bits & HAP_PERM_PW ) == HAP_PERM_PW ) { permissions += "pw"; };
						if ( ( permission_bits & HAP_PERM_EV ) == HAP_PERM_EV ) { permissions += "ev"; };

						// The service iid should change when the type of the service changes. Hashing the concatenated
						// uuid's makes sure the iid is unique for the service. The first 1000 iid's are reserved.
						std::stringstream idstr;
						idstr << defenition.service_uuid << "|" << defenition.characteristic_uuid << "|";
						unsigned long service_iid;
						std::hash<std::string> hasher;
						do {
							service_iid = hasher( idstr.str() );
							idstr << "X";
						} while ( service_iid < 1000 );

						// Create the characteristics array for the primary service. It contains the name too, which makes
						// sure the name of the service gets updated on iOS devices when the device name is altered.
						json characteristics = json::array();
						characteristics += {
							{ "type", "23" },
							{ "iid", HAPCharacteristic::S_NAME },
							{ "value", device->getName() },
							{ "perms", { "pr" } },
							{ "format", "string" },
						};
						json characteristic = {
							{ "type", defenition.characteristic_uuid },
							{ "iid", defenition.iid },
							{ "perms", permissions },
							{ "format", defenition.format },
							{ "ev", ( permission_bits & HAP_PERM_EV ) == HAP_PERM_EV }
						};
						this->_addHAPValue( device, defenition.format, characteristic );
						characteristics += characteristic;

						// Services with additional required characteristics are added here.
						if ( subtype == Switch::resolveTextSubType( Switch::SubType::ALARM ) ) {
							// An alarm has a current- and target state. I suppose it's for enter- and leave delays, which
							// arent supported (yet) by micasa, so target state == current state. Target state should be
							// writable if device is configured to allow updates. Current state is never writable.
							if ( Device::resolveUpdateSource( updateSources & Device::UpdateSource::USER ) != 0 ) {
								permissions += "pw";
							}
							unsigned int state = 3;
							if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::HOME ) {
								state = 0;
							} else if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::AWAY ) {
								state = 1;
							}
							characteristics += {
								{ "type", "67" }, // target state is required besides current state
								{ "iid", defenition.iid - 1 }, // should be left a gap in HAPMappings
								{ "perms", permissions },
								{ "format", "uint8" },
								{ "ev", ( permission_bits & HAP_PERM_EV ) == HAP_PERM_EV },
								{ "value", state },
								{ "valid-values", { 0, 1, 3 } }
							};
						} else if ( subtype == Switch::resolveTextSubType( Switch::SubType::BLINDS ) ) {
							// Blinds have a current- and target position aswell as a current state characteristic.
							unsigned int state = 2; // stopped
							if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::OPEN ) {
								state = 1; // going to the maximum value
							} else if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::CLOSE ) {
								state = 0; // going to the minimum value
							}
							characteristics += {
								{ "type", "72" }, // position state is required
								{ "iid", defenition.iid - 2 }, // should be left a gap in HAPMappings
								{ "perms", permissions },
								{ "format", "uint8" },
								{ "ev", ( permission_bits & HAP_PERM_EV ) == HAP_PERM_EV },
								{ "value", state },
							};
							if ( Device::resolveUpdateSource( updateSources & Device::UpdateSource::USER ) != 0 ) {
								permissions += "pw";
							}
							state = 50;
							if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::OPEN ) {
								state = 100;
							} else if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::CLOSE ) {
								state = 0;
							}
							characteristics += {
								{ "type", "7C" }, // target position is required besides current position
								{ "iid", defenition.iid - 1 }, // should be left a gap in HAPMappings
								{ "perms", permissions },
								{ "format", "uint8" },
								{ "ev", ( permission_bits & HAP_PERM_EV ) == HAP_PERM_EV },
								{ "value", state },
							};
						} else if ( subtype == Level::resolveTextSubType( Level::SubType::DIMMER ) ) {
							characteristics += {
								{ "type", "25" }, // on/off is required besides brightness
								{ "iid", defenition.iid - 1 }, // should be left a gap in HAPMappings
								{ "perms", permissions },
								{ "format", "bool" },
								{ "ev", ( permission_bits & HAP_PERM_EV ) == HAP_PERM_EV },
								{ "value", ( std::static_pointer_cast<Level>( device )->getValue() > 0 ) }
							};
						}

						services += {
							{ "type", defenition.service_uuid },
							{ "iid", service_iid },
							{ "characteristics", characteristics },
							{ "primary", true }
						};

						accessories += {
							{ "aid", device->getId() + 1 },
							{ "services", services }
						};

					} catch( std::out_of_range exception_ ) {
						Logger::logr( Logger::LogLevel::ERROR, this, "Device %s is not supported.", device->getName().c_str() );
						device->getSettings()->remove( "enable_homekit_" + this->getReference() );
						device->getSettings()->commit();
					} catch( std::runtime_error exception_ ) {
						Logger::log( Logger::LogLevel::ERROR, this, exception_.what() );
					}
				}

				// HAP par 2.5.3.2 - A bridge must not expose more than 100 HAP accessory objects.
				if ( accessories.size() >= 100 ) {
					Logger::log( Logger::LogLevel::WARNING, this, "The HomeKit doesn't allow more than 100 accessories." );
					break;
				}
			}

			json output = {
				{ "accessories", accessories }
			};

			session_.send( "HTTP/1.1 200 OK", "Content-Type: application/hap+json", output.dump() );

		} catch( std::out_of_range exception_ ) {
			Logger::log( Logger::LogLevel::ERROR, this, "Session is not valid." );
		} catch( std::runtime_error exception_ ) {
			Logger::logr( Logger::LogLevel::ERROR, this, exception_.what() );
		}
	};

	void HomeKit::_handleCharacteristics( Session& session_ ) {
		std::string method = session_.m_connection->getMethod();
		Logger::logr( Logger::LogLevel::VERBOSE, this, "Characteristics %s request received.", method.c_str() );

		try {
			if ( method == "PUT" ) {

				json data = json::parse( session_.m_connection->getBody() );
				if ( data.find( "characteristics" ) == data.end() ) {
					throw std::runtime_error( "Missing characteristics in characteristics PUT request." );
				}
				for ( auto const& characteristic : data["characteristics"] ) {
					auto aid = jsonGet<unsigned long>( characteristic["aid"] );
					if ( aid == 1 ) {
						continue; // bridge
					}
					auto iid = jsonGet<unsigned long>( characteristic["iid"] );
					auto device = g_controller->getDeviceById( aid - 1 );
					if ( __likely( device != nullptr ) ) {
						if ( __likely( characteristic.find( "value" ) != characteristic.end() ) ) {
							try {
								std::string subtype = device->getSettings()->get( "subtype", device->getSettings()->get( DEVICE_SETTING_DEFAULT_SUBTYPE, "generic" ) );
								const HAPDefenition& defenition = HAPMappings.at( device->getType() ).at( subtype );
								if ( __likely( iid == defenition.iid ) ) {
									if ( device->getType() == Device::Type::SWITCH ) {
										auto value = jsonGet<bool>( characteristic, "value" );
										std::static_pointer_cast<Switch>( device )->updateValue( static_cast<Device::UpdateSource>( 0 ), value ? Switch::Option::ON : Switch::Option::OFF );
									} else if ( device->getType() == Device::Type::LEVEL ) {
										auto value = jsonGet<double>( characteristic, "value" );
										std::static_pointer_cast<Level>( device )->updateValue( static_cast<Device::UpdateSource>( 0 ), value );

										// HomeKit will send a brightness level *and* an on/off command for dimmers if
										// the button is pressed (as opposed to the slider).
										if ( subtype == Level::resolveTextSubType( Level::SubType::DIMMER ) ) {
											this->_releasePendingUpdate( device->getReference() + "_dimmer" );
											this->_queuePendingUpdate( device->getReference() + "_dimmer", 0, 1000 );
										}
									} else if ( device->getType() == Device::Type::COUNTER ) {
										auto value = jsonGet<double>( characteristic, "value" );
										std::static_pointer_cast<Level>( device )->updateValue( static_cast<Device::UpdateSource>( 0 ), value );
									}
								} else {
									switch( (HAPCharacteristic)iid ) {
										case HAPCharacteristic::IDENTIFY:
											// Not implemented yet.
											break;
										default: {
											// Services with additional required characteristics are added here.
											if (
												subtype == Switch::resolveTextSubType( Switch::SubType::ALARM )
												&& iid == (unsigned long)defenition.iid - 1
											) {
												auto value = jsonGet<unsigned int>( characteristic, "value" );
												if ( value == 0 ) {
													std::static_pointer_cast<Switch>( device )->updateValue( static_cast<Device::UpdateSource>( 0 ), Switch::Option::HOME );
												} else if ( value == 1 ) {
													std::static_pointer_cast<Switch>( device )->updateValue( static_cast<Device::UpdateSource>( 0 ), Switch::Option::AWAY );
												} else {
													std::static_pointer_cast<Switch>( device )->updateValue( static_cast<Device::UpdateSource>( 0 ), Switch::Option::OFF );
												}
											} else if (
												subtype == Switch::resolveTextSubType( Switch::SubType::BLINDS )
												&& iid == (unsigned long)defenition.iid - 1
											) {
												// HomeKit blinds can be set to a percentage whereas our switch blinds
												// only support open,  close and stop states.
												auto value = jsonGet<unsigned int>( characteristic, "value" );
												auto state = std::static_pointer_cast<Switch>( device )->getValueOption();
												bool busy = this->_releasePendingUpdate( device->getReference() + "_busy" );

												if ( value == 0 ) {
													// If the blinds are in our stopped state, or 50% for HomeKit, a
													// button press on iOS will always result in close. If it happens
													// shortly after a previous close, use open instead. This allows
													// for more precise positioning of the blinds.
													if ( busy ) {
														state = Switch::Option::STOP;
													} else {
														bool close = this->_releasePendingUpdate( device->getReference() + "_close" );
														if ( close ) {
															state = Switch::Option::OPEN;
														} else {
															state = Switch::Option::CLOSE;
															this->_queuePendingUpdate( device->getReference() + "_close", 0, 60000 );
														}
													}
												} else if ( value < 33 ) {
													state = Switch::Option::CLOSE;
												} else if ( value == 100 ) {
													if ( busy ) {
														state = Switch::Option::STOP;
													} else {
														state = Switch::Option::OPEN;
													}
												} else if ( value > 66 ) {
													state = Switch::Option::OPEN;
												} else {
													state = Switch::Option::STOP;
												}
												std::static_pointer_cast<Switch>( device )->updateValue( static_cast<Device::UpdateSource>( 0 ), state );
												if (
													state == Switch::Option::OPEN
													|| state == Switch::Option::CLOSE
												) {
													this->_queuePendingUpdate( device->getReference() + "_busy", 0, 60000 );
												}
											} else if (
												subtype == Level::resolveTextSubType( Level::SubType::DIMMER )
												&& iid == (unsigned long)defenition.iid - 1
											) {
												// This characteristic is set when the user presses a dimmer button, so
												// instead of setting the dimmer to a level, it is turned on or off.
												bool busy = this->_releasePendingUpdate( device->getReference() + "_dimmer" );
												if ( ! busy ) {
													auto value = jsonGet<bool>( characteristic, "value" );
													if ( value ) {
														std::static_pointer_cast<Level>( device )->updateValue( static_cast<Device::UpdateSource>( 0 ), 100 );
													} else {
														std::static_pointer_cast<Level>( device )->updateValue( static_cast<Device::UpdateSource>( 0 ), 0 );
													}
												}
											} else {
												Logger::logr( Logger::LogLevel::WARNING, this, "Unhandled characteristic iid %d requested.", iid );
												continue;
											}
										}
									}
								}
							} catch( std::out_of_range exception_ ) {
								Logger::logr( Logger::LogLevel::ERROR, this, "Device %s is not supported.", device->getName().c_str() );
								device->getSettings()->remove( "enable_homekit_" + this->getReference() );
								device->getSettings()->commit();
							}
						} else if ( characteristic.find( "ev" ) != characteristic.end() ) {
							if ( jsonGet<bool>( characteristic, "ev" ) ) {
								// Add the device to the vector of monitored devices if it's not already present.
								auto deviceIt = session_.m_devices.begin();
								for ( ; deviceIt != session_.m_devices.end(); deviceIt++ ) {
									if ( deviceIt->lock() == device ) {
										break;
									}
								}
								if ( deviceIt == session_.m_devices.end() ) {
									Logger::logr( Logger::LogLevel::VERBOSE, this, "Start monitoring %s.", device->getName().c_str() );
									session_.m_devices.push_back( device );
								}
							} else {
								// Remove the device from the vector of monitored devices, if it can be found.
								for ( auto deviceIt = session_.m_devices.begin(); deviceIt != session_.m_devices.end(); ) {
									if ( deviceIt->lock() == device ) {
										Logger::logr( Logger::LogLevel::VERBOSE, this, "Stop monitoring %s.", device->getName().c_str() );
										deviceIt = session_.m_devices.erase( deviceIt );
									} else {
										deviceIt++;
									}
								}
							}
						}
					} else {
						Logger::logr( Logger::LogLevel::ERROR, this, "Invalid accessory id %d in characteristics PUT request.", aid );
					}
				}

				session_.send( "HTTP/1.1 204 No Content", "Content-Type: application/hap+json", "" );

			} else if ( method == "GET" ) {

				// HAP par 5.7.3 - an id param should always be present and is a comma-separated list of accessory and
				// characteristic id pairs seperated by a dot.
				json characteristics = json::array();
				auto params = session_.m_connection->getParams();
				auto find = params.find( "id" );
				if ( find == params.end() ) {
					throw std::runtime_error( "Missing id parameter in characteristics GET request." );
				}
				auto idpairs = stringSplit( find->second, ',' );
				for ( auto const& idpair : idpairs ) {
					auto ids =  stringSplit( idpair, '.' );
					auto aid = std::stoul( ids[0] );
					if ( aid == 1 ) {
						continue; // bridge
					}
					auto iid = std::stoul( ids[1] );
					auto device = g_controller->getDeviceById( aid - 1 );
					if ( __likely( device != nullptr ) ) {
						try {
							std::string subtype = device->getSettings()->get( "subtype", device->getSettings()->get( DEVICE_SETTING_DEFAULT_SUBTYPE, "generic" ) );
							const HAPDefenition& defenition = HAPMappings.at( device->getType() ).at( subtype );

							json characteristic = {
								{ "aid", aid },
								{ "iid", iid }
							};
							if ( __likely( iid == defenition.iid ) ) {
								this->_addHAPValue( device, defenition.format, characteristic );
							} else {
								switch( (HAPCharacteristic)iid ) {
									case HAPCharacteristic::NAME:
									case HAPCharacteristic::S_NAME:
										characteristic["value"] = device->getName();
										break;
									case HAPCharacteristic::BATTERY_STATE:
										characteristic["value"] = 2; // not chargeable
										break;
									case HAPCharacteristic::BATTERY_LEVEL:
									case HAPCharacteristic::BATTERY_LOW: {
										unsigned int level = device->getSettings()->get<unsigned int>( DEVICE_SETTING_BATTERY_LEVEL, 100 );
										level = level > 100 ? 100 : level;
										if ( (HAPCharacteristic)iid == HAPCharacteristic::BATTERY_LEVEL ) {
											characteristic["value"] = level;
										} else {
											characteristic["value"] = ( level < LOW_BATTERY_THRESHOLD ? 1 : 0 );
										}
										break;
									}
									default: {
										// Services with additional required characteristics are added here.
										if (
											subtype == Switch::resolveTextSubType( Switch::SubType::ALARM )
											&& iid == (unsigned long)defenition.iid - 1
										) {
											unsigned int state = 3;
											if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::HOME ) {
												state = 0;
											} else if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::AWAY ) {
												state = 1;
											}
											characteristic["value"] = state;
										} else if (
											subtype == Switch::resolveTextSubType( Switch::SubType::BLINDS )
											&& iid == (unsigned long)defenition.iid - 1
										) {
											// HomeKit blinds are controlled using percentages, whereas ours uses open,
											// close and stop states. We're mimicing the HomeKit behaviour using 50%
											// as stopped state and 0 vs 100 for close vs open state.
											unsigned int state = 50;
											if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::OPEN ) {
												state = 100;
											} else if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::CLOSE ) {
												state = 0;
											}
											characteristic["value"] = state;
										} else if (
											subtype == Switch::resolveTextSubType( Switch::SubType::BLINDS )
											&& iid == (unsigned long)defenition.iid - 2
										) {
											unsigned int state = 2; // stopped
											if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::OPEN ) {
												state = 1; // going to the maximum value
											} else if ( std::static_pointer_cast<Switch>( device )->getValueOption() == Switch::Option::CLOSE ) {
												state = 0; // going to the minimum value
											}
											characteristic["value"] = state;
										} else if (
											subtype == Level::resolveTextSubType( Level::SubType::DIMMER )
											&& iid == (unsigned long)defenition.iid - 1
										) {
											characteristic["value"] = ( std::static_pointer_cast<Level>( device )->getValue() > 0 );
										} else {
											Logger::logr( Logger::LogLevel::WARNING, this, "Unhandled characteristic iid %d requested.", iid );
											continue;
										}
									}
								}
							}

							characteristics += characteristic;
						} catch( std::out_of_range exception_ ) {
							Logger::logr( Logger::LogLevel::ERROR, this, "Device %s is not supported.", device->getName().c_str() );
						} catch( std::runtime_error exception_ ) {
							Logger::log( Logger::LogLevel::ERROR, this, exception_.what() );
						}
					} else {
						Logger::logr( Logger::LogLevel::ERROR, this, "Invalid accessory id %d in characteristics GET request.", aid );
					}
				}

				json output = {
					{ "characteristics", characteristics }
				};

				session_.send( "HTTP/1.1 200 OK", "Content-Type: application/hap+json", output.dump() );
			}

		} catch( std::out_of_range exception_ ) {
			Logger::log( Logger::LogLevel::ERROR, this, "Session is not valid." );
		} catch( json::exception exception_ ) {
			Logger::log( Logger::LogLevel::ERROR, this, "Invalid json received." );
		} catch( std::runtime_error exception_ ) {
			Logger::logr( Logger::LogLevel::ERROR, this, exception_.what() );
		}
	};

	void HomeKit::_addHAPValue( std::shared_ptr<Device> device_, const std::string& format_, nlohmann::json& object_ ) {
		std::string subtype = device_->getSettings()->get( "subtype", device_->getSettings()->get( DEVICE_SETTING_DEFAULT_SUBTYPE, "generic" ) );
		if ( "bool" == format_ ) {
			if ( device_->getType() == Device::Type::SWITCH ) {
				object_["value"] = (
					std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::ON
					|| std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::ACTIVATE
					|| std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::ENABLED
					|| std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::OPEN
					|| std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::TRIGGERED
				);
			} else {
				throw std::runtime_error( "Device " + device_->getName() + " doesn't support bool values." );
			}
		} else if ( "uint8" == format_ ) {
			if ( device_->getType() == Device::Type::SWITCH ) {
				if ( subtype == Switch::resolveTextSubType( Switch::SubType::ALARM ) ) {
					unsigned int state = 3;
					if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::HOME ) {
						state = 0;
					} else if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::AWAY ) {
						state = 1;
					}
					object_["value"] = state;
				} else if ( subtype == Switch::resolveTextSubType( Switch::SubType::BLINDS ) ) {
					unsigned int state = 50;
					if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::OPEN ) {
						state = 100;
					} else if ( std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::CLOSE ) {
						state = 0;
					}
					object_["value"] = state;
				} else {
					object_["value"] = (
						std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::ON
						|| std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::ACTIVATE
						|| std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::ENABLED
						|| std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::OPEN
						|| std::static_pointer_cast<Switch>( device_ )->getValueOption() == Switch::Option::TRIGGERED
					) ? 1 : 0;
				}
			} else if ( device_->getType() == Device::Type::LEVEL ) {
				object_["value"] = (uint8_t)std::static_pointer_cast<Level>( device_ )->getValue();
			} else if ( device_->getType() == Device::Type::COUNTER ) {
				object_["value"] = (uint8_t)std::static_pointer_cast<Counter>( device_ )->getValue();
			} else {
				throw std::runtime_error( "Device " + device_->getName() + " doesn't support uint8 values." );
			}
		} else if ( "int" == format_ ) {
			if ( device_->getType() == Device::Type::LEVEL ) {
				object_["value"] = (int)std::static_pointer_cast<Level>( device_ )->getValue();
			} else if ( device_->getType() == Device::Type::COUNTER ) {
				object_["value"] = (int)std::static_pointer_cast<Counter>( device_ )->getValue();
			} else {
				throw std::runtime_error( "Device " + device_->getName() + " doesn't support uint8 values." );
			}
		} else if ( "float" == format_ ) {
			if ( device_->getType() == Device::Type::LEVEL ) {
				object_["value"] = std::static_pointer_cast<Level>( device_ )->getValue();
			} else if ( device_->getType() == Device::Type::COUNTER ) {
				object_["value"] = std::static_pointer_cast<Counter>( device_ )->getValue();
			} else {
				throw std::runtime_error( "Device " + device_->getName() + " doesn't support float values." );
			}
#ifdef _DEBUG
		} else {
			assert( false && "HAPMappings contains invalid format." );
#endif // _DEBUG
		}
	};

	std::string HomeKit::_getSetupCode() const {
		if ( ! this->m_settings->contains( { "setup_code" } ) ) {
			std::string code;
			bool valid = true;
			do {
				std::stringstream scode;
				bool dash = false;
				for ( auto const &count : { 3, 2, 3 } ) {
					if ( dash ) {
						scode << '-';
					}
					for ( int i = 1; i <= count; i++ ) {
						scode << randomNumber( 0, 9 );
					}
					dash = true;
				}
				code = scode.str();

				for ( auto const &invalid : {
					"000-00-000",
					"111-11-111",
					"222-22-222",
					"333-33-333",
					"444-44-444",
					"555-55-555",
					"666-66-666",
					"777-77-777",
					"888-88-888",
					"999-99-999",
					"123-45-678",
					"876-54-321"
				} ) {
					if ( code == invalid ) {
						valid = false;
						break;
					}
				}
			} while( ! valid );

			this->m_settings->put( "setup_code", code );
			this->m_settings->commit();
		}
		return this->m_settings->get( "setup_code" );
	};

	std::string HomeKit::_getAccessoryId() const {
		if ( ! this->m_settings->contains( { "accessory_id" } ) ) {
			std::stringstream sid;
			bool colon = false;
			for ( int i = 1; i <= 6; i++ ) {
				if ( colon ) {
					sid << ':';
				}
				sid << randomString( 2, "0123456789ABCDEF" );
				colon = true;
			}

			this->m_settings->put( "accessory_id", sid.str() );
			this->m_settings->commit();
		}
		return this->m_settings->get( "accessory_id" );
	};

	void HomeKit::Session::send( std::string protocol_, std::string type_, std::string data_ ) {
		// HAP par 5.5.2 - Each HTTP message is split into frames no larger than 1024 bytes. Each frame has the
		// following format:
		// <2:AAD for little endian length of encrypted data (n) in bytes>
		// <n:encrypted data according to AEAD algorithm, up to 1024 bytes>
		// <16:authTag according to AEAD algorithm>
		unsigned int length = data_.size();
		char headers[256];
		bzero( headers, 256 );
		snprintf( headers, 256, "%s\r\n%s\r\nContent-Length: %u\r\n\r\n", protocol_.c_str(), type_.c_str(), length );
		std::string output = headers + data_;

		while( output.size() > 0 ) {
			length = output.size();
			if ( length > 1024 ) {
				length = 1024;
			}

			char reply[length + 18];
			reply[0] = length % 256;
			reply[1] = ( length - (uint8_t)reply[0] ) / 256;

			chacha20_ctx chacha20;
			bzero( &chacha20, sizeof( chacha20 ) );
			chacha20_setup( &chacha20, this->m_accessoryToControllerKey, 32, (uint8_t*)&this->m_accessoryToControllerCount );
			this->m_accessoryToControllerCount++;

			char temp[64];
			bzero( temp, 64 );
			char temp2[64];
			bzero( temp2, 64 );
			char verify[16];
			bzero( verify, 16 );

			chacha20_encrypt( &chacha20, (const uint8_t*)temp, (uint8_t*)temp2, 64 );
			chacha20_encrypt( &chacha20, (const uint8_t*)output.c_str(), (uint8_t*)&reply[2], length );
			poly1305_genkey( (const unsigned char*)temp2, (uint8_t*)reply, length, Type_Data_With_Length, verify );

			memcpy( (unsigned char*)&reply[length + 2], verify, 16 );

			this->m_connection->send( std::string( reply, length + 18 ) );
			output.erase( 0, length );
		}
	};

}; // namespace micasa
