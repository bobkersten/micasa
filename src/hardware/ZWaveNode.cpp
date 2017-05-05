#include <chrono>

#include "ZWaveNode.h"

#include "ZWave/CommandClasses.h"

#include "../Logger.h"
#include "../User.h"

#include "../device/Level.h"
#include "../device/Counter.h"
#include "../device/Switch.h"

// OpenZWave includes
#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Group.h"
#include "Notification.h"
#include "value_classes/ValueStore.h"
#include "value_classes/Value.h"
#include "value_classes/ValueBool.h"
#include "platform/Log.h"
#include "Defs.h"

#ifdef _DEBUG
	#include <cassert>
#endif // _DEBUG

namespace micasa {

	using namespace nlohmann;
	using namespace OpenZWave;

	const char* ZWaveNode::label = "Z-Wave Node";

	ZWaveNode::ZWaveNode( const unsigned int id_, const Hardware::Type type_, const std::string reference_, const std::shared_ptr<Hardware> parent_ ) :
		Hardware( id_, type_, reference_, parent_ ),
		m_homeId( 0 ),
		m_nodeId( 0 ),
		m_configuration( json::object() )
	{
	};

	void ZWaveNode::start() {
#ifdef _DEBUG
		assert( this->m_settings->contains( { "home_id", "node_id" } ) && "ZWaveNode should be declared with home_id and node_id." );
#endif // _DEBUG
		Logger::log( Logger::LogLevel::VERBOSE, this, "Starting..." );
		Hardware::start();

		this->m_homeId = this->m_settings->get<unsigned int>( "home_id" );
		this->m_nodeId = this->m_settings->get<unsigned int>( "node_id" );

		this->declareDevice<Switch>( "heal", "Node Heal", {
			{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::ANY ) },
			{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Switch::resolveTextSubType( Switch::SubType::ACTION ) },
			{ DEVICE_SETTING_MINIMUM_USER_RIGHTS,    User::resolveRights( User::Rights::INSTALLER ) },
			{ "ignore_duplicates", false }
		} )->updateValue( Device::UpdateSource::HARDWARE, Switch::Option::IDLE, true );
		this->declareDevice<Switch>( "identify", "Identify Node", {
			{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::ANY ) },
			{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Switch::resolveTextSubType( Switch::SubType::ACTION ) },
			{ DEVICE_SETTING_MINIMUM_USER_RIGHTS,    User::resolveRights( User::Rights::INSTALLER ) },
			{ "ignore_duplicates", false }
		} )->updateValue( Device::UpdateSource::HARDWARE, Switch::Option::IDLE, true );
		this->declareDevice<Switch>( "config", "Request Node Config", {
			{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::ANY ) },
			{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Switch::resolveTextSubType( Switch::SubType::ACTION ) },
			{ DEVICE_SETTING_MINIMUM_USER_RIGHTS,    User::resolveRights( User::Rights::INSTALLER ) },
			{ "ignore_duplicates", false }
		} )->updateValue( Device::UpdateSource::HARDWARE, Switch::Option::IDLE, true );
	};

	void ZWaveNode::stop() {
		Logger::log( Logger::LogLevel::VERBOSE, this, "Stopping..." );
		Hardware::stop();
	};

	std::string ZWaveNode::getLabel() const {
		return this->m_settings->get( "label", std::string( ZWaveNode::label ) );
	};

	json ZWaveNode::getJson( bool full_ ) const {
		json result = Hardware::getJson( full_ );
		for ( auto const& setting: this->m_configuration ) {
			result[setting["name"].get<std::string>()] = setting.find( "value" ).value();
		}
		if ( full_ ) {
			result["settings"] = this->getSettingsJson();
		}
		return result;
	}

	json ZWaveNode::getSettingsJson() const {
		std::lock_guard<std::mutex> lock( this->m_configurationMutex );
		json result = Hardware::getSettingsJson();
		for ( auto setting: this->m_configuration ) {
			setting.erase( "value" );
			result += setting;
		}
		return result;
	};

	void ZWaveNode::putSettingsJson( const nlohmann::json& settings_ ) {

		// The name of the z-wave device can be updated and openzwave will store this value in the configuration file.
		// NOTE the name is mandatory and should've been verified before this method is called.
		Manager::Get()->SetNodeName( this->m_homeId, this->m_nodeId, settings_["name"] );

		// Settings that are present in our configuration map are pushed to the z-wave hardware. NOTE sleeping devices
		// need to be woken up before the settings are received.
		std::lock_guard<std::mutex> lock( this->m_configurationMutex );		
		for ( auto settingIt = settings_.begin(); settingIt != settings_.end(); settingIt++ ) {
			auto find = this->m_configuration.find( settingIt.key() );
			if ( find != this->m_configuration.end() ) {
				auto& config = *find;

				if ( jsonGet<bool>( config["readonly"] ) ) {
					continue;
				}
					
				ValueID valueId( this->m_homeId, std::stoull( settingIt.key() ) );
				ValueID::ValueType type = valueId.GetType();					
				if ( type == ValueID::ValueType_Decimal ) {
					float value = jsonGet<float>( settingIt.value() );
					if ( Manager::Get()->SetValue( valueId, value ) ) {
						config["value"] = value;
					}
				} else if ( type == ValueID::ValueType_Bool ) {
					bool value = jsonGet<bool>( settingIt.value() );
					if ( Manager::Get()->SetValue( valueId, value ) ) {
						config["value"] = value;
					}
				} else if ( type == ValueID::ValueType_Byte ) {
					unsigned int value = jsonGet<unsigned int>( settingIt.value() );
					if ( Manager::Get()->SetValue( valueId, (uint8)value ) ) {
						config["value"] = value;
					}
				} else if ( type == ValueID::ValueType_Short ) {
					int16 value = jsonGet<int16>( settingIt.value() );
					if ( Manager::Get()->SetValue( valueId, value ) ) {
						config["value"] = value;
					}
				} else if ( type == ValueID::ValueType_Int ) {
					int32 value = jsonGet<int32>( settingIt.value() );
					if ( Manager::Get()->SetValue( valueId, value ) ) {
						config["value"] = value;
					}
				} else if ( type == ValueID::ValueType_List ) {
					std::string value = jsonGet<std::string>( settingIt.value() );
					if ( Manager::Get()->SetValueListSelection( valueId, value ) ) {
						config["value"] = value;
					}
				} else if ( type == ValueID::ValueType_String ) {
					std::string value = jsonGet<std::string>( settingIt.value() );
					if ( Manager::Get()->SetValue( valueId, value ) ) {
						config["value"] = value;
					}
				}
			}
		}
	};

	json ZWaveNode::getDeviceJson( std::shared_ptr<const Device> device_, bool full_ ) const {
		json result = json::object();

		try {
			ValueID valueId( this->m_homeId, std::stoull( device_->getReference() ) );
			switch( valueId.GetCommandClassId() ) {
				case COMMAND_CLASS_SWITCH_BINARY: {
					result["prevent_race_conditions"] = device_->getSettings()->get<bool>( "prevent_race_conditions", false );
				}
			}
		} catch( std::invalid_argument ) { } // reference isn't a valueid, like an action device
		
		return result;
	};

	json ZWaveNode::getDeviceSettingsJson( std::shared_ptr<const Device> device_ ) const {
		json result = json::array();

		try {
			ValueID valueId( this->m_homeId, std::stoull( device_->getReference() ) );
			switch( valueId.GetCommandClassId() ) {
				case COMMAND_CLASS_SWITCH_BINARY: {
					result += {
						{ "name", "prevent_race_conditions" },
						{ "label", "Prevent Race Conditions" },
						{ "description", "Try to prevent race conditions, which are situations where a scripted and manual switch action occur shortly after each other, where the second action inadvertently undoes the first." },
						{ "type", "boolean" },
						{ "class", "advanced" },
						{ "sort", 99 }
					};
				}
			}
		} catch( std::invalid_argument ) { } // reference isn't a valueid, like an action device

		return result;
	};

	bool ZWaveNode::updateDevice( const Device::UpdateSource& source_, std::shared_ptr<Device> device_, bool& apply_ ) {

		// First handle all controller action pseudo devices.
		if (
			device_->getType() == Device::Type::SWITCH
			&& (
				device_->getReference() == "heal"
				|| device_->getReference() == "identify"
				|| device_->getReference() == "config"
			)
		) {
			std::shared_ptr<Switch> device = std::static_pointer_cast<Switch>( device_ );
			if ( device->getValueOption() == Switch::Option::ACTIVATE ) {

				if (
					this->getState() <= Hardware::State::INIT
					|| this->m_parent->getState() < Hardware::State::READY
				) {
					Logger::log( Logger::LogLevel::ERROR, this, "Controller not ready." );
					return false;
				}

				// This method is most likely called from the scheduler and should therefore not block for too long, so
				// we're making several short attempts to obtain the manager lock instead of blocking for a long time.
				this->m_scheduler.schedule( 0, ( OPEN_ZWAVE_MANAGER_TRY_LOCK_DURATION_MSEC / OPEN_ZWAVE_MANAGER_TRY_LOCK_ATTEMPTS ) - OPEN_ZWAVE_MANAGER_TRY_LOCK_MSEC, OPEN_ZWAVE_MANAGER_TRY_LOCK_ATTEMPTS, this, [=]( Scheduler::Task<>& task_ ) {
					if ( ZWave::g_managerMutex.try_lock_for( std::chrono::milliseconds( OPEN_ZWAVE_MANAGER_TRY_LOCK_MSEC ) ) ) {
						std::lock_guard<std::timed_mutex> lock( ZWave::g_managerMutex, std::adopt_lock );

						if ( device->getReference() == "heal" ) {

							Manager::Get()->HealNetworkNode( this->m_homeId, this->m_nodeId, true );
							device->updateValue( source_ | Device::UpdateSource::HARDWARE, Switch::Option::ACTIVATE );
							Logger::log( Logger::LogLevel::NORMAL, this, "Node heal initiated." );

						} else if ( device->getReference() == "identify" ) {

							Manager::Get()->RefreshNodeInfo( this->m_homeId, this->m_nodeId );
							device->updateValue( source_ | Device::UpdateSource::HARDWARE, Switch::Option::ACTIVATE );
							Logger::log( Logger::LogLevel::NORMAL, this, "Identifying node." );

						} else if ( device->getReference() == "config" ) {

							Manager::Get()->RequestAllConfigParams( this->m_homeId, this->m_nodeId );
							device->updateValue( source_ | Device::UpdateSource::HARDWARE, Switch::Option::ACTIVATE );
							Logger::log( Logger::LogLevel::NORMAL, this, "Requesting all node configuration parameters." );
						}

						task_.repeat = 0; // done

					// After several tries the manager instance still isn't ready, so we're bailing out with an error.
					} else if ( task_.repeat == 0 ) {
						Logger::log( Logger::LogLevel::ERROR, this, "Controller busy, command failed." );	
					}
				} );

				apply_ = false; // value is applied only after a successfull command
				return true;
			}

			return false;

		// Process all other device types.
		} else {

			if ( this->getState() != Hardware::State::READY ) {
				if ( this->getState() == Hardware::State::FAILED ) {
					Logger::log( Logger::LogLevel::WARNING, this, "Node is dead." );
					return false;
				} else if ( this->getState() == Hardware::State::SLEEPING ) {
					Logger::log( Logger::LogLevel::WARNING, this, "Node is sleeping." );
					// fallthrough, event can be sent regardless of sleeping state.
				} else {
					Logger::log( Logger::LogLevel::WARNING, this, "Node is busy." );
					return false;
				}
			}

			// Reconstruct the value id which is needed to send a command to a node.
			ValueID valueId( this->m_homeId, std::stoull( device_->getReference() ) );
			if (
				valueId.GetCommandClassId() == COMMAND_CLASS_SWITCH_BINARY
				&& valueId.GetType() == ValueID::ValueType_Bool
				&& device_->getType() == Device::Type::SWITCH
			) {
				std::shared_ptr<Switch> device = std::static_pointer_cast<Switch>( device_ );
				bool value = ( device->getValueOption() == Switch::Option::ON ) ? true : false;
				std::string valueStr = device->getValue();

				// This method is most likely called from the scheduler and should therefore not block for too long, so
				// we're making several short attempts to obtain the manager lock instead of blocking for a long time.
				this->m_scheduler.schedule( 0, ( OPEN_ZWAVE_MANAGER_TRY_LOCK_DURATION_MSEC / OPEN_ZWAVE_MANAGER_TRY_LOCK_ATTEMPTS ) - OPEN_ZWAVE_MANAGER_TRY_LOCK_MSEC, OPEN_ZWAVE_MANAGER_TRY_LOCK_ATTEMPTS, this, [=]( Scheduler::Task<>& task_ ) {
					if ( ZWave::g_managerMutex.try_lock_for( std::chrono::milliseconds( OPEN_ZWAVE_MANAGER_TRY_LOCK_MSEC ) ) ) {
						std::lock_guard<std::timed_mutex> lock( ZWave::g_managerMutex, std::adopt_lock );

						if ( this->_queuePendingUpdate( device->getReference(), source_, valueStr, OPEN_ZWAVE_NODE_BUSY_BLOCK_MSEC, OPEN_ZWAVE_NODE_BUSY_WAIT_MSEC ) ) {
							Manager::Get()->SetValue( valueId, value );
						} else {
							Logger::log( Logger::LogLevel::WARNING, this, "Node busy." );
						}

						task_.repeat = 0; // done

					// After several tries the manager instance still isn't ready, so we're bailing out with an error.
					} else if ( task_.repeat == 0 ) {
						Logger::log( Logger::LogLevel::ERROR, this, "Controller busy, command failed." );	
					}
				} );

			} else if (
				valueId.GetCommandClassId() == COMMAND_CLASS_SWITCH_MULTILEVEL
				&& valueId.GetType() == ValueID::ValueType_Byte
				&& device_->getType() == Device::Type::LEVEL
			) {
				std::shared_ptr<Level> device = std::static_pointer_cast<Level>( device_ );
				auto value = uint8( device->getValue() );

				// This method is most likely called from the scheduler and should therefore not block for too long, so
				// we're making several short attempts to obtain the manager lock instead of blocking for a long time.
				this->m_scheduler.schedule( 0, ( OPEN_ZWAVE_MANAGER_TRY_LOCK_DURATION_MSEC / OPEN_ZWAVE_MANAGER_TRY_LOCK_ATTEMPTS ) - OPEN_ZWAVE_MANAGER_TRY_LOCK_MSEC, OPEN_ZWAVE_MANAGER_TRY_LOCK_ATTEMPTS, this, [=]( Scheduler::Task<>& task_ ) {
					if ( ZWave::g_managerMutex.try_lock_for( std::chrono::milliseconds( OPEN_ZWAVE_MANAGER_TRY_LOCK_MSEC ) ) ) {
						std::lock_guard<std::timed_mutex> lock( ZWave::g_managerMutex, std::adopt_lock );

						if ( this->_queuePendingUpdate( device->getReference(), source_, OPEN_ZWAVE_NODE_BUSY_BLOCK_MSEC, OPEN_ZWAVE_NODE_BUSY_WAIT_MSEC ) ) {
							Manager::Get()->SetValue( valueId, value );
						} else {
							Logger::log( Logger::LogLevel::WARNING, this, "Node busy." );
						}

						task_.repeat = 0; // done

					// After several tries the manager instance still isn't ready, so we're bailing out with an error.
					} else if ( task_.repeat == 0 ) {
						Logger::log( Logger::LogLevel::ERROR, this, "Controller busy, command failed." );	
					}
				} );

			} else {
				Logger::log( Logger::LogLevel::ERROR, this, "Invalid command." );
				return false;
			}

			apply_ = false; // value is applied only after a successfull command
			return true;
		}
	};

	void ZWaveNode::_handleNotification( const Notification* notification_ ) {
		// No need to lock here, the parent ZWave instance already holds a lock while proxying the
		// notification to us.
#ifdef _DEBUG
		assert( this->m_homeId == notification_->GetHomeId() && "ZWaveNode home_id should be correct." );
		assert( this->m_nodeId == notification_->GetNodeId() && "ZWaveNode node_id should be correct." );
#endif // _DEBUG

		switch( notification_->GetType() ) {

			case Notification::Type_EssentialNodeQueriesComplete: {
				if ( this->getState() == Hardware::State::INIT ) {
					Logger::log( Logger::LogLevel::NORMAL, this, "Node ready." );
					this->setState( Hardware::State::READY );
				}
				this->_updateNames();
				break;
			}

			case Notification::Type_NodeQueriesComplete: {
				this->_updateNames();
				break;
			}

			case Notification::Type_ValueAdded:
			case Notification::Type_ValueChanged: {
				ValueID valueId = notification_->GetValueID();
				Device::UpdateSource source = Device::UpdateSource::HARDWARE;
				this->_processValue( valueId, source );
				break;
			}

			case Notification::Type_SceneEvent: {
				// Scene (activation) events are handled ourselves because they do not represent a valid value id.
				this->declareDevice<Switch>( "scene_" + std::to_string( notification_->GetSceneId() ), "Scene", {
					{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::HARDWARE ) }
				} )->updateValue( Device::UpdateSource::HARDWARE, Switch::Option::ACTIVATE );
				break;
			}

			case Notification::Type_Notification: {
				switch( notification_->GetNotification() ) {
					case Notification::Code_Alive: {
						if ( this->getState() == Hardware::State::FAILED ) {
							Logger::log( Logger::LogLevel::NORMAL, this, "Node is alive again." );
						}
						this->_updateNames();
						this->setState( Hardware::State::READY );
						break;
					}
					case Notification::Code_Dead: {
						Logger::log( Logger::LogLevel::ERROR, this, "Node is dead." );
						this->setState( Hardware::State::FAILED );
						break;
					}
					case Notification::Code_Awake: {
						if ( this->getState() == Hardware::State::SLEEPING ) {
							Logger::log( Logger::LogLevel::NORMAL, this, "Node is awake again." );
						}
						this->setState( Hardware::State::READY );
						this->_updateNames();
						break;
					}
					case Notification::Code_Sleep: {
						Logger::log( Logger::LogLevel::NORMAL, this, "Node is asleep." );
						this->setState( Hardware::State::SLEEPING );
						break;
					}
					case Notification::Code_Timeout: {
						Logger::log( Logger::LogLevel::WARNING, this, "Node timeout." );
						break;
					}
				}
				break;
			}

			case Notification::Type_NodeProtocolInfo: {
				this->_updateNames();
			}

			case Notification::Type_NodeNaming: {
				this->_updateNames();
				break;
			}

			default: {
				break;
			}
		}
	};

	void ZWaveNode::_processValue( const ValueID& valueId_, Device::UpdateSource source_ ) {
		std::string label = Manager::Get()->GetValueLabel( valueId_ );
		unsigned int index = valueId_.GetIndex();
		std::string reference = std::to_string( valueId_.GetId() );
		
		// Some values are not going to be processed ever and can be filtered out beforehand. NOTE these labels cannot
		// be changed by the end-user so they're safe to be checked against.
		if (
			"Exporting" == label
			|| "Interval" == label
			|| "Previous Reading" == label
			|| "Library Version" == label
			|| "Protocol Version" == label
			|| "Application Version" == label
			|| "Previous Reading" == label
			|| "Power Factor" == label
		) {
			return;
		}

		// OpenZWave has an option to filter out duplicate values, but still it seems that some duplicate values are
		// received anyhow. This function detects duplicate values (value id's that report the exact same value more
		// than once).
		auto fIsDuplicate = [this,&valueId_,&reference]() -> bool {
			std::string stringValue;
			Manager::Get()->GetValueAsString( valueId_, &stringValue );
			std::string valueIdExt = reference + "_df_" + stringValue;
			if (
				stringValue.empty()
				|| this->_queuePendingUpdate( valueIdExt, 0, OPEN_ZWAVE_NODE_DUPLICATE_VALUE_FILTER_MSEC )
			) {
				return false;
			} else {
				return true;
			}
		};

		// Process all other values by command class.
		unsigned int commandClass = valueId_.GetCommandClassId();
		switch( commandClass ) {

			case COMMAND_CLASS_POWERLEVEL: {
				// This is actually not ONE command class but several; Powerlevel, Timeout, Set Powerlevel, Test Node,
				// Test Powerlevel, Frame Count, Test, Report, Test Status, Acked Frames and possibly others. They are
				// used to keep the mesh network in optimal state and do not provide any meaningfull information for
				// the end user. They are thus ignored.
				break;
			}

			case COMMAND_CLASS_PROTECTION: // not yet implemented
			case COMMAND_CLASS_NO_OPERATION:
			case COMMAND_CLASS_SWITCH_ALL:
			case COMMAND_CLASS_ZWAVE_PLUS_INFO: {
				// Not implemented yet.
				break;
			}

			case COMMAND_CLASS_BASIC: {
				// https://github.com/OpenZWave/open-zwave/wiki/Basic-Command-Class
				// Conclusion; try to use devices that send other cc values aswell as the basic set. For instance,
				// the Aeotec ZW089 Recessed Door Sensor Gen5 defaults to only sending basic messages BUT can be
				// configured to also send binary reports.
				break;
			}

			case COMMAND_CLASS_SWITCH_BINARY:
			case COMMAND_CLASS_SENSOR_BINARY: {
				// Detect subtype.
				auto subtype = Switch::SubType::GENERIC;
				auto hardwareLabel = this->getLabel();
				std::transform( hardwareLabel.begin(), hardwareLabel.end(), hardwareLabel.begin(), ::tolower );
				if ( hardwareLabel.find( "pir" ) != string::npos ) {
					subtype = Switch::SubType::MOTION_DETECTOR;
				} else if ( hardwareLabel.find( "motion" ) != string::npos ) {
					subtype = Switch::SubType::MOTION_DETECTOR;
				} else if ( hardwareLabel.find( "home security" ) != string::npos ) {
					subtype = Switch::SubType::MOTION_DETECTOR;
				} else if ( hardwareLabel.find( "door" ) != string::npos ) {
					subtype = Switch::SubType::DOOR_CONTACT;
				}
			
				Device::UpdateSource allowedUpdateSources = Device::UpdateSource::HARDWARE | Device::UpdateSource::INTERNAL;
				if ( commandClass == COMMAND_CLASS_SWITCH_BINARY ) {
					allowedUpdateSources |= Device::UpdateSource::USER;
				}

				std::string data; // data stored alognside pending update
				bool wasPendingUpdate = this->_releasePendingUpdate( reference, source_, data );
				bool boolValue = false;
				if ( valueId_.GetType() == ValueID::ValueType_Bool ) {
					if ( false == Manager::Get()->GetValueAsBool( valueId_, &boolValue ) ) {
						Logger::log( Logger::LogLevel::ERROR, this, "Unable to extract bool value." );
						return;
					}
				} else if ( valueId_.GetType() == ValueID::ValueType_Byte ) {
					unsigned char byteValue = 0;
					if ( false == Manager::Get()->GetValueAsByte( valueId_, &byteValue ) ) {
						Logger::log( Logger::LogLevel::ERROR, this, "Unable to extract byte value." );
						return;
					}
					boolValue = !!( (unsigned int)byteValue );
				}

				Switch::Option targetValue = ( boolValue ? Switch::Option::ON : Switch::Option::OFF );

				auto device = this->declareDevice<Switch>( reference, label, {
					{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( allowedUpdateSources ) },
					{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Switch::resolveTextSubType( subtype ) },
					{ DEVICE_SETTING_ALLOW_SUBTYPE_CHANGE,   true }
				} );
				if ( "Unknown" != label ) {
					device->setLabel( label );
				}
				
				// NOTE During testing it appears as if some nodes report the wrong dynamic value after an update. This
				// is known to happen with some Fibaro FGS223 firmwares where the original value is reported while the
				// new value does get set. NOTE adding INTERNAL to the update source prevents this check from running
				// more than once.
				if (
					wasPendingUpdate
					&& data.size() > 0
					&& targetValue != Switch::resolveTextOption( data )
					&& Manager::Get()->IsNodeListeningDevice( this->m_homeId, this->m_nodeId ) // useless to query battery powered devices
					&& ( source_ & Device::UpdateSource::INTERNAL ) != Device::UpdateSource::INTERNAL
					&& this->_queuePendingUpdate( reference, source_ | Device::UpdateSource::INTERNAL, data, 0, OPEN_ZWAVE_NODE_BUSY_WAIT_MSEC )
				) {
					Logger::logr( Logger::LogLevel::WARNING, this, "Possible wrong value notification. Received %s, expected %s.", Switch::resolveTextOption( targetValue ).c_str(), data.c_str() );
					Manager::Get()->RefreshValue( valueId_ );

				// If the prevent_race_conditions setting is active and the race condition pending update is present,
				// the update need to be reverted. NOTE the data stored in the pending race-condition update is readded
				// to the queue, which should make the bug-check above also work when fixing race conditions.
				} else if (
					! wasPendingUpdate
					&& this->_releasePendingUpdate( reference + "_race", data )
					&& targetValue != Switch::resolveTextOption( data )
				) {
					// NOTE the data variable is guaranteed to be set when the _releasePendingUpdate call returns true.
					// The data variable contains the value we should revert to.
					this->m_scheduler.schedule( 0, ( OPEN_ZWAVE_MANAGER_TRY_LOCK_DURATION_MSEC / OPEN_ZWAVE_MANAGER_TRY_LOCK_ATTEMPTS ) - OPEN_ZWAVE_MANAGER_TRY_LOCK_MSEC, OPEN_ZWAVE_MANAGER_TRY_LOCK_ATTEMPTS, this, [=]( Scheduler::Task<>& task_ ) {
						if ( ZWave::g_managerMutex.try_lock_for( std::chrono::milliseconds( OPEN_ZWAVE_MANAGER_TRY_LOCK_MSEC ) ) ) {
							std::lock_guard<std::timed_mutex> lock( ZWave::g_managerMutex, std::adopt_lock );

							if ( this->_queuePendingUpdate( reference, source_, data, OPEN_ZWAVE_NODE_BUSY_BLOCK_MSEC, OPEN_ZWAVE_NODE_BUSY_WAIT_MSEC ) ) {
								Manager::Get()->SetValue( valueId_, ( Switch::resolveTextOption( data ) == Switch::Option::ON ) ? true : false );
								Logger::logr( Logger::LogLevel::WARNING, this, "Preventing race condition. Received %s, should remain %s.", Switch::resolveTextOption( targetValue ).c_str(), data.c_str() );
							}

							task_.repeat = 0; // done

						// After several tries the manager instance still isn't ready, so we're bailing out with an
						// error.
						} else if ( task_.repeat == 0 ) {
							Logger::log( Logger::LogLevel::ERROR, this, "Controller busy, command failed." );	
						}
					} );

				// After all checks have been done to make sure the proper value is set, it might still be a duplicate.
				} else if (
					! wasPendingUpdate
					&& fIsDuplicate()
				) {
					Logger::logr( Logger::LogLevel::VERBOSE, this, "Ignoring duplicate value. Received %s.", Switch::resolveTextOption( targetValue ).c_str() );

				// The value appears to be valid and should be used to set the device.
				} else {
					
					// If the prevent race conditions setting is active a special race condition pending update is set
					// which is used to detect and prevent race conditions. The value to revert to when a race condition
					// is detected is stored as data variable alongside the pending update.
					if (
						device->getSettings()->get<bool>( "prevent_race_conditions", false )
						&& Device::resolveUpdateSource( source_ & Device::UpdateSource::EVENT ) > 0
					) {
						data = Switch::resolveTextOption( targetValue );
						this->_queuePendingUpdate( reference + "_race", source_, data, 0, OPEN_ZWAVE_NODE_RACE_WAIT_MSEC );
					}

					device->updateValue( source_, targetValue );
				}
				break;
			}

			case COMMAND_CLASS_SWITCH_MULTILEVEL: {
				// For now only level multilevel devices are supported.
				// NOTE: for instance, the fibaro dimmer has several multilevel devices, such as start level-,
				// step size and dimming duration. These should be handled through the configuration.
				if ( "Level" == label ) {

					// Detect subtype.
					auto subtype = Level::SubType::GENERIC;
					auto hardwareLabel = this->getLabel();
					std::transform( hardwareLabel.begin(), hardwareLabel.end(), hardwareLabel.begin(), ::tolower );
					if ( hardwareLabel.find( "dimmer" ) != string::npos ) {
						subtype = Level::SubType::DIMMER;
					}
			
					// If there's an update mutex available we need to make sure that it is properly notified of the
					// execution of the update.
					bool wasPendingUpdate = this->_releasePendingUpdate( reference, source_ );
					if (
						! wasPendingUpdate
						&& fIsDuplicate()
					) {
						Logger::log( Logger::LogLevel::VERBOSE, this, "Ignoring duplicate value." );
						break;
					}

					unsigned char byteValue = 0;
					if (
						valueId_.GetType() == ValueID::ValueType_Byte
						&& false != Manager::Get()->GetValueAsByte( valueId_, &byteValue )
					) {
						auto device = this->declareDevice<Level>( reference, label, {
							{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::ANY ) },
							{ DEVICE_SETTING_DEFAULT_UNIT,           Level::resolveTextUnit( Level::Unit::PERCENT ) },
							{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Level::resolveTextSubType( subtype ) },
							{ DEVICE_SETTING_ALLOW_SUBTYPE_CHANGE,   true }
						} );
						if ( "Unknown" != label ) {
							device->setLabel( label );
						}
						device->updateValue( source_, (unsigned int)byteValue );
					}
				}
			}

			case COMMAND_CLASS_METER:
			case COMMAND_CLASS_SENSOR_MULTILEVEL: {
				if ( fIsDuplicate() ) {
					Logger::log( Logger::LogLevel::VERBOSE, this, "Ignoring duplicate value." );
					break;
				}

				float floatValue = 0.;
				if (
					valueId_.GetType() == ValueID::ValueType_Decimal
					&& false != Manager::Get()->GetValueAsFloat( valueId_, &floatValue )
				) {
					double multiplier = 1.;
					if (
						"Energy" == label
						|| "Gas" == label
						|| "Water" == label
					) {
						auto subtype = Counter::SubType::GENERIC;
						auto unit = Counter::Unit::GENERIC;
						if ( "Energy" == label ) {
							subtype = Counter::SubType::ENERGY;
							unit = Counter::Unit::KILOWATTHOUR;
							if ( Manager::Get()->GetValueUnits( valueId_ ) == "Wh" ) {
								multiplier = 1000.;
							}
						} else if ( "Gas" == label ) {
							subtype = Counter::SubType::GAS;
							unit = Counter::Unit::M3;
						} else if ( "Water" == label ) {
							subtype = Counter::SubType::WATER;
							unit = Counter::Unit::M3;
						}
						auto device = this->declareDevice<Counter>( reference, label, {
							{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::HARDWARE ) },
							{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Counter::resolveTextSubType( subtype ) },
							{ DEVICE_SETTING_DEFAULT_UNIT,           Counter::resolveTextUnit( unit ) }
						} );
						if ( "Unknown" != label ) {
							device->setLabel( label );
						}
						device->updateValue( source_, floatValue * multiplier );
					} else {
						auto subtype = Level::SubType::GENERIC;
						auto unit = Level::Unit::GENERIC;
						if ( "Power" == label ) {
							subtype = Level::SubType::POWER;
							unit = Level::Unit::WATT;
						} else if ( "Voltage" == label ) {
							subtype = Level::SubType::ELECTRICITY;
							unit = Level::Unit::VOLT;
						} else if ( "Current" == label ) {
							subtype = Level::SubType::CURRENT;
							unit = Level::Unit::AMPERES;
						} else if ( "Temperature" == label ) {
							subtype = Level::SubType::TEMPERATURE;
							if ( Manager::Get()->GetValueUnits( valueId_ ) == "F" ) {
								unit = Level::Unit::FAHRENHEIT;
							} else {
								unit = Level::Unit::CELSIUS;
							}
						} else if ( "Luminance" == label ) {
							subtype = Level::SubType::LUMINANCE;
							unit = Level::Unit::LUX;
							// Convert from % to lux.
							if ( Manager::Get()->GetValueUnits( valueId_ ) != "lux" ) {
								multiplier = 1000. / 100.;
							}
						}
						auto device = this->declareDevice<Level>( reference, label, {
							{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::HARDWARE ) },
							{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Level::resolveTextSubType( subtype ) },
							{ DEVICE_SETTING_DEFAULT_UNIT,           Level::resolveTextUnit( unit ) }
						} );
						if ( "Unknown" != label ) {
							device->setLabel( label );
						}
						device->updateValue( source_, floatValue * multiplier );
					}
				}
				break;
			}

			case COMMAND_CLASS_BATTERY: {
				unsigned char byteValue = 0;
				if (
					valueId_.GetType() == ValueID::ValueType_Byte
					&& false != Manager::Get()->GetValueAsByte( valueId_, &byteValue )
				) {
					// The battery command class provides battery status of the hardware and thus also for *all* devices
					// of this hardware.
					auto devices = this->getAllDevices();
					for ( auto deviceIt = devices.begin(); deviceIt != devices.end(); deviceIt++ ) {
						if (
							(*deviceIt)->getReference() != "heal"
							&& (*deviceIt)->getReference() != "identify"
							&& (*deviceIt)->getReference() != "config"
						) {
							(*deviceIt)->getSettings()->put( DEVICE_SETTING_BATTERY_LEVEL, (unsigned int)byteValue );
						}
					}

					// A 'normal' device is also created which provides detailed insight on how the battery level
					// decreases over time.
					this->declareDevice<Level>( reference, label, {
						{ DEVICE_SETTING_ALLOWED_UPDATE_SOURCES, Device::resolveUpdateSource( Device::UpdateSource::HARDWARE ) },
						{ DEVICE_SETTING_DEFAULT_SUBTYPE,        Level::resolveTextSubType( Level::SubType::BATTERY_LEVEL ) },
						{ DEVICE_SETTING_DEFAULT_UNIT,           Level::resolveTextUnit( Level::Unit::PERCENT ) }
					} )->updateValue( source_, (unsigned int)byteValue );
				}
				break;
			}

			case COMMAND_CLASS_WAKE_UP:
			case COMMAND_CLASS_CONFIGURATION: {
				std::lock_guard<std::mutex> lock( this->m_configurationMutex );
			
				if (
					commandClass == COMMAND_CLASS_WAKE_UP
					&& label != "Wake-up Interval"
				) {
					break;
				}
				
				// The configuration is stored with the reference as both the key and as the name property. This
				// comes in handy in both cases where the configuration is needed, that is when adding it to the
				// json and when processing settings from the callback.

				// Some configuration parameters refer to other paramters by index.
				if ( index > 0 ) {
					label = std::to_string( index ) + ". " + label;
				}

				json setting = {
					{ "name", reference },
					{ "label", label },
					{ "description", Manager::Get()->GetValueHelp( valueId_ ) },
					{ "class", "advanced" },
					{ "mandatory", true },
					{ "sort", 99 + index }
				};

				ValueID::ValueType type = valueId_.GetType();
				
				if ( type == ValueID::ValueType_Decimal ) {
					setting["type"] = "double";
					setting["minimum"] = Manager::Get()->GetValueMin( valueId_ );
					if ( Manager::Get()->GetValueMax( valueId_ ) > Manager::Get()->GetValueMin( valueId_ ) ) {
						setting["maximum"] = Manager::Get()->GetValueMax( valueId_ );
					} else {
						setting["maximum"] = std::numeric_limits<double>::max();
					}
					float floatValue = 0;
					Manager::Get()->GetValueAsFloat( valueId_, &floatValue );
					setting["value"] = floatValue;
				} else if ( type == ValueID::ValueType_Bool ) {
					setting["type"] = "boolean";
					bool boolValue = 0;
					Manager::Get()->GetValueAsBool( valueId_, &boolValue );
					setting["value"] = boolValue;
				} else if ( type == ValueID::ValueType_Byte ) {
					setting["type"] = "byte";
					setting["minimum"] = Manager::Get()->GetValueMin( valueId_ );
					if ( Manager::Get()->GetValueMax( valueId_ ) > Manager::Get()->GetValueMin( valueId_ ) ) {
						setting["maximum"] = Manager::Get()->GetValueMax( valueId_ );
					} else {
						setting["maximum"] = std::numeric_limits<unsigned char>::max();
					}
					unsigned char byteValue = 0;
					Manager::Get()->GetValueAsByte( valueId_, &byteValue );
					setting["value"] = (unsigned int)byteValue;
				} else if ( type == ValueID::ValueType_Short ) {
					setting["type"] = "short";
					setting["minimum"] = Manager::Get()->GetValueMin( valueId_ );
					if ( Manager::Get()->GetValueMax( valueId_ ) > Manager::Get()->GetValueMin( valueId_ ) ) {
						setting["maximum"] = Manager::Get()->GetValueMax( valueId_ );
					} else {
						setting["maximum"] = std::numeric_limits<short>::max();
					}
					short shortValue = 0;
					Manager::Get()->GetValueAsShort( valueId_, &shortValue );
					setting["value"] = shortValue;
				} else if ( type == ValueID::ValueType_Int ) {
					setting["type"] = "int";
					setting["minimum"] = Manager::Get()->GetValueMin( valueId_ );
					if ( Manager::Get()->GetValueMax( valueId_ ) > Manager::Get()->GetValueMin( valueId_ ) ) {
						setting["maximum"] = Manager::Get()->GetValueMax( valueId_ );
					} else {
						setting["maximum"] = std::numeric_limits<int>::max();
					}
					int intValue = 0;
					Manager::Get()->GetValueAsInt( valueId_, &intValue );
					setting["value"] = intValue;
				} else if ( type == ValueID::ValueType_Button ) {
					break; // skip this setting for now > unsupported (make an action switch for this?)
				} else if ( type == ValueID::ValueType_List ) {
					setting["type"] = "list";
					std::string listValue;
					Manager::Get()->GetValueListSelection( valueId_, &listValue );
					setting["value"] = listValue;

					setting["options"] = json::array();
					std::vector<std::string> items;
					Manager::Get()->GetValueListItems( valueId_, &items );
					for ( auto itemsIt = items.begin(); itemsIt != items.end(); itemsIt++ ) {
						json option = json::object();
						option["value"] = (*itemsIt);
						option["label"] = (*itemsIt);
						setting["options"] += option;
					}
				} else if ( type == ValueID::ValueType_String ) {
					setting["type"] = "string";
					std::string stringValue;
					Manager::Get()->GetValueAsString( valueId_, &stringValue );
					setting["value"] = stringValue;
				}
				
				// If the variable is readonly it's type is overridden to display, meaning that the client should
				// show the value for informational purpose only.
				if ( Manager::Get()->IsValueReadOnly( valueId_ ) ) {
					setting["type"] = "display";
					setting["readonly"] = true;
				} else {
					setting["readonly"] = false;
				}

				this->m_configuration[reference] = setting;
				break;
			}

			case COMMAND_CLASS_ALARM:
			case COMMAND_CLASS_SENSOR_ALARM: {
				// https://github.com/OpenZWave/open-zwave/wiki/Alarm-Command-Class
				break;
			}

			default: {
				Logger::logr( Logger::LogLevel::WARNING, this, "Unknown Command Class 0x%02X (%s)", commandClass, label.c_str() );
				break;
			}
		}
	};

	void ZWaveNode::_updateNames() {
		std::string type = Manager::Get()->GetNodeType( this->m_homeId, this->m_nodeId );
		std::string manufacturer = Manager::Get()->GetNodeManufacturerName( this->m_homeId, this->m_nodeId );
		std::string product = Manager::Get()->GetNodeProductName( this->m_homeId, this->m_nodeId );
		if ( ! manufacturer.empty() ) {
			this->m_settings->put( "label", manufacturer + " " + product );
		} else if ( ! type.empty() ) {
			this->m_settings->put( "label", type );
		}

		std::string nodeName = Manager::Get()->GetNodeName( this->m_homeId, this->m_nodeId );
		if (
			! nodeName.empty()
			&& ! this->m_settings->contains( "name" )
		) {
			this->m_settings->put( "name", nodeName );
		}
		
		this->m_settings->commit();
	};

}; // namespace micasa
