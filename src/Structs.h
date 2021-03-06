#pragma once

#include <string>
#include <vector>

namespace micasa {

	const std::vector<std::string> c_queries = {

		// Plugins
		"CREATE TABLE IF NOT EXISTS `plugins` ( "
		"`id` INTEGER PRIMARY KEY, " // functions as sqlite3 _rowid_ when named *exactly* INTEGER PRIMARY KEY
		"`plugin_id` INTEGER DEFAULT NULL, "
		"`reference` VARCHAR(64) NOT NULL, "
		"`type` VARCHAR(32) NOT NULL, "
		"`enabled` INTEGER DEFAULT 0 NOT NULL, "
		"`created` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `plugin_id` ) REFERENCES `plugins` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_plugins_reference` ON `plugins`( `reference` )",
		"CREATE INDEX IF NOT EXISTS `ix_plugins_enabled` ON `plugins`( `enabled` )",

		// Devices
		"CREATE TABLE IF NOT EXISTS `devices` ( "
		"`id` INTEGER PRIMARY KEY, "
		"`plugin_id` INTEGER NOT NULL, "
		"`reference` VARCHAR(64) NOT NULL, "
		"`label` VARCHAR(255) NOT NULL, "
		"`type` VARCHAR(32) NOT NULL, "
		"`enabled` INTEGER DEFAULT 1 NOT NULL, "
		"`created` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `plugin_id` ) REFERENCES `plugins` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_devices_plugin_id_reference` ON `devices`( `plugin_id`, `reference` )",
		"CREATE INDEX IF NOT EXISTS `ix_devices_plugin_id` ON `devices`( `plugin_id` )",
		"CREATE INDEX IF NOT EXISTS `ix_devices_enabled` ON `devices`( `enabled` )",

		// Device History
		"CREATE TABLE IF NOT EXISTS `device_text_history` ( "
		"`device_id` INTEGER NOT NULL, "
		"`value` TEXT NOT NULL, "
		"`date` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		// NOTE: no unique key for text devices > every value needs to be inserted.
		"CREATE INDEX IF NOT EXISTS `ix_device_text_history_device_id` ON `device_text_history`( `device_id` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_text_history_date` ON `device_text_history`( `date` )",

		"CREATE TABLE IF NOT EXISTS `device_counter_history` ( "
		"`device_id` INTEGER NOT NULL, "
		"`value` FLOAT NOT NULL, "
		"`date` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"`samples` INTEGER DEFAULT 1 NOT NULL, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_device_counter_history_device_id_date` ON `device_counter_history` ( `device_id`, `date` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_counter_history_device_id` ON `device_counter_history`( `device_id` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_counter_history_date` ON `device_counter_history`( `date` )",

		"CREATE TABLE IF NOT EXISTS `device_level_history` ( "
		"`device_id` INTEGER NOT NULL, "
		"`value` FLOAT NOT NULL, "
		"`date` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"`samples` INTEGER DEFAULT 1 NOT NULL, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_device_level_history_device_id_date` ON `device_level_history` ( `device_id`, `date` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_level_history_device_id` ON `device_level_history`( `device_id` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_level_history_date` ON `device_level_history`( `date` )",

		"CREATE TABLE IF NOT EXISTS `device_switch_history` ( "
		"`device_id` INTEGER NOT NULL, "
		"`value` VARCHAR(32) NOT NULL, "
		"`date` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		// NOTE: no unique key for switch devices > every value needs to be inserted.
		"CREATE INDEX IF NOT EXISTS `ix_device_switch_history_device_id` ON `device_switch_history`( `device_id` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_switch_history_date` ON `device_switch_history`( `date` )",

		// Device Trends
		"CREATE TABLE IF NOT EXISTS `device_counter_trends` ( "
		"`device_id` INTEGER NOT NULL, "
		"`last` BIGINT NOT NULL, "
		"`diff` BIGINT NOT NULL, "
		"`date` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_device_counter_trends_device_id_date` ON `device_counter_trends`( `device_id`, `date` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_counter_trends_device_id` ON `device_counter_trends`( `device_id` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_counter_trends_date` ON `device_counter_trends`( `date` )",

		"CREATE TABLE IF NOT EXISTS `device_level_trends` ( "
		"`device_id` INTEGER NOT NULL, "
		"`min` FLOAT NOT NULL, "
		"`max` FLOAT NOT NULL, "
		"`average` FLOAT NOT NULL, "
		"`date` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_device_level_trends_device_id_date` ON `device_level_trends`( `device_id`, `date` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_level_trends_device_id` ON `device_level_trends`( `device_id` )",
		"CREATE INDEX IF NOT EXISTS `ix_device_level_trends_date` ON `device_level_trends`( `date` )",

		// Users
		"CREATE TABLE IF NOT EXISTS `users` ( "
		"`id` INTEGER PRIMARY KEY, "
		"`name` VARCHAR(255) NOT NULL, "
		"`username` VARCHAR(255) NOT NULL, "
		"`password` TEXT NOT NULL, "
		"`enabled` INTEGER DEFAULT 1 NOT NULL, "
		"`rights` INTEGER DEFAULT 0 NOT NULL, "
		"`created` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"`last_login` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ) ",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_users_username_password` ON `users`( `username`, `password` )",

		// Settings
		"CREATE TABLE IF NOT EXISTS `settings` ( "
		"`key` VARCHAR(64) NOT NULL, "
		"`value` TEXT NOT NULL, "
		"`last_update` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_settings_key` ON `settings`( `key` )",

		"CREATE TABLE IF NOT EXISTS `plugin_settings` ( "
		"`plugin_id` INTEGER NOT NULL, "
		"`key` VARCHAR(64) NOT NULL, "
		"`value` TEXT NOT NULL, "
		"`last_update` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `plugin_id` ) REFERENCES `plugins` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_plugin_settings_plugin_id_key` ON `plugin_settings`( `plugin_id`, `key` )",

		"CREATE TABLE IF NOT EXISTS `device_settings` ( "
		"`device_id` INTEGER NOT NULL, "
		"`key` VARCHAR(64) NOT NULL, "
		"`value` TEXT NOT NULL, "
		"`last_update` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_device_settings_device_id_key` ON `device_settings`( `device_id`, `key` )",

		"CREATE TABLE IF NOT EXISTS `user_settings` ( "
		"`user_id` INTEGER NOT NULL, "
		"`key` VARCHAR(64) NOT NULL, "
		"`value` TEXT NOT NULL, "
		"`last_update` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `user_id` ) REFERENCES `users` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_user_settings_user_id_key` ON `user_settings`( `user_id`, `key` )",

		// Scripts
		"CREATE TABLE IF NOT EXISTS `scripts` ( "
		"`id` INTEGER PRIMARY KEY, "
		"`name` VARCHAR(255) NOT NULL, "
		"`code` TEXT, "
		"`language` VARCHAR(64) NOT NULL DEFAULT 'javascript', "
		"`enabled` INTEGER DEFAULT 1 NOT NULL, "
		"`created` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL ) ",

		"CREATE TABLE IF NOT EXISTS `x_device_scripts` ( "
		"`device_id` INTEGER NOT NULL, "
		"`script_id` INTEGER NOT NULL, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT, "
		"FOREIGN KEY ( `script_id` ) REFERENCES `scripts` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_device_scripts_device_id_script_id` ON `x_device_scripts`( `device_id`, `script_id` )",

		// Timers
		"CREATE TABLE IF NOT EXISTS `timers` ( "
		"`id` INTEGER PRIMARY KEY, "
		"`name` VARCHAR(255) NOT NULL, "
		"`cron` VARCHAR(64) NOT NULL, "
		"`enabled` INTEGER DEFAULT 1 NOT NULL, "
		"`created` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL )",

		"CREATE TABLE IF NOT EXISTS `x_timer_scripts` ( "
		"`timer_id` INTEGER NOT NULL, "
		"`script_id` INTEGER NOT NULL, "
		"FOREIGN KEY ( `timer_id` ) REFERENCES `timers` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT, "
		"FOREIGN KEY ( `script_id` ) REFERENCES `scripts` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE TABLE IF NOT EXISTS `x_timer_devices` ( "
		"`timer_id` INTEGER NOT NULL, "
		"`device_id` INTEGER NOT NULL, "
		"`value` VARCHAR(64) NOT NULL, "
		"FOREIGN KEY ( `timer_id` ) REFERENCES `timers` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_timer_scripts_timer_id_script_id` ON `x_timer_scripts`( `timer_id`, `script_id` )",

		"CREATE UNIQUE INDEX IF NOT EXISTS `ix_timer_devices_timer_id_device_id` ON `x_timer_devices`( `timer_id`, `device_id` )",

		// Links
		"CREATE TABLE IF NOT EXISTS `links` ( "
		"`id` INTEGER PRIMARY KEY, "
		"`name` VARCHAR(255) NOT NULL, "
		"`device_id` INTEGER NOT NULL, "
		"`target_device_id` INTEGER NOT NULL, "
		"`value` VARCHAR(64) NULL, "
		"`target_value` VARCHAR(64) NULL, "
		"`after` FLOAT NULL, "
		"`for` FLOAT NULL, "
		"`clear` INTEGER DEFAULT 1 NOT NULL, "
		"`enabled` INTEGER DEFAULT 1 NOT NULL, "
		"`created` TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, "
		"FOREIGN KEY ( `device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT, "
		"FOREIGN KEY ( `target_device_id` ) REFERENCES `devices` ( `id` ) ON DELETE CASCADE ON UPDATE RESTRICT )",

		"CREATE INDEX IF NOT EXISTS `ix_links_enabled` ON `links`( `enabled` )",

		"CREATE INDEX IF NOT EXISTS `ix_links_device_id` ON `links`( `device_id` )",
	};

}; // namespace micasa
