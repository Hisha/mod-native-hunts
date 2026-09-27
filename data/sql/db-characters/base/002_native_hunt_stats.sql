CREATE TABLE IF NOT EXISTS `native_hunt_stats` (
  `character_guid` INT UNSIGNED NOT NULL,
  `lifetime_completed` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_completed_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`character_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
