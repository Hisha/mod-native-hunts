ALTER TABLE `native_hunt_assignment`
  ADD COLUMN `hunt_instance_id`
    CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL
    AFTER `character_guid`;

UPDATE `native_hunt_assignment`
SET `hunt_instance_id` = UUID()
WHERE `hunt_instance_id` IS NULL OR `hunt_instance_id` = '';

ALTER TABLE `native_hunt_assignment`
  MODIFY COLUMN `hunt_instance_id`
    CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  ADD UNIQUE KEY `uq_native_hunt_instance` (`hunt_instance_id`);

CREATE TABLE IF NOT EXISTS `native_hunt_collection` (
  `hunt_instance_id` CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `objective_key` VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `relic_key` VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `tracking_award` TINYINT UNSIGNED NOT NULL,
  `collected_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`hunt_instance_id`, `objective_key`, `relic_key`),
  KEY `idx_native_hunt_collection_objective`
    (`hunt_instance_id`, `objective_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
