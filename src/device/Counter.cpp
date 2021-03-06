#include <atomic>
#include <algorithm>

#include "Counter.h"

#include "../Logger.h"
#include "../Database.h"
#include "../Plugin.h"
#include "../Controller.h"
#include "../User.h"
#include "../Utils.h"

#define DEVICE_COUNTER_DEFAULT_HISTORY_RETENTION 7 // days
#define DEVICE_COUNTER_DEFAULT_TRENDS_RETENTION 60 // months

namespace micasa {

	extern std::unique_ptr<Database> g_database;
	extern std::unique_ptr<Controller> g_controller;

	using namespace std::chrono;
	using namespace nlohmann;

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

	const std::map<Counter::Unit, std::string> Counter::UnitFormat = {
		{ Counter::Unit::GENERIC, "%.3f" },
		{ Counter::Unit::KILOWATTHOUR, "%.3f" },
		{ Counter::Unit::M3, "%.3f" }
	};

	const std::map<Counter::SubType, std::vector<Counter::Unit>> Counter::SubTypeUnits = {
		{ Counter::SubType::ENERGY, { Counter::Unit::KILOWATTHOUR } },
		{ Counter::SubType::GAS, { Counter::Unit::M3 } },
		{ Counter::SubType::WATER, { Counter::Unit::M3 } },
	};

	Counter::Counter( std::weak_ptr<Plugin> plugin_, const unsigned int id_, const std::string reference_, std::string label_, bool enabled_ ) :
		Device( plugin_, id_, reference_, label_, enabled_ ),
		m_value( 0 ),
		m_updated( system_clock::now() ),
		m_rateLimiter( { 0, Device::resolveUpdateSource( 0 ) } )
	{
		try {
			json result = g_database->getQueryRow<json>(
				"SELECT `value`, CAST( strftime( '%%s', 'now' ) AS INTEGER ) - CAST( strftime( '%%s', `date` ) AS INTEGER ) AS `age` "
				"FROM `device_counter_history` "
				"WHERE `device_id` = %d "
				"ORDER BY `date` DESC "
				"LIMIT 1",
				this->m_id
			);
			this->m_value = this->m_rateLimiter.value = jsonGet<double>( result, "value" );
			this->m_updated = system_clock::now() - seconds( jsonGet<unsigned int>( result, "age" ) );
		} catch( const Database::NoResultsException& ex_ ) {
			Logger::log( Logger::LogLevel::DEBUG, this, "No starting value." );
		}
	};

	void Counter::start() {
#ifdef _DEBUG
		assert( this->m_enabled && "Device needs to be enabled while being started." );
#endif // _DEBUG
		this->m_scheduler.schedule( randomNumber( 0, SCHEDULER_INTERVAL_5MIN ), SCHEDULER_INTERVAL_5MIN, SCHEDULER_REPEAT_INFINITE, this, [this]( std::shared_ptr<Scheduler::Task<>> ) {
			this->_processTrends();
		} );
		this->m_scheduler.schedule( randomNumber( 0, SCHEDULER_INTERVAL_1HOUR ), SCHEDULER_INTERVAL_1HOUR, SCHEDULER_REPEAT_INFINITE, this, [this]( std::shared_ptr<Scheduler::Task<>> ) {
			this->_purgeHistoryAndTrends();
		} );
	};

	void Counter::stop() {
#ifdef _DEBUG
		assert( this->m_enabled && "Device needs to be enabled while being stopped." );
#endif // _DEBUG
		this->m_scheduler.erase( [this]( const Scheduler::BaseTask& task_ ) {
			return task_.data == this;
		} );
	};

	void Counter::updateValue( Device::UpdateSource source_, t_value value_ ) {
		if (
			! this->m_enabled
			&& ( source_ & Device::UpdateSource::PLUGIN ) != Device::UpdateSource::PLUGIN
		) {
			return;
		}

		if ( ( this->m_settings->get<Device::UpdateSource>( DEVICE_SETTING_ALLOWED_UPDATE_SOURCES ) & source_ ) != source_ ) {
			Logger::log( Logger::LogLevel::ERROR, this, "Invalid update source." );
			return;
		}

		if (
			this->getSettings()->get<bool>( "ignore_duplicates", false )
			&& this->m_value == value_
			&& this->getPlugin()->getState() >= Plugin::State::READY
		) {
			Logger::log( Logger::LogLevel::VERBOSE, this, "Ignoring duplicate value." );
			return;
		}

		if (
			this->m_settings->contains( "rate_limit" )
			&& this->getPlugin()->getState() >= Plugin::State::READY
		) {
			unsigned long rateLimit = 1000 * this->m_settings->get<double>( "rate_limit" );
			system_clock::time_point now = system_clock::now();
			system_clock::time_point next = this->m_updated + milliseconds( rateLimit );
			if ( next > now ) {
				this->m_rateLimiter.source = source_;
				this->m_rateLimiter.value = value_;
				auto task = this->m_rateLimiter.task.lock();
				if ( ! task ) {
					this->m_rateLimiter.task = this->m_scheduler.schedule( next, 0, 1, this, [this]( std::shared_ptr<Scheduler::Task<>> ) {
						this->_processValue( this->m_rateLimiter.source, this->m_rateLimiter.value );
					} );
				}
			} else {
				this->_processValue( source_, value_ );
			}
		} else {
			this->_processValue( source_, value_ );
		}
	};

	void Counter::incrementValue( Device::UpdateSource source_, t_value value_ ) {
		this->updateValue( source_, std::max( this->m_value, this->m_rateLimiter.value ) + value_ );
	};

	json Counter::getJson() const {
		json result = Device::getJson();

		double divider = this->m_settings->get<double>( "divider", 1 );
		std::string unit = this->m_settings->get( "unit", this->m_settings->get( DEVICE_SETTING_DEFAULT_UNIT, "" ) );

		result["value"] = std::stod( stringFormat( Counter::UnitFormat.at( Counter::resolveTextUnit( unit ) ), this->m_value / divider ) );
		result["raw_value"] = this->m_value;
		result["source"] = Device::resolveUpdateSource( this->m_source );
		result["age"] = duration_cast<seconds>( system_clock::now() - this->m_updated ).count();
		result["type"] = "counter";
		result["subtype"] = this->m_settings->get( "subtype", this->m_settings->get( DEVICE_SETTING_DEFAULT_SUBTYPE, "generic" ) );
		result["unit"] = unit;
		result["history_retention"] = this->m_settings->get<int>( "history_retention", DEVICE_COUNTER_DEFAULT_HISTORY_RETENTION );
		result["trends_retention"] = this->m_settings->get<int>( "trends_retention", DEVICE_COUNTER_DEFAULT_TRENDS_RETENTION );
		if ( this->m_settings->contains( "divider" ) ) {
			result["divider"] = this->m_settings->get<double>( "divider" );
		}
		if ( this->m_settings->contains( "rate_limit" ) ) {
			result["rate_limit"] = this->m_settings->get<double>( "rate_limit" );
		}

		for ( auto const& plugin : g_controller->getAllPlugins() ) {
			plugin->updateDeviceJson( Device::shared_from_this(), result, plugin == this->getPlugin() );
		}

		return result;
	};

	json Counter::getSettingsJson() const {
		json result = Device::getSettingsJson();
		if ( this->m_settings->get<bool>( DEVICE_SETTING_ALLOW_SUBTYPE_CHANGE, false ) ) {
			std::string sclass = this->m_settings->contains( "subtype" ) ? "advanced" : "normal";
			json setting = {
				{ "name", "subtype" },
				{ "label", "SubType" },
				{ "mandatory", true },
				{ "type", "list" },
				{ "options", json::array() },
				{ "class", sclass },
				{ "sort", 10 }
			};
			for ( auto subTypeIt = Counter::SubTypeText.begin(); subTypeIt != Counter::SubTypeText.end(); subTypeIt++ ) {
				json option = json::object();
				option["value"] = subTypeIt->second;
				option["label"] = subTypeIt->second;

				// Add unit setting if the units for this device can be changed and more than one are defined. If no
				// specific units have been defined, such as the generic subtype, all units can be selected.
				auto find = Counter::SubTypeUnits.find( subTypeIt->first );
				if (
					this->m_settings->get<bool>( DEVICE_SETTING_ALLOW_UNIT_CHANGE, false )
					&& (
						find == Counter::SubTypeUnits.end() // no units specified; add all units
						|| find->second.size() > 1 // more than one unit specified
					)
				) {
					option["settings"] = json::array();
					json setting = {
						{ "name", "unit" },
						{ "label", "Unit" },
						{ "type", "list" },
						{ "mandatory", true },
						{ "options", json::array() },
						{ "class", sclass },
						{ "sort", 11 }
					};
					if ( find != Counter::SubTypeUnits.end() ) {
						// Show only those units that are relevant to the selected subtype.
						if ( find->second.size() > 1 ) {
							for ( auto unitIt = find->second.begin(); unitIt != find->second.end(); unitIt++ ) {
								setting["options"] += {
									{ "value", Counter::UnitText.at( *unitIt ) },
									{ "label", Counter::UnitText.at( *unitIt ) }
								};
							}
						}
					} else {
						// Add all available units.
						for ( auto unitIt = Counter::UnitText.begin(); unitIt != Counter::UnitText.end(); unitIt++ ) {
							setting["options"] += {
								{ "value", unitIt->second },
								{ "label", unitIt->second }
							};
						}
					}
					option["settings"] += setting;
				}
				setting["options"] += option;
			}
			result += setting;
		}

		result += {
			{ "name", "history_retention" },
			{ "label", "History Retention" },
			{ "description", "How long to keep history in the database in days. Counter devices store their value in the history database every 5 minutes." },
			{ "type", "int" },
			{ "minimum", 1 },
			{ "default", DEVICE_COUNTER_DEFAULT_HISTORY_RETENTION },
			{ "class", "advanced" },
			{ "sort", 12 }
		};

		result += {
			{ "name", "trends_retention" },
			{ "label", "Trends Retention" },
			{ "description", "How long to keep trends in the database in months. Counter device trends keep averaged data on hourly basis and are less resource hungry." },
			{ "type", "int" },
			{ "minimum", 1 },
			{ "default", DEVICE_COUNTER_DEFAULT_TRENDS_RETENTION },
			{ "class", "advanced" },
			{ "sort", 13 }
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
			{ "name", "rate_limit" },
			{ "label", "Rate Limiter" },
			{ "description", "Limits the number of updates to once per configured time period in seconds." },
			{ "type", "double" },
			{ "class", "advanced" },
			{ "sort", 999 }
		};

		for ( auto const& plugin : g_controller->getAllPlugins() ) {
			plugin->updateDeviceSettingsJson( Device::shared_from_this(), result, plugin == this->getPlugin() );
		}

		return result;
	};

	void Counter::putSettingsJson( const nlohmann::json& settings_ ) {
		// If there's only one unit suitable for the subtype, no unit will be posted because there's no
		// corresponding field (see notes in getSettingsJson), so the correct unit needs to be set here.
		if ( this->m_settings->get<bool>( DEVICE_SETTING_ALLOW_UNIT_CHANGE, false ) ) {
			Counter::SubType subtype = Counter::resolveTextSubType( jsonGet<>( settings_, "subtype", Counter::SubTypeText.at( Counter::SubType::GENERIC ) ) );
			if (
				Counter::SubTypeUnits.find( subtype ) != Counter::SubTypeUnits.end()
				&& Counter::SubTypeUnits.at( subtype ).size() == 1
			) {
				this->m_settings->put( "unit", Counter::resolveTextUnit( Counter::SubTypeUnits.at( subtype ).front() ) );
			}
		}
	};

	json Counter::getData( unsigned int range_, const std::string& interval_, const std::string& group_ ) const {
		std::vector<std::string> validIntervals = { "hour", "day", "week", "month", "year" };
		if ( std::find( validIntervals.begin(), validIntervals.end(), interval_ ) == validIntervals.end() ) {
			return json::array();
		}
		std::string interval = interval_;
		if ( interval == "week" ) {
			interval = "day";
			range_ *= 7;
		}

		std::vector<std::string> validGroups = { "hour", "day", "month", "year" };
		if ( std::find( validGroups.begin(), validGroups.end(), group_ ) == validGroups.end() ) {
			return json::array();
		}

		std::string unit = this->m_settings->get( "unit", this->m_settings->get( DEVICE_SETTING_DEFAULT_UNIT, "" ) );
		std::string format = Counter::UnitFormat.at( Counter::resolveTextUnit( unit ) );
		double divider = this->m_settings->get<double>( "divider", 1 );

		std::string dateFormat = "%Y-%m-%d %H:30:00";
		std::string groupFormat = "%Y-%m-%d-%H";
		std::string start = "'start of day','+' || strftime( '%H' ) || ' hours'";
		if ( group_ == "day" ) {
			dateFormat = "%Y-%m-%d 12:00:00";
			groupFormat = "%Y-%m-%d";
			start = "'start of day'";
		} else if ( group_ == "month" ) {
			dateFormat = "%Y-%m-15 12:00:00";
			groupFormat = "%Y-%m";
			start = "'start of month'";
		} else if ( group_ == "year" ) {
			dateFormat = "%Y-06-15 12:00:00";
			groupFormat = "%Y";
			start = "'start of year'";
		}
		return g_database->getQuery<json>(
			"SELECT CAST( printf( %Q, sum( `diff` ) / %.6f ) AS REAL ) AS `value`, CAST( strftime( '%%s', strftime( %Q, MAX( `date` ) ) ) AS INTEGER ) AS `timestamp`, strftime( %Q, MAX( `date` ) ) AS `date` "
			"FROM `device_counter_trends` "
			"WHERE `device_id` = %d "
			"AND `date` >= datetime( 'now', '-%d %s', %s ) "
			"GROUP BY strftime( %Q, `date` ) "
			"ORDER BY `date` ASC ",
			format.c_str(),
			divider,
			dateFormat.c_str(),
			dateFormat.c_str(),
			this->m_id,
			range_,
			interval.c_str(),
			start.c_str(),
			groupFormat.c_str()
		);
	};

	void Counter::_processValue( const Device::UpdateSource& source_, const t_value& value_ ) {

		// Make a local backup of the original value (the plugin might want to revert it).
		t_value previous = this->m_value;
		this->m_value = value_;

		// If the update originates from the plugin it is not send back to the plugin again.
		bool success = true;
		bool apply = true;
		for ( auto const& plugin : g_controller->getAllPlugins() ) {
			if (
				plugin != this->getPlugin()
				|| ( source_ & Device::UpdateSource::PLUGIN ) != Device::UpdateSource::PLUGIN
			) {
				success = success && plugin->updateDevice( source_, Device::shared_from_this(), plugin == this->getPlugin(), apply );
			}
		}
		if ( success && apply ) {
			if ( this->m_enabled ) {
				std::string date = "strftime( '%Y-%m-%d %H:', datetime( 'now' ) ) || CASE WHEN CAST( strftime( '%M',  datetime( 'now' ) ) AS INTEGER ) < 10 THEN '0' ELSE '' END || CAST( CAST( strftime( '%M', datetime( 'now' ) ) AS INTEGER ) / 5 * 5 AS TEXT ) || ':00'";

				g_database->putQuery(
					"REPLACE INTO `device_counter_history` ( `device_id`, `date`, `value`, `samples` ) "
					"VALUES ( "
						"%d, "
						"%s, "
						"COALESCE( ( "
							"SELECT printf( '%%.6f', ( ( `value` * `samples` ) + %.6f ) / ( `samples` + 1 ) ) "
							"FROM `device_counter_history` "
							"WHERE `device_id` = %d "
							"AND `date` = %s "
						"), %.6f ), "
						"COALESCE( ( "
							"SELECT `samples` + 1 "
							"FROM `device_counter_history` "
							"WHERE `device_id` = %d "
							"AND `date` = %s "
						"), 1 ) "
					")",
					this->m_id,
					date.c_str(),
					this->m_value,
					this->m_id,
					date.c_str(),
					this->m_value,
					this->m_id,
					date.c_str()
				);
			}
			this->m_source = source_;
			this->m_updated = system_clock::now();
			if (
				this->m_enabled
				&& this->getPlugin()->getState() >= Plugin::State::READY
			) {
				g_controller->newEvent<Counter>( std::static_pointer_cast<Counter>( Device::shared_from_this() ), source_ );
			}
			Logger::logr( Logger::LogLevel::NORMAL, this, "New value %.3lf.", this->m_value );
		} else {
			this->m_value = previous;
		}
	};

	void Counter::_processTrends() const {
		std::string hourFormat = "%Y-%m-%d %H:30:00";
		std::string groupFormat = "%Y-%m-%d-%H";

		auto trends = g_database->getQuery(
			"SELECT MAX( rowid ) AS `last_rowid`, strftime( %Q, MAX( `date` ) ) AS `date`, MAX( `value` ) AS `max` "
			"FROM `device_counter_history` "
			"WHERE `device_id`=%d AND `Date` > datetime( 'now', '-5 hour' ) "
			"GROUP BY strftime( %Q, `date` )",
			hourFormat.c_str(),
			this->m_id,
			groupFormat.c_str()
		);

		// In order to properly calculate the diff between the beginning and ending of an hour, the maximum of the
		// previous hour is needed, otherwise the first 5 minutes are lost. So the first record is skipped while it's
		// maximum value is kept to calculate the diff for the next hour.
		double max;
		for ( auto trendsIt = trends.begin(); trendsIt != trends.end(); trendsIt++ ) {
			if ( trendsIt != trends.begin() ) {
				try {
					auto value = g_database->getQueryValue<double>(
						"SELECT `value` "
						"FROM `device_counter_history` "
						"WHERE rowid = %q",
						(*trendsIt)["last_rowid"].c_str()
					);
					g_database->putQuery(
						"REPLACE INTO `device_counter_trends` ( `device_id`, `last`, `diff`, `date` ) "
						"VALUES ( %d, %.6f, %.6f, %Q )",
						this->m_id,
						value,
						std::stod( (*trendsIt)["max"] ) - max,
						(*trendsIt)["date"].c_str()
					);
				} catch( const Database::NoResultsException& ex_ ) { /* ignore */ }
			}
			max = std::stod( (*trendsIt)["max"] );
		}
	};

	void Counter::_purgeHistoryAndTrends() const {
		g_database->putQuery(
			"DELETE FROM `device_counter_history` "
			"WHERE `device_id` = %d "
			"AND `date` < datetime( 'now','-%d day' )",
			this->m_id,
			this->m_settings->get<int>( "history_retention", DEVICE_COUNTER_DEFAULT_HISTORY_RETENTION )
		);
		g_database->putQuery(
			"DELETE FROM `device_counter_trends` "
			"WHERE `device_id` = %d "
			"AND `date` < datetime( 'now','-%d month' )",
			this->m_id,
			this->m_settings->get<int>( "trends_retention", DEVICE_COUNTER_DEFAULT_TRENDS_RETENTION )
		);
	};

}; // namespace micasa
