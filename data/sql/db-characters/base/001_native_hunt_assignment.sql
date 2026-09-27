-- Native Hunts owns this clean assignment model. It intentionally has no
-- compatibility relationship with mod-hunts tables.
CREATE TABLE IF NOT EXISTS `native_hunt_assignment` (
  `character_guid` INT UNSIGNED NOT NULL,
  `huntmaster_key` VARCHAR(40) NOT NULL,
  `huntmaster_entry` MEDIUMINT UNSIGNED NOT NULL,
  `huntmaster_spawn_guid` INT UNSIGNED NOT NULL,
  `prey_key` VARCHAR(40) NOT NULL,
  `tier` TINYINT UNSIGNED NOT NULL,
  `zone_key` VARCHAR(64) NOT NULL,
  `zone_id` INT UNSIGNED NOT NULL,
  `map_id` INT UNSIGNED NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL COMMENT '1=Tracking,2=FinalRevealed,3=PreyActive,4=ReadyToTurnIn',
  `tracking_progress` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `final_site_key` VARCHAR(64) NOT NULL DEFAULT '',
  `revision` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `accepted_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`character_guid`),
  KEY `idx_native_hunt_state` (`state`),
  CONSTRAINT `chk_native_hunt_progress` CHECK (`tracking_progress` <= 100)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
