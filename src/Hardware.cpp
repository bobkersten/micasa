#include <iostream>

#include "Hardware.h"
#include "Database.h"

#include "hardware/OpenZWave.h"
#include "hardware/OpenZWaveNode.h"
#include "hardware/WeatherUnderground.h"
#include "hardware/PiFace.h"
#include "hardware/PiFaceBoard.h"
#include "hardware/HarmonyHub.h"
#include "hardware/P1Meter.h"
#include "hardware/RFXCom.h"
#include "hardware/SolarEdge.h"
#include "hardware/SolarEdgeInverter.h"
 
namespace micasa {

	extern std::shared_ptr<Database> g_database;
	extern std::shared_ptr<WebServer> g_webServer;
	extern std::shared_ptr<Logger> g_logger;

	Hardware::Hardware( const unsigned int id_, const std::string reference_, const std::shared_ptr<Hardware> parent_, std::string label_ ) : Worker(), m_id( id_ ), m_reference( reference_ ), m_parent( parent_ ), m_label( label_ ) {
#ifdef _DEBUG
		assert( g_webServer && "Global WebServer instance should be created before Hardware instances." );
		assert( g_webServer && "Global Database instance should be created before Hardware instances." );
		assert( g_logger && "Global Logger instance should be created before Hardware instances." );
#endif // _DEBUG
		this->m_settings.populate( *this );
	};

	Hardware::~Hardware() {
#ifdef _DEBUG
		assert( g_webServer && "Global WebServer instance should be destroyed after Hardware instances." );
		assert( g_webServer && "Global Database instance should be destroyed after Hardware instances." );
		assert( g_logger && "Global Logger instance should be destroyed after Hardware instances." );
#endif // _DEBUG
	};

	std::shared_ptr<Hardware> Hardware::_factory( const Type type_, const unsigned int id_, const std::string reference_, const std::shared_ptr<Hardware> parent_, std::string label_ ) {
		switch( type_ ) {
			case HARMONY_HUB:
				return std::make_shared<HarmonyHub>( id_, reference_, parent_, label_ );
				break;
			case OPEN_ZWAVE:
				return std::make_shared<OpenZWave>( id_, reference_, parent_, label_ );
				break;
			case OPEN_ZWAVE_NODE:
				return std::make_shared<OpenZWaveNode>( id_, reference_, parent_, label_ );
				break;
			case P1_METER:
				return std::make_shared<P1Meter>( id_, reference_, parent_, label_ );
				break;
			case PIFACE:
				return std::make_shared<PiFace>( id_, reference_, parent_, label_ );
				break;
			case PIFACE_BOARD:
				return std::make_shared<PiFaceBoard>( id_, reference_, parent_, label_ );
				break;
			case RFXCOM:
				return std::make_shared<RFXCom>( id_, reference_, parent_, label_ );
				break;
			case SOLAREDGE:
				return std::make_shared<SolarEdge>( id_, reference_, parent_, label_ );
				break;
			case SOLAREDGE_INVERTER:
				return std::make_shared<SolarEdgeInverter>( id_, reference_, parent_, label_ );
				break;
			case WEATHER_UNDERGROUND:
				return std::make_shared<WeatherUnderground>( id_, reference_, parent_, label_ );
				break;
		}
#ifdef _DEBUG
		assert( true && "Hardware types should be defined in the Type enum." );
#endif // _DEBUG
		return nullptr;
	}

	void Hardware::start() {
		g_webServer->addResourceCallback( std::make_shared<WebServer::ResourceCallback>( WebServer::ResourceCallback( {
			"hardware-" + std::to_string( this->m_id ),
			"Returns a list of available hardware.",
			"api/hardware",
			WebServer::Method::GET,
			WebServer::t_callback( [this]( const std::string& uri_, const std::map<std::string, std::string>& input_, const WebServer::Method& method_, int& code_, nlohmann::json& output_ ) {
				if ( output_.is_null() ) {
					output_ = nlohmann::json::array();
				}
				output_ += this->getJson();
			} )
		} ) ) );
		g_webServer->addResourceCallback( std::make_shared<WebServer::ResourceCallback>( WebServer::ResourceCallback( {
			"hardware-" + std::to_string( this->m_id ),
			"Returns detailed information for " + this->m_label,
			"api/hardware/" + std::to_string( this->m_id ),
			WebServer::Method::GET,
			WebServer::t_callback( [this]( const std::string& uri_, const std::map<std::string, std::string>& input_, const WebServer::Method& method_, int& code_, nlohmann::json& output_ ) {
				output_ = this->getJson();
			} )
		} ) ) );
		
		std::lock_guard<std::mutex> lock( this->m_devicesMutex );

		std::vector<std::map<std::string, std::string> > devicesData = g_database->getQuery(
			"SELECT `id`, `reference`, `label`, `type` "
			"FROM `devices` "
			"WHERE `hardware_id`=%d"
			, this->m_id
		);
		for ( auto devicesIt = devicesData.begin(); devicesIt != devicesData.end(); devicesIt++ ) {
			Device::Type type = static_cast<Device::Type>( atoi( (*devicesIt)["type"].c_str() ) );
#ifdef _DEBUG
			assert( type >= 1 && type <= 4 && "Device types should be defined in the Type enum." );
#endif // _DEBUG
			std::shared_ptr<Device> device = Device::_factory( this->shared_from_this(), type, std::stoi( (*devicesIt)["id"] ), (*devicesIt)["reference"], (*devicesIt)["label"] );
			device->start();
			this->m_devices.push_back( device );
		}

		Worker::start();
		g_logger->log( Logger::LogLevel::NORMAL, this, "Started." );
	};
	
	void Hardware::stop() {
		g_webServer->removeResourceCallback( "hardware-" + std::to_string( this->m_id ) );
		
		{
			std::lock_guard<std::mutex> lock( this->m_devicesMutex );
			for( auto devicesIt = this->m_devices.begin(); devicesIt < this->m_devices.end(); devicesIt++ ) {
				(*devicesIt)->stop();
			}
			this->m_devices.clear();
		}

		if ( this->m_settings.isDirty() ) {
			this->m_settings.commit( *this );
		}
		Worker::stop();
		g_logger->log( Logger::LogLevel::NORMAL, this, "Stopped." );
	};

	const std::string Hardware::getName() const {
		return this->m_settings.get( "name", this->m_label );
	};
	
	void Hardware::setLabel( const std::string& label_ ) {
		if ( label_ != this->m_label ) {
			this->m_label = label_;
			g_database->putQuery(
				"UPDATE `hardware` "
				"SET `label`=%Q "
				"WHERE `id`=%d"
				, label_.c_str(), this->m_id
			);
		}
	};
	
	const nlohmann::json Hardware::getJson() const {
		nlohmann::json result = {
			{ "id", this->m_id },
			{ "label", this->getLabel() },
			{ "name", this->getName() },
		};
		if ( this->m_parent ) {
			result["parent"] = this->m_parent->getJson();
		}
		return result;
	};
	
	std::shared_ptr<Device> Hardware::_getDevice( const std::string reference_ ) const {
		std::lock_guard<std::mutex> lock( this->m_devicesMutex );
		for ( auto devicesIt = this->m_devices.begin(); devicesIt != this->m_devices.end(); devicesIt++ ) {
			if ( (*devicesIt)->getReference() == reference_ ) {
				return *devicesIt;
			}
		}
		return nullptr;
	};
	
	std::shared_ptr<Device> Hardware::_getDeviceById( const unsigned int id_ ) const {
		std::lock_guard<std::mutex> lock( this->m_devicesMutex );
		for ( auto devicesIt = this->m_devices.begin(); devicesIt != this->m_devices.end(); devicesIt++ ) {
			if ( (*devicesIt)->getId() == id_ ) {
				return *devicesIt;
			}
		}
		return nullptr;
	};
	
	std::shared_ptr<Device> Hardware::_declareDevice( const Device::Type type_, const std::string reference_, const std::string label_, const std::map<std::string, std::string> settings_ ) {
		// TODO also declare relationships with other devices, such as energy and power, or temperature
		// and humidity. Provide a hardcoded list of references upon declaring so that these relationships
		// can be altered at will by the client (maybe they want temperature and pressure).
		std::lock_guard<std::mutex> lock( this->m_devicesMutex );
		for ( auto devicesIt = this->m_devices.begin(); devicesIt != this->m_devices.end(); devicesIt++ ) {
			if ( (*devicesIt)->getReference() == reference_ ) {
				return *devicesIt;
			}
		}

		long id = g_database->putQuery(
			"INSERT INTO `devices` ( `hardware_id`, `reference`, `type`, `label` ) "
			"VALUES ( %d, %Q, %d, %Q )"
			, this->m_id, reference_.c_str(), static_cast<int>( type_ ), label_.c_str()
		);
		std::shared_ptr<Device> device = Device::_factory( this->shared_from_this(), type_, id, reference_, label_ );

		Settings& settings = device->getSettings();
		settings.insert( settings_ );
		settings.commit( *device );
		
		device->start();
		this->m_devices.push_back( device );

		return device;
	};
	
} // namespace micasa
