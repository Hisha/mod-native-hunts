-- Display 5011 is used by build-12340 GameObject 178104 (Resonite Crystal).
-- Its client model is the purple AmethystCrystal01.m2 rather than the buried
-- cracked-ice model previously assigned to the prototype.
UPDATE `gameobject_template`
SET `displayId` = 5011, `size` = 1.0
WHERE `entry` = 990000
  AND `ScriptName` = 'mod_native_hunts_shared_relic';
