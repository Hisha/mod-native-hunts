-- Read-only Native Hunts Huntmaster provisioning audit for the world database.
-- This file intentionally contains no data-definition or data-modification SQL.

-- 1. Physical overlap summary. A row_count above one is a visible duplicate.
SELECT
    c.`map`, c.`position_x`, c.`position_y`, c.`position_z`,
    COUNT(*) AS `row_count`,
    GROUP_CONCAT(
        CONCAT('guid=', c.`guid`, ', entry=', c.`id`, ', name=', ct.`name`,
               ', ScriptName=', ct.`ScriptName`)
        ORDER BY c.`guid` SEPARATOR ' | '
    ) AS `spawns`
FROM `creature` c
JOIN `creature_template` ct ON ct.`entry` = c.`id`
WHERE ct.`ScriptName` = 'mod_native_hunts_huntmaster'
GROUP BY c.`map`, c.`position_x`, c.`position_y`, c.`position_z`
ORDER BY c.`map`, c.`position_x`, c.`position_y`, c.`position_z`;

-- 2. Every physical Huntmaster row with Content Manager provenance. Rows with
-- no owner are legacy/unowned; rows whose owner realm differs from ACTIVE are
-- retained rows from another realm namespace in this same world database.
SELECT
    c.`map`, c.`position_x`, c.`position_y`, c.`position_z`,
    c.`guid`, c.`id` AS `creature_entry`, ct.`name`, ct.`ScriptName`,
    so.`realm_name` AS `spawn_owner_realm`,
    so.`package_key` AS `spawn_owner_package`,
    so.`symbol` AS `spawn_symbol`,
    so.`applied_build` AS `spawn_applied_build`,
    sto.`realm_name` AS `template_owner_realm`,
    sto.`package_key` AS `template_owner_package`,
    sto.`symbol` AS `template_symbol`,
    sto.`applied_build` AS `template_applied_build`,
    ab.`realm_name` AS `active_realm`,
    CASE
        WHEN so.`realm_name` = ab.`realm_name`
         AND sto.`realm_name` = ab.`realm_name`
         AND so.`package_key` = 'mod-native-hunts'
         AND sto.`package_key` = 'mod-native-hunts'
        THEN 1 ELSE 0
    END AS `active_native_hunts_owner`
FROM `creature` c
JOIN `creature_template` ct ON ct.`entry` = c.`id`
LEFT JOIN `content_manager_server_resource_owner` so
    ON so.`resource_kind` = 'creature-spawn.guid' AND so.`entry` = c.`guid`
LEFT JOIN `content_manager_server_resource_owner` sto
    ON sto.`resource_kind` = 'creature-template.id' AND sto.`entry` = c.`id`
LEFT JOIN (
    SELECT `realm_name` FROM `content_manager_build` WHERE `state` = 'ACTIVE'
) ab ON 1 = 1
WHERE ct.`ScriptName` = 'mod_native_hunts_huntmaster'
ORDER BY c.`map`, c.`position_x`, c.`position_y`, c.`position_z`, c.`guid`;

-- 3. The authoritative template entry and spawn GUID for each logical
-- Huntmaster are the leases belonging to the one ACTIVE build's realm.
SELECT
    b.`build_number`, b.`realm_name`,
    ta.`symbol` AS `template_symbol`, ta.`allocated_value` AS `template_entry`,
    sa.`symbol` AS `spawn_symbol`, sa.`allocated_value` AS `spawn_guid`,
    c.`map`, c.`position_x`, c.`position_y`, c.`position_z`,
    ct.`name`, ct.`ScriptName`,
    CASE WHEN c.`guid` = sa.`allocated_value`
           AND c.`id` = ta.`allocated_value`
           AND sto.`entry` = ta.`allocated_value`
           AND so.`entry` = sa.`allocated_value`
         THEN 1 ELSE 0 END AS `physical_and_owner_match`
FROM `content_manager_build` b
JOIN `content_manager_allocation` ta
    ON ta.`realm_name` = b.`realm_name`
   AND ta.`package_key` = 'mod-native-hunts'
   AND ta.`resource_kind` = 'creature-template.id'
   AND ta.`symbol` LIKE 'huntmaster-%'
JOIN `content_manager_allocation` sa
    ON sa.`realm_name` = ta.`realm_name`
   AND sa.`package_key` = ta.`package_key`
   AND sa.`resource_kind` = 'creature-spawn.guid'
   AND sa.`symbol` = CONCAT(ta.`symbol`, '-spawn')
LEFT JOIN `content_manager_server_resource_owner` sto
    ON sto.`resource_kind` = ta.`resource_kind`
   AND sto.`entry` = ta.`allocated_value`
   AND sto.`realm_name` = ta.`realm_name`
   AND sto.`package_key` = ta.`package_key`
   AND sto.`symbol` = ta.`symbol`
LEFT JOIN `content_manager_server_resource_owner` so
    ON so.`resource_kind` = sa.`resource_kind`
   AND so.`entry` = sa.`allocated_value`
   AND so.`realm_name` = sa.`realm_name`
   AND so.`package_key` = sa.`package_key`
   AND so.`symbol` = sa.`symbol`
LEFT JOIN `creature` c ON c.`guid` = sa.`allocated_value`
LEFT JOIN `creature_template` ct ON ct.`entry` = ta.`allocated_value`
WHERE b.`state` = 'ACTIVE'
ORDER BY ta.`symbol`;
