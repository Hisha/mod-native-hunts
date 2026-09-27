-- Native Hunts reward/progression counters. This update targets the preceding
-- clean Native Hunts schema and intentionally uses no unsupported IF NOT EXISTS
-- column syntax.
ALTER TABLE `native_hunt_stats`
  ADD COLUMN `standard_completed` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `lifetime_completed`,
  ADD COLUMN `daily_standard_completed` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `standard_completed`,
  ADD COLUMN `daily_standard_reset_date` DATE NULL AFTER `daily_standard_completed`,
  ADD COLUMN `greens_received` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `daily_standard_reset_date`,
  ADD COLUMN `blues_received` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `greens_received`,
  ADD COLUMN `epics_received` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `blues_received`,
  ADD COLUMN `elite_completed` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `epics_received`,
  ADD COLUMN `elite_daily_accepted` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `elite_completed`,
  ADD COLUMN `elite_daily_accept_reset_date` DATE NULL AFTER `elite_daily_accepted`;
