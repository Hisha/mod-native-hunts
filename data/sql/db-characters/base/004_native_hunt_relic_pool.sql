CREATE TABLE IF NOT EXISTS `native_hunt_relic_pool` (
  `pool_id` CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `zone_id` INT UNSIGNED NOT NULL,
  `relic_count` SMALLINT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `retired_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`pool_id`),
  UNIQUE KEY `uq_native_hunt_relic_pool_scope`
    (`map_id`, `instance_id`, `zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `native_hunt_relic_position` (
  `pool_id` CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `relic_index` SMALLINT UNSIGNED NOT NULL,
  `position_x` FLOAT NOT NULL,
  `position_y` FLOAT NOT NULL,
  `position_z` FLOAT NOT NULL,
  `orientation` FLOAT NOT NULL,
  PRIMARY KEY (`pool_id`, `relic_index`),
  CONSTRAINT `fk_native_hunt_relic_position_pool`
    FOREIGN KEY (`pool_id`) REFERENCES `native_hunt_relic_pool` (`pool_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
