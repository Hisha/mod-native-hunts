CREATE TABLE IF NOT EXISTS `native_hunt_stats` (
  `character_guid` INT UNSIGNED NOT NULL,
  `lifetime_completed` INT UNSIGNED NOT NULL DEFAULT 0,
  `standard_completed` INT UNSIGNED NOT NULL DEFAULT 0,
  `daily_standard_completed` INT UNSIGNED NOT NULL DEFAULT 0,
  `daily_standard_reset_date` DATE NULL,
  `greens_received` INT UNSIGNED NOT NULL DEFAULT 0,
  `blues_received` INT UNSIGNED NOT NULL DEFAULT 0,
  `epics_received` INT UNSIGNED NOT NULL DEFAULT 0,
  `elite_completed` INT UNSIGNED NOT NULL DEFAULT 0,
  `elite_daily_accepted` INT UNSIGNED NOT NULL DEFAULT 0,
  `elite_daily_accept_reset_date` DATE NULL,
  `last_completed_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`character_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
