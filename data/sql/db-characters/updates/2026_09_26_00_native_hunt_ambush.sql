-- Existing PTR assignments gain only Native Hunts Standard tracking-ambush state.
ALTER TABLE `native_hunt_assignment`
  ADD COLUMN `ambushes_completed` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `tracking_progress`,
  ADD COLUMN `ambush_pending` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `ambushes_completed`;
