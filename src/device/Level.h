#pragma once

#include "../Device.h"

#include "../Scheduler.h"

namespace micasa {

	class Level final : public Device {

	public:
		enum class SubType: unsigned short {
			GENERIC = 1,
			TEMPERATURE,
			HUMIDITY,
			POWER,
			ELECTRICITY,
			CURRENT,
			PRESSURE,
			LUMINANCE,
			THERMOSTAT_SETPOINT,
			BATTERY_LEVEL,
			DIMMER
		}; // enum class SubType
		static const std::map<SubType, std::string> SubTypeText;
		ENUM_UTIL_W_TEXT( SubType, SubTypeText );

		enum class Unit: unsigned short {
			GENERIC = 1,
			PERCENT,
			WATT,
			VOLT,
			AMPERES,
			CELSIUS,
			FAHRENHEIT,
			PASCAL,
			LUX,
			SECONDS
		}; // enum class Unit
		static const std::map<Unit, std::string> UnitText;
		ENUM_UTIL_W_TEXT( Unit, UnitText );
		static const std::map<Unit, std::string> UnitFormat;
		static const std::map<Level::SubType, std::vector<Level::Unit>> SubTypeUnits;

		typedef double t_value;
		static const Device::Type type;

		Level( std::weak_ptr<Plugin> plugin_, const unsigned int id_, const std::string reference_, std::string label_, bool enabled_ );

		void updateValue( Device::UpdateSource source_, t_value value_ );
		t_value getValue() const { return this->m_value; };
		nlohmann::json getData( unsigned int range_, const std::string& interval_, const std::string& group_ ) const;

		void start() override;
		void stop() override;
		Device::Type getType() const override { return Level::type; };
		nlohmann::json getJson() const override;
		nlohmann::json getSettingsJson() const override;
		void putSettingsJson( const nlohmann::json& settings_ ) override;

	private:
		t_value m_value;
		Device::UpdateSource m_source;
		std::chrono::system_clock::time_point m_updated;
		struct {
			t_value value;
			unsigned long count;
			Device::UpdateSource source;
			std::weak_ptr<Scheduler::Task<>> task;
		} m_rateLimiter;

		void _processValue( const Device::UpdateSource& source_, const t_value& value_ );
		void _processTrends() const;
		void _purgeHistoryAndTrends() const;

	}; // class Level

}; // namespace micasa
