-- The prototype relic is spawned at runtime so its shared-pool lifecycle can
-- account for persisted offline assignments. This file defines no world spawn.
INSERT IGNORE INTO `gameobject_template`
  (`entry`, `type`, `displayId`, `name`, `size`, `ScriptName`, `VerifiedBuild`)
VALUES
  (990000, 1, 3991, 'Cursed Hunt Relic', 1.0,
   'mod_native_hunts_shared_relic', 12340);
