#include "Counter.h"

#include "../Database.h"
#include "../Hardware.h"
#include "../Controller.h"
#include "../User.h"

namespace micasa {

	extern std::shared_ptr<Database> g_database;
	extern std::shared_ptr<Logger> g_logger;
	extern std::shared_ptr<WebServer> g_webServer;
	extern std::shared_ptr<Controller> g_controller;

	const Device::Type Counter::type = Device::Type::COUNTER;

	const std::map<Counter::SubType, std::string> Counter::SubTypeText = {
		{ Counter::SubType::GENERIC, "generic" },
		{ Counter::SubType::ENERGY, "energy" },
		{ Counter::SubType::GAS, "gas" },
		{ Counter::SubType::WATER, "water" },
	};

	const std::map<Counter::Unit, std::string> Counter::UnitText = {
		{ Counter::Unit::GENERIC, "" },
		{ Counter::Unit::KILOWATTHOUR, "kWh" },
		{ Counter::Unit::M3, "M3" }
	};

	void Counter::start() {
		try {
			this->m_value = g_database->getQueryValue<int>(
			   "SELECT `value` "
			   "FROM `device_counter_history` "
			   "WHERE `device_id`=%d "
			   "ORDER BY `date` DESC "
			   "LIMIT 1"
			   , this->m_id
			);
		} catch( const Database::NoResultsException& ex_ ) { }

		g_webServer->addResourceCallback( {
			"device-" + std::to_string( this->m_id ),
			"^api/devices/" + std::to_string( this->m_id ) + "/data$",
			100,
			User::Rights::VIEWER,
			WebServer::Method::GET,
			WebServer::t_callback( [this]( std::shared_ptr<User> user_, const nlohmann::json& input_, const WebServer::Method& method_, nlohmann::json& output_ ) {
				// TODO check range to fetch
				output_["data"] = g_database->getQuery<json>(
					"SELECT * FROM ( "
						"SELECT printf(\"%%.3f\", `diff`) AS `value`, CAST(strftime('%%s',`date`) AS INTEGER) AS `timestamp` "
						"FROM `device_counter_trends` "
						"WHERE `device_id`=%d "
						"AND `date` < datetime('now','-%d day') "
						"ORDER BY `date` ASC "
					")"
					"UNION ALL "
					"SELECT * FROM ( "
						"SELECT printf(\"%%.3f\", max(`value`)-min(`value`)) AS `value`, strftime('%%s',`date`) - ( strftime('%%s',`date`) %% ( 60 * 60 ) ) AS `timestamp` "
						"FROM `device_counter_history` "
						"WHERE `device_id`=%d "
						"AND `date` >= datetime('now','-%d day') "
						"GROUP BY `timestamp` "
						"ORDER BY `date` ASC "
					") ",
					this->m_id, this->m_settings->get<int>( DEVICE_SETTING_KEEP_HISTORY_PERIOD, 7 ), this->m_id, this->m_settings->get<int>( DEVICE_SETTING_KEEP_HISTORY_PERIOD, 7 )
				);
				output_["code"] = 200;
			} )
		} );

		Device::start();
	};

	void Counter::stop() {
		Device::stop();
	};
	
	bool Counter::updateValue( const Device::UpdateSource& source_, const t_value& value_ ) {

		// The update source should be defined in settings by the declaring hardware.
		if ( ( this->m_settings->get<Device::UpdateSource>( DEVICE_SETTING_ALLOWED_UPDATE_SOURCES ) & source_ ) != source_ ) {
			g_logger->log( Logger::LogLevel::ERROR, this, "Invalid update source." );
			return false;
		}

		// Do not process duplicate values.
		if ( this->m_value == value_ ) {
			return true;
		}

		// Make a local backup of the original value (the hardware might want to revert it).
		this->m_previousValue = this->m_value;
		this->m_value = value_;
		
		// If the update originates from the hardware, do not send it to the hardware again!
		bool success = true;
		bool apply = true;
		if ( ( source_ & Device::UpdateSource::HARDWARE ) != Device::UpdateSource::HARDWARE ) {
			success = this->m_hardware->updateDevice( source_, this->shared_from_this(), apply );
		}
		if ( success && apply ) {
			g_database->putQuery(
				"INSERT INTO `device_counter_history` (`device_id`, `value`) "
				"VALUES (%d, %d)",
				this->m_id,
				value_
			);
			if ( this->isRunning() ) {
				g_controller->newEvent<Counter>( *this, source_ );
			}
			this->m_lastUpdate = std::chrono::system_clock::now(); // after newEvent so the interval can be determined
			g_logger->logr( Logger::LogLevel::NORMAL, this, "New value %d.", value_ );
		} else {
			this->m_value = this->m_previousValue;
		}
		return success;
	}
	
	json Counter::getJson( bool full_ ) const {
		json result = Device::getJson( full_ );
		result["value"] = this->getValue();
		result["type"] = "counter";
		result["subtype"] = this->m_settings->get( "subtype", this->m_settings->get( DEVICE_SETTING_DEFAULT_SUBTYPE, "" ) );
		result["unit"] = this->m_settings->get( "units", this->m_settings->get( DEVICE_SETTING_DEFAULT_UNITS, "" ) );
		
		if ( full_ ) {
			if ( this->m_settings->get<bool>( DEVICE_SETTING_ALLOW_SUBTYPE_CHANGE, false ) ) {
				json setting = {
					{ "name", "subtype" },
					{ "label", "SubType" },
					{ "type", "list" },
					{ "options", json::array() },
					{ "value", result["subtype"] }
				};
				for ( auto subTypeIt = Counter::SubTypeText.begin(); subTypeIt != Counter::SubTypeText.end(); subTypeIt++ ) {
					setting["options"] += {
						{ "value", subTypeIt->second },
						{ "label", subTypeIt->second }
					};
				}
				result["settings"] += setting;
			}
			if ( this->m_settings->get<bool>( DEVICE_SETTING_ALLOW_UNIT_CHANGE, false ) ) {
				json setting = {
					{ "name", "unit" },
					{ "label", "Unit" },
					{ "type", "list" },
					{ "options", json::array() },
					{ "value", result["unit"] }
				};
				for ( auto unitIt = Counter::UnitText.begin(); unitIt != Counter::UnitText.end(); unitIt++ ) {
					setting["options"] += {
						{ "value", unitIt->second },
						{ "label", unitIt->second }
					};
				}
				result["settings"] += setting;
			}
		}
		
		return result;
	};

	std::chrono::milliseconds Counter::_work( const unsigned long int& iteration_ ) {
		if ( iteration_ > 0 ) {
			std::string hourFormat = "%Y-%m-%d %H:00:00";
			std::string groupFormat = "%Y-%m-%d-%H";
			// TODO store value every 5 minutes regardless of updates?
			// TODO do not generate trends for anything older than an hour?? and what if micasa was offline for a while?
			// TODO maybe grab the latest hour from trends and start from there?
			// TODO for counter devices it's no use storing the latest value of the current hour in trends, only process
			// full (completed) hours.
			auto trends = g_database->getQuery(
				"SELECT MAX(`date`) AS `date1`, strftime(%Q, MAX(`date`)) AS `date2`, MAX(`value`)-MIN(`value`) AS `diff` "
				"FROM `device_counter_history` "
				"WHERE `device_id`=%d AND `Date` > datetime('now','-1 hour') "
				"GROUP BY strftime(%Q, `date`)"
				, hourFormat.c_str(), this->m_id, groupFormat.c_str()
			);
			for ( auto trendsIt = trends.begin(); trendsIt != trends.end(); trendsIt++ ) {
				auto value = g_database->getQueryValue<int>(
					"SELECT `value` "
					"FROM `device_counter_history` "
					"WHERE `device_id`=%d AND `date`=%Q"
					, this->m_id, (*trendsIt)["date1"].c_str()
				);
				g_database->putQuery(
					"REPLACE INTO `device_counter_trends` (`device_id`, `last`, `diff`, `date`) "
					"VALUES (%d, %d, %q, %Q)"
					, this->m_id, value, (*trendsIt)["diff"].c_str(), (*trendsIt)["date2"].c_str()
				);
			}

			// Purge history after a configured period (defaults to 7 days for counter devices because these have a
			// separate trends table).
			g_database->putQuery(
				"DELETE FROM `device_counter_history` "
				"WHERE `device_id`=%d AND `Date` < datetime('now','-%d day')"
				, this->m_id, this->m_settings->get<int>( DEVICE_SETTING_KEEP_HISTORY_PERIOD, 7 )
			);
			return std::chrono::milliseconds( 1000 * 60 * 5 );
		} else {
			// To prevent all devices from crunching data at the same time an offset is used.
			static volatile unsigned int offset = 0;
			offset += ( 1000 * 15 ); // 15 seconds interval
			return std::chrono::milliseconds( offset % ( 1000 * 60 * 5 ) );
		}
	}

}; // namespace micasa
