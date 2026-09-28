-- All completions predating Elite progression were Standard Hunts. On an
-- upgraded realm, lifetime_completed therefore consists of Standard plus any
-- subsequently recorded Elite completions. Converge to that derived floor
-- without lowering a legitimate newer standard_completed value.
UPDATE `native_hunt_stats`
SET `standard_completed` = GREATEST(
  `standard_completed`,
  IF(`lifetime_completed` >= `elite_completed`,
     `lifetime_completed` - `elite_completed`, 0)
)
WHERE `standard_completed` < IF(
  `lifetime_completed` >= `elite_completed`,
  `lifetime_completed` - `elite_completed`, 0
);
