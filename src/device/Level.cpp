#include "Level.h"

#include "../Logger.h"
#include "../Database.h"
#include "../Hardware.h"
#include "../Controller.h"
#include "../User.h"

namespace micasa {

	extern std::shared_ptr<Database> g_database;
	extern std::shared_ptr<Logger> g_logger;
	extern std::shared_ptr<Controller> g_controller;

	using namespace nlohmann;
	using namespace std::chrono;

	const Device::Type Level::type = Device::Type::LEVEL;

	const std::map<Level::SubType, std::string> Level::SubTypeText = {
		{ Level::SubType::GENERIC, "generic" },
		{ Level::SubType::TEMPERATURE, "temperature" },
		{ Level::SubType::HUMIDITY, "humidity" },
		{ Level::SubType::POWER, "power" },
		{ Level::SubType::ELECTRICITY, "electricity" },
		{ Level::SubType::CURRENT, "current" },
		{ Level::SubType::PRESSURE, "pressure" },
		{ Level::SubType::LUMINANCE, "luminance" },
		{ Level::SubType::THERMOSTAT_SETPOINT, "thermostat_setpoint" },
		{ Level::SubType::BATTERY_LEVEL, "battery_level" },
		{ Level::SubType::DIMMER, "dimmer" },
	};

	const std::map<Level::Unit, std::string> Level::UnitText = {
		{ Level::Unit::GENERIC, "" },
		{ Level::Unit::PERCENT, "%" },
		{ Level::Unit::WATT, "Watt" },
		{ Level::Unit::VOLT, "V" },
		{ Level::Unit::AMPERES, "A" },
		{ Level::Unit::CELSIUS, "°C" },
		{ Level::Unit::FAHRENHEIT, "°F" },
		{ Level::Unit::PASCAL, "Pa" },
		{ Level::Unit::LUX, "lx" },
	};
	
	void Level::start() {
		try {
			this->m_value = g_database->getQueryValue<double>(
				"SELECT `value` "
				"FROM `device_level_history` "
				"WHERE `device_id`=%d "
				"ORDER BY `date` DESC "
				"LIMIT 1"
				, this->m_id
			);
		} catch( const Database::NoResultsException& ex_ ) { }

		Device::start();
	};
	
	void Level::stop() {
		Device::stop();
	};

	void Level::updateValue( const Device::UpdateSource& source_, const t_value& value_ ) {

		// The update source should be defined in settings by the declaring hardware.
		if ( ( this->m_settings->get<Device::UpdateSource>( DEVICE_SETTING_ALLOWED_UPDATE_SOURCES ) & source_ ) != source_ ) {
			auto configured = Device::resolveUpdateSource( this->m_settings->get<Device::UpdateSource>( DEVICE_SETTING_ALLOWED_UPDATE_SOURCES ) );
			auto requested = Device::resolveUpdateSource( source_ );
			g_logger->logr( Logger::LogLevel::ERROR, this, "Invalid update source (%d vs %d).", configured, requested );
			return;
		}

		if (
			this->getSettings()->get<bool>( "ignore_duplicates", this->getType() == Device::Type::SWITCH || this->getType() == Device::Type::TEXT )
			&& this->m_value == value_
		) {
			g_logger->log( Logger::LogLevel::VERBOSE, this, "Ignoring duplicate value." );
			return;
		}

		// Although we're storing values as reported by the hardware, they need to be converted to the displayed value
		// before being checked against minimum and maximum values.
		double divider = this->m_settings->get<double>( "divider", 1 );
		double offset = this->m_settings->get<double>( "offset", 0 );
		if (
			(
				this->m_settings->contains( "minimum" )
				&& ( ( value_ + offset ) / divider ) < this->m_settings->get<double>( "minimum" )
			)
			|| (
				this->m_settings->contains( "maximum" )
				&& ( ( value_ + offset ) / divider ) > this->m_settings->get<double>( "maximum" )
			)
		) {
			g_logger->log( Logger::LogLevel::ERROR, this, "Invalid value." );
			return;
		}
	
		// Make a local backup of the original value (the hardware might want to revert it).
		t_value previous = this->m_value;
		this->m_value = value_;
			
		// If the update originates from the hardware, do not send it to the hardware again.
		bool success = true;
		bool apply = true;
		if ( ( source_ & Device::UpdateSource::HARDWARE ) != Device::UpdateSource::HARDWARE ) {
			success = this->m_hardware->updateDevice( source_, this->shared_from_this(), apply );
		}

		if ( success && apply ) {
			g_database->putQuery(
				"INSERT INTO `device_level_history` (`device_id`, `value`) "
				"VALUES (%d, %.3f)",
				this->m_id,
				value_
			);
			this->m_previousValue = previous; // before newEvent so previous value can be provided
			if ( this->isRunning() ) {
				g_controller->newEvent<Level>( std::static_pointer_cast<Level>( this->shared_from_this() ), source_ );
			}
			g_logger->logr( Logger::LogLevel::NORMAL, this, "New value %.3f.", value_ );
		} else {
			this->m_value = previous;
		}
		if (
			success
			&& ( source_ & Device::UpdateSource::INIT ) != Device::UpdateSource::INIT
		) {
			this->touch();
		}
	};

	json Level::getJson( bool full_ ) const {
		double divider = this->m_settings->get<double>( "divider", 1 );
		double offset = this->m_settings->get<double>( "offset", 0 );

		json result = Device::getJson( full_ );
		//std::stringstream ss;
		//ss << std::fixed << std::setprecision( 3 ) << this->getValue();
		//result["value"] = ss.str();
		result["value"] = round( ( ( this->m_value / divider ) + offset ) * 1000.0f ) / 1000.0f;
		result["type"] = "level";
		result["subtype"] = this->m_settings->get( "subtype", this->m_settings->get( DEVICE_SETTING_DEFAULT_SUBTYPE, "" ) );
		result["unit"] = this->m_settings->get( "unit", this->m_settings->get( DEVICE_SETTING_DEFAULT_UNIT, "" ) );

		if ( this->m_settings->contains( "minimum" ) ) {
			result["minimum"] = this->m_settings->get<double>( "minimum" );
		}
		if ( this->m_settings->contains( "maximum" ) ) {
			result["maximum"] = this->m_settings->get<double>( "maximum" );
		}
		if ( this->m_settings->contains( "divider" ) ) {
			result["divider"] = this->m_settings->get<double>( "divider" );
		}
		if ( this->m_settings->contains( "offset" ) ) {
			result["offset"] = this->m_settings->get<double>( "offset" );
		}

		if ( full_ ) {
			result["settings"] = this->getSettingsJson();
		}
		return result;
	};

	json Level::getSettingsJson() const {
		json result = Device::getSettingsJson();
		if ( this->m_settings->get<bool>( DEVICE_SETTING_ALLOW_SUBTYPE_CHANGE, false ) ) {
			json setting = {
				{ "name", "subtype" },
				{ "label", "SubType" },
				{ "type", "list" },
				{ "options", json::array() },
				{ "class", this->m_settings->contains( "subtype" ) ? "advanced" : "normal" },
				{ "sort", 10 }
			};
			for ( auto subTypeIt = Level::SubTypeText.begin(); subTypeIt != Level::SubTypeText.end(); subTypeIt++ ) {
				setting["options"] += {
					{ "value", subTypeIt->second },
					{ "label", subTypeIt->second }
				};
			}
			result += setting;
		}
		if ( this->m_settings->get<bool>( DEVICE_SETTING_ALLOW_UNIT_CHANGE, false ) ) {
			json setting = {
				{ "name", "unit" },
				{ "label", "Unit" },
				{ "type", "list" },
				{ "options", json::array() },
				{ "class", this->m_settings->contains( "unit" ) ? "advanced" : "normal" },
				{ "sort", 11 }
			};
			for ( auto unitIt = Level::UnitText.begin(); unitIt != Level::UnitText.end(); unitIt++ ) {
				setting["options"] += {
					{ "value", unitIt->second },
					{ "label", unitIt->second }
				};
			}
			result += setting;
		}

		result += {
			{ "name", "minimum" },
			{ "label", "Minimum" },
			{ "type", "double" },
			{ "class", "advanced" },
			{ "sort", 996 }
		};

		result += {
			{ "name", "maximum" },
			{ "label", "Maximum" },
			{ "type", "double" },
			{ "class", "advanced" },
			{ "sort", 997 }
		};

		result += {
			{ "name", "divider" },
			{ "label", "Divider" },
			{ "description", "A divider to convert the value to the designated unit." },
			{ "type", "double" },
			{ "class", "advanced" },
			{ "sort", 998 }
		};

		result += {
			{ "name", "offset" },
			{ "label", "Offset" },
			{ "description", "A positive or negative value that is added to- or subtracted from the value in it's designated unit (after the optional divider has been applied)." },
			{ "type", "double" },
			{ "class", "advanced" },
			{ "sort", 999 }
		};

		return result;
	};

	json Level::getData( unsigned int range_, const std::string& interval_, const std::string& group_ ) const {
		std::vector<std::string> validIntervals = { "day", "week", "month", "year" };
		if ( std::find( validIntervals.begin(), validIntervals.end(), interval_ ) == validIntervals.end() ) {
			return json::array();
		}
		std::string interval = interval_;
		if ( interval == "week" ) {
			interval = "day";
			range_ *= 7;
		}

		std::vector<std::string> validGroups = { "none", "hour", "day", "month", "year" };
		if ( std::find( validGroups.begin(), validGroups.end(), group_ ) == validGroups.end() ) {
			return json::array();
		}

		double divider = this->m_settings->get<double>( "divider", 1 );
		double offset = this->m_settings->get<double>( "offset", 0 );

		if ( group_ == "none" ) {
			return g_database->getQuery<json>(
				"SELECT printf(\"%%.3f\", ( `value` / %.6f ) + %.6f ) AS `value`, CAST( strftime('%%s',`date`) AS INTEGER ) AS `timestamp`, `date` "
				"FROM `device_level_history` "
				"WHERE `device_id`=%d "
				"AND `date` >= datetime('now','-%d %s') "
				"ORDER BY `date` ASC ",
				divider,
				offset,
				this->m_id,
				range_,
				interval.c_str()
			);
		} else {
			std::string dateFormat = "%Y-%m-%d %H:30:00";
			std::string groupFormat = "%Y-%m-%d-%H";
			if ( group_ == "day" ) {
				dateFormat = "%Y-%m-%d 12:00:00";
				groupFormat = "%Y-%m-%d";
			} else if ( group_ == "month" ) {
				dateFormat = "%Y-%m-15 12:00:00";
				groupFormat = "%Y-%m";
			} else if ( group_ == "year" ) {
				dateFormat = "%Y-06-15 12:00:00";
				groupFormat = "%Y";
			}
			return g_database->getQuery<json>(
				"SELECT printf(\"%%.3f\", ( avg(`average`) + %.6f ) /  %.6f ) AS `value`, CAST( strftime( '%%s', strftime( %Q, MAX(`date`) ) ) AS INTEGER ) AS `timestamp`, strftime( %Q, MAX(`date`) ) AS `date` "
				"FROM `device_level_trends` "
				"WHERE `device_id`=%d "
				"AND `date` >= datetime('now','-%d %s') "
				"GROUP BY strftime(%Q, `date`) "
				"ORDER BY `date` ASC ",
				offset,
				divider,
				dateFormat.c_str(),
				dateFormat.c_str(),
				this->m_id,
				range_,
				interval.c_str(),
				groupFormat.c_str()
			);
		}
	};

	std::chrono::milliseconds Level::_work( const unsigned long int& iteration_ ) {
		if ( iteration_ > 0 ) {

			std::string hourFormat = "%Y-%m-%d %H:30:00";
			std::string groupFormat = "%Y-%m-%d-%H";

			auto trends = g_database->getQuery(
				"SELECT MAX(`value`) AS `max`, MIN(`value`) AS `min`, printf(\"%%.3f\", AVG(`value`)) AS `average`, strftime(%Q, MAX(`date`)) AS `date` "
				"FROM `device_level_history` "
				"WHERE `device_id`=%d AND `Date` > datetime('now','-4 hour') "
				"GROUP BY strftime(%Q, `date`)",
				hourFormat.c_str(),
				this->m_id,
				groupFormat.c_str()
			);
			for ( auto trendsIt = trends.begin(); trendsIt != trends.end(); trendsIt++ ) {
				g_database->putQuery(
					"REPLACE INTO `device_level_trends` (`device_id`, `min`, `max`, `average`, `date`) "
					"VALUES (%d, %q, %q, %q, %Q)",
					this->m_id,
					(*trendsIt)["min"].c_str(),
					(*trendsIt)["max"].c_str(),
					(*trendsIt)["average"].c_str(),
					(*trendsIt)["date"].c_str()
				);
			}

			// Purge history after a configured period (defaults to 7 days for level devices because these have a
			// separate trends table).
			g_database->putQuery(
				"DELETE FROM `device_level_history` "
				"WHERE `device_id`=%d AND `Date` < datetime('now','-%d day')",
				this->m_id,
				this->m_settings->get<int>( DEVICE_SETTING_KEEP_HISTORY_PERIOD, 7 )
			);
			return std::chrono::milliseconds( 1000 * 60 * 5 );

		} else {

			// To prevent all devices from crunching data at the same time an offset is used.
			static volatile unsigned int offset = 0;
			offset += ( 1000 * 10 ); // 10 seconds interval
			return std::chrono::milliseconds( offset % ( 1000 * 60 * 5 ) );
		}
	};

}; // namespace micasa
