// https://www.wunderground.com/weather/api/d/docs?d=index

#include <sstream>

#include "WeatherUnderground.h"

#include "../device/Level.h"
#include "../device/Text.h"
#include "../device/Switch.h"
#include "../Logger.h"
#include "../Network.h"
#include "../Utils.h"

#include "json.hpp"

namespace micasa {

	extern std::shared_ptr<Logger> g_logger;

	using namespace nlohmann;

	void WeatherUnderground::start() {
		g_logger->log( Logger::LogLevel::VERBOSE, this, "Starting..." );
		Hardware::start();
	};
	
	void WeatherUnderground::stop() {
		g_logger->log( Logger::LogLevel::VERBOSE, this, "Stopping..." );
		Hardware::stop();
	};

	std::string WeatherUnderground::getLabel() const throw() {
		if ( this->m_details.size() ) {
			std::stringstream label;
			label << WeatherUnderground::label << " (" << this->m_details << ")";
			return label.str();
		} else {
			return WeatherUnderground::label;
		}
	};

	json WeatherUnderground::getJson( bool full_ ) const {
		json result = Hardware::getJson( full_ );
		result["api_key"] = this->m_settings->get( "api_key", "" );
		result["location"] = this->m_settings->get( "location", "" );
		result["scale"] = this->m_settings->get( "scale", "celsius" );
		if ( full_ ) {
			result["settings"] = this->getSettingsJson();
		}
		return result;
	};

	json WeatherUnderground::getSettingsJson() const {
		json result = Hardware::getSettingsJson();
		result += {
			{ "name", "api_key" },
			{ "label", "API Key" },
			{ "type", "string" },
			{ "class", this->m_settings->contains( "api_key" ) ? "advanced" : "normal" },
			{ "mandatory", true },
			{ "sort", 97 }
		};
		result += {
			{ "name", "location" },
			{ "label", "Location" },
			{ "type", "string" },
			{ "mandatory", true },
			{ "sort", 98 }
		};
		result += {
			{ "name", "scale" },
			{ "label", "Scale" },
			{ "type", "list" },
			{ "class", this->m_settings->contains( "scale" ) ? "advanced" : "normal" },
			{ "mandatory", true },
			{ "sort", 99 },
			{ "options", {
				{
					{ "value", "celsius" },
					{ "label", "Celsius" }
				},
				{
					{ "value", "fahrenheit" },
					{ "label", "Fahrenheid" }
				}
			} }
		};
		return result;
	};

	std::chrono::milliseconds WeatherUnderground::_work( const unsigned long int& iteration_ ) {
		
		if ( ! this->m_settings->contains( { "api_key", "location", "scale" } ) ) {
			g_logger->log( Logger::LogLevel::ERROR, this, "Missing settings." );
			this->setState( Hardware::State::FAILED );
			return std::chrono::milliseconds( 60 * 1000 );
		}

		std::stringstream url;
		url << "http://api.wunderground.com/api/" << this->m_settings->get( "api_key" ) << "/conditions/astronomy/q/" << this->m_settings->get( "location" ) << ".json";

		Network::get().connect( url.str(), Network::t_callback( [this]( mg_connection* connection_, int event_, void* data_ ) {
			if ( event_ == MG_EV_HTTP_REPLY ) {
				std::string body;
				body.assign( ((http_message*)data_)->body.p, ((http_message*)data_)->body.len );
				this->_processHttpReply( body );
				connection_->flags |= MG_F_CLOSE_IMMEDIATELY;
			}  else if (
				event_ == MG_EV_CLOSE
				&& this->getState() == Hardware::State::INIT
			) {
				g_logger->log( Logger::LogLevel::ERROR, this, "Connection failure." );
				this->setState( Hardware::State::FAILED );
			}
		} ) );
		
		return std::chrono::milliseconds( 1000 * 60 * 5 );
	};

	void WeatherUnderground::_processHttpReply( const std::string& body_ ) {
		try {
			json data = json::parse( body_ );

			if ( data["response"].is_object() ) {
				if ( data["response"]["error"].is_null() ) {
					if (
						! data["current_observation"].is_null()
						&& data["current_observation"].is_object()
					) {
						auto observation = data["current_observation"];

						if ( this->m_settings->get( "scale" ) == "fahrenheit" ) {
							this->declareDevice<Level>( "1", "Temperature in " + this->m_settings->get( "location" ), {
								{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::CONTROLLER ) },
								{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Level::resolveSubType( Level::SubType::TEMPERATURE ) },
								{ DEVICE_SETTING_DEFAULT_UNIT,           Level::resolveUnit( Level::Unit::FAHRENHEIT ) }
							} )->updateValue( Device::UpdateSource::HARDWARE, jsonGet<double>( observation, "temp_f" ) );
						} else if ( this->m_settings->get( "scale" ) == "celsius" ) {
							this->declareDevice<Level>( "2", "Temperature in " + this->m_settings->get( "location" ), {
								{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::CONTROLLER ) },
								{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Level::resolveSubType( Level::SubType::TEMPERATURE ) },
								{ DEVICE_SETTING_DEFAULT_UNIT,           Level::resolveUnit( Level::Unit::CELSIUS ) }
							} )->updateValue( Device::UpdateSource::HARDWARE, jsonGet<double>( observation, "temp_c" ) );
						}
						this->declareDevice<Level>( "3", "Humidity in " + this->m_settings->get( "location" ), {
							{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::CONTROLLER ) },
							{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Level::resolveSubType( Level::SubType::HUMIDITY ) },
							{ DEVICE_SETTING_DEFAULT_UNIT,           Level::resolveUnit( Level::Unit::PERCENT ) }
						} )->updateValue( Device::UpdateSource::HARDWARE, jsonGet<double>( observation, "relative_humidity" ) );
						this->declareDevice<Level>( "4", "Barometric pressure in " + this->m_settings->get( "location" ), {
							{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::CONTROLLER ) },
							{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Level::resolveSubType( Level::SubType::PRESSURE ) },
							{ DEVICE_SETTING_DEFAULT_UNIT,           Level::resolveUnit( Level::Unit::PASCAL ) }
						} )->updateValue( Device::UpdateSource::HARDWARE, jsonGet<double>( observation, "pressure_mb" ) );
						this->declareDevice<Text>( "5", "Wind Direction in " + this->m_settings->get( "location" ), {
							{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::CONTROLLER ) },
							{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Text::resolveSubType( Text::SubType::WIND_DIRECTION ) }
						} )->updateValue( Device::UpdateSource::HARDWARE, jsonGet<std::string>( observation, "wind_dir" ) );
					}

					if (
						! data["moon_phase"].is_null()
						&& data["moon_phase"].is_object()
					) {
						auto moonphase = data["moon_phase"];

						double now = jsonGet<unsigned int>( moonphase["current_time"], "hour" ) + ( jsonGet<unsigned int>( moonphase["current_time"], "minute" ) / 100.0f );
						double sunrise = jsonGet<unsigned int>( moonphase["sunrise"], "hour" ) + ( jsonGet<unsigned int>( moonphase["sunrise"], "minute" ) / 100.0f );
						double sunset = jsonGet<unsigned int>( moonphase["sunset"], "hour" ) + ( jsonGet<unsigned int>( moonphase["sunset"], "minute" ) / 100.0f );

						char buffer[50];
						int length = sprintf( buffer, "sunrise %5.2fh sunset %5.2fh", sunrise, sunset );
						this->m_details = this->m_settings->get( "location" ) + ", " + std::string( buffer, length );

						Switch::Option value;
						if (
							sunrise < now
							&& sunset > now
						) {
							value = Switch::Option::ON;
						} else {
							value = Switch::Option::OFF;
						}

						this->declareDevice<Switch>( "6", "Daytime in " + this->m_settings->get( "location" ), {
							{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::CONTROLLER ) }
						} )->updateValue( Device::UpdateSource::HARDWARE, value );
					}

					this->setState( Hardware::State::READY );

				} else {
					if (
						data["response"]["error"].is_object()
						&& ! data["response"]["error"]["description"].is_null()
					) {
						this->setState( Hardware::State::FAILED );
						g_logger->log( Logger::LogLevel::ERROR, this, data["response"]["error"]["description"].get<std::string>() );
					}
				}
			}
		} catch( ... ) {
			this->setState( Hardware::State::FAILED );
			g_logger->log( Logger::LogLevel::ERROR, this, "Invalid response." );
		}
	};
	
}; // namespace micasa
